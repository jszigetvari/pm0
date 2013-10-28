/******************************************************************************\
**                                                                            **
**  pm0 - a D-Link DNS-313 HDD power management utility                       **
**                                                                            **
**  Copyright Janos Szigetvari <jszigetvari_(at)_gmail_(dot)_com>, 2012.      **
**                                                                            **
**  This program is free software: you can redistribute it and/or modify      **
**  it under the terms of the GNU General Public License as published by      **
**  the Free Software Foundation, either version 3 of the License, or         **
**  (at your option) any later version.                                       **
**                                                                            **
**  This program is distributed in the hope that it will be useful,           **
**  but WITHOUT ANY WARRANTY without even the implied warranty of             **
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             **
**  GNU General Public License for more details.                              **
**                                                                            **
**  You should have received a copy of the GNU General Public License         **
**  along with this program.  If not, see <http://www.gnu.org/licenses/>.     **
**                                                                            **
**                                                                            **
\******************************************************************************/    

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>

#ifdef _GNU_SOURCE
#include <getopt.h> /* getopt_long */
#endif

#ifdef WITH_LIBCONFIG
#include <libconfig.h>
#endif

//=========== DEFINES ==========

#define EXEC_NAME	"pm0"
#define PID_FILE	"/var/run/pm0.pid"
#define PID_TXT_LENGTH	6
#define DEV_FILE	"/dev/sl_pwr"
#define CONF_FILE	"/etc/pm0.conf"

#define IOCTL_PM0_REGISTER_PID	_IO('P',0x02)
#define IOCTL_PM0_SET_IDLETIME	_IO('P',0x03)

#define ALL_OK						0
#define ERR_OUT_OF_MEMORY			255
#define ERR_INVALID_ARG				254
#define ERR_FORK_FAIL				253
#define ERR_ALREADY_RUNNING			252
#define ERR_STAT_OTHER				251
#define ERR_CLEANUP					250
#define ERR_SIGWAIT_FAIL			249
#define ERR_SIGPROCMASK_FAIL		248
#define ERR_SIGACTION_FAIL			247
#define ERR_IOCTL_PID				246
#define ERR_IOCTL_TIMEOUT			245
#define ERR_DEV_FILE				244
#define ERR_OPEN_FAIL				243
#define ERR_WRITE_FAIL				242
#define ERR_EXECV_FAIL				241
#define ERR_CONFIG_READ_FAIL		240
#define ERR_FOPEN_FAIL				239

//=========== TYPEDEFS ==========

typedef enum { false=0, true=1 } bool;

typedef struct conf {
		char *m_conf_file;
		bool m_verbose;
		unsigned long m_timeout;
		char *m_suspend_exec;
		char **m_suspend_args;
} conf_t;

typedef conf_t * conf_ptr_t;
typedef conf_t const * conf_cptr_t;

//=========== GLOBALS ==========

conf_t pm0_conf = {
		CONF_FILE,
		false,
		0,
		NULL,
		NULL
	};
	
//=========== FUNCTION DECLARATIONS ==========

#ifdef WITH_LIBCONFIG
int init_config(config_t *, FILE *);
int read_config(config_t *, conf_ptr_t);
void close_config(config_t *);
#endif

void help(FILE *, char const * const);
bool check_exec(struct stat const *);
void exec_suspend(int);
int setup_default_args(conf_ptr_t);
void cleanup_daemon();
void cleanup_main();
void close_file(int, char*);
void fclose_file(FILE *, char *);
void daemon_task();

//=========== FUNCTIONS ==========

void help(FILE *fd, char const * const en) {
    fprintf( fd,
	     "\n"
#ifdef _GNU_SOURCE
	     "Usage: %s -t|--timeout <min>"
#ifdef WITH_LIBCONFIG
		 " [-c|--config filename]"
#endif /* WITH_LIBCONFIG */
		 " [-h|--help] [-v|--verbose] [-x|--exec cmd [args]]\n"
	     "Options:\t-t|--timeout <minutes>:\t\tSet HDD suspend timeout.\n"
#ifdef WITH_LIBCONFIG
		 "\t\t-c|--config:\t\t\tUse a different config file\n"
#endif /* WITH_LIBCONFIG */
		 "\t\t-h|--help:\t\t\tShow this screen\n"
	     "\t\t-v|--verbose:\t\t\tTurn on verbose logging and output\n"
		 "\t\t-x|--exec:\t\t\tExecute a program with arguments on suspend\n"
#else /* not _GNU_SOURCE */ 
	     "Usage: %s -t <minutes>"
#ifdef WITH_LIBCONFIG
		 " [-c filename]"
#endif /* WITH_LIBCONFIG */		 
		 " [-h] [-v] [-x cmd [args]]\n"
	     "Options:\t-t <minutes>:\t\tSet HDD suspend timeout.\n"
#ifdef WITH_LIBCONFIG
		 "\t\t-c:\t\t\tUse a different config file\n"
#endif /* WITH_LIBCONFIG */
		 "\t\t-h:\t\tShow this screen\n"
	     "\t\t-v:\t\tTurn on verbose logging and output\n"
		 "\t\t-x:\t\t\tExecute a program with arguments on suspend\n"
#endif /* _GNU_SOURCE */
	     "\n",
	     en
            );
}


bool check_exec(struct stat const *filestat) {
	uid_t u = getuid();
	gid_t g = getgid();
	
	if (pm0_conf.m_verbose == true) {
		struct group *grp = NULL;
		struct passwd *pwd = NULL;
		char *usr_str, *grp_str, *default_val = "[unknown]";
		
		if ((pwd = getpwuid(u)) == NULL) usr_str = default_val;
		else usr_str = pwd->pw_name;
		if ((grp = getgrgid(g)) == NULL) grp_str = default_val;
		else grp_str = grp->gr_name;
		
		syslog(LOG_INFO, "%s running with UID %d (%s) and GID %d (%s).\n", EXEC_NAME, u, usr_str, g, grp_str);
		
		if ((pwd = getpwuid((*filestat).st_uid)) == NULL) usr_str = default_val;
		else usr_str = pwd->pw_name;
		if ((grp = getgrgid((*filestat).st_gid)) == NULL) grp_str = default_val;
		else grp_str = grp->gr_name;
		
		syslog(LOG_INFO, "The executable is owned by UID %d (%s) and GID %d (%s).\n", filestat->st_uid, usr_str, filestat->st_gid, grp_str);		
	}
	
	if (u == filestat->st_uid) {
		if ((filestat->st_mode & S_IXUSR) == S_IXUSR) return true;
	}
	else {
		if (g == filestat->st_gid) {
			if ((filestat->st_mode & S_IXGRP) == S_IXGRP) return true;
		}
		else {
			if ((filestat->st_mode & S_IXOTH) == S_IXOTH) return true;
		}
	}
	
	return false;
}

void exec_suspend(int n) {
	struct stat stat_buf;
	pid_t cp;
	int i;
	
	char *string_buf, *string_buf_end;
	int sring_len;
	
	if (stat(pm0_conf.m_suspend_exec, &stat_buf) == -1) {
		syslog(LOG_ERR, "stat() failed on \'%s\': %s\n", pm0_conf.m_suspend_exec, strerror(errno));
		return;
	}
	else {
		if ((stat_buf.st_mode & S_IFMT) != S_IFREG) {
			syslog(LOG_ERR, "\'%s\' is not a regular file!\n", pm0_conf.m_suspend_exec);
			return;
		}
				
		if (check_exec(&stat_buf) != true) {
			syslog(LOG_ERR, "\'%s\' is not a executable for the user/group on whose behalf %s is running on!\n", pm0_conf.m_suspend_exec, EXEC_NAME);
			return;
		}
	}
	
	if ((cp = fork()) == -1) {
		syslog(LOG_ERR, "fork() failed: %s\n", strerror(errno));
		return;
	}
	if (cp == 0) {
		for (i=0; pm0_conf.m_suspend_args[i] != NULL; i++) {
			if (strcmp("%d", pm0_conf.m_suspend_args[i]) == 0) {
				snprintf(pm0_conf.m_suspend_args[i], 3, "%d", n);
			}
		}
		
		if (pm0_conf.m_verbose == true) {
			for (i=0, sring_len=1; pm0_conf.m_suspend_args[i] != NULL; i++) {
				sring_len += strlen(pm0_conf.m_suspend_args[i]);
			}
			sring_len += 3*i;//quotes(*2) and spaces
			if ((string_buf = (char *) calloc(sring_len, sizeof(char *))) == NULL) {
				syslog(LOG_ERR, "calloc() failed!\n");
				cleanup_daemon();
				exit(ERR_OUT_OF_MEMORY);
			}
			
			sprintf(string_buf, "\'%s\'", pm0_conf.m_suspend_args[0]);
			for (i=1; pm0_conf.m_suspend_args[i] != NULL; i++) {
				string_buf_end = string_buf + strlen(string_buf);
				sprintf(string_buf_end, " \'%s\'", pm0_conf.m_suspend_args[i]);
			}
				
			//a little more extensive logging will be needed!
			syslog(LOG_INFO, "Executing %s with arguments: %s.\n", pm0_conf.m_suspend_exec, string_buf);
			
			free(string_buf);
		}
		
		if (execv(pm0_conf.m_suspend_exec, pm0_conf.m_suspend_args) == -1) {
			syslog(LOG_ERR, "execv() failed on \'%s\': %s\n", pm0_conf.m_suspend_exec, strerror(errno));
			cleanup_daemon();
			exit(ERR_EXECV_FAIL);
		}
	}
}


int setup_default_args(conf_ptr_t p_conf) {
	int i;
	
	if ((p_conf->m_suspend_args = (char **) calloc(2, sizeof(char *))) == NULL) {
		fprintf(stderr, "calloc() failed!\n");
		return ERR_OUT_OF_MEMORY;
	}
	p_conf->m_suspend_args[1] = NULL;
			
	//we will need to store the executable name as the 0th argument
	i = strlen(p_conf->m_suspend_exec);
	if ((p_conf->m_suspend_args[0] = (char *) calloc(i+1, sizeof(char))) == NULL) {
		fprintf(stderr, "calloc() failed!\n");
		return ERR_OUT_OF_MEMORY;
	}
	strncpy(p_conf->m_suspend_args[0], p_conf->m_suspend_exec, i);
	return ALL_OK;
}


#ifdef WITH_LIBCONFIG

int init_config(config_t *conf_main, FILE *conf_file) {
	config_init(conf_main);		
	if (conf_file != NULL) {
		if (config_read(conf_main, conf_file) == CONFIG_FALSE) {
			fprintf(stderr, "config_read() failed!\n");
			return ERR_CONFIG_READ_FAIL;
		}
	}
	else config_read_string(conf_main, "");
	return ALL_OK;
}

//	verbose = true;
//	suspend_timeout = 20;
//	suspend_exec = "/bin/bash";
//	suspend_args = [];
int read_config(config_t *source, conf_ptr_t target) {
	config_setting_t *tmp_setting;
	char const *tmp_s;
	int tmp_i, i, j;
	
	if (config_lookup_bool(source, "main.verbose", &tmp_i) == CONFIG_TRUE) {
			if ((target->m_verbose == false) && (tmp_i == true)) target->m_verbose =  true;
	}
	
	if (target->m_verbose == true) {
		fprintf(stderr, "Now parsing config file.\n");
	}
	
	if (config_lookup_int(source, "main.suspend_timeout", &tmp_i) == CONFIG_TRUE) {
			if ((target->m_timeout == 0) && (tmp_i > 0)) target->m_timeout = (unsigned long) tmp_i;
	}
	else {
		if (target->m_verbose == true) {
			fprintf(stderr, "No setting named \'main.suspend_timeout\' was found in the config file.\n");
		}	
	}
	if (config_lookup_string(source, "main.suspend_exec", &tmp_s) == CONFIG_TRUE) {
		//no exec was set on command line AND the config file contains something more than ""
		if ((target->m_suspend_exec == NULL) && (strlen(tmp_s) > 0)) {
			i=strlen(tmp_s);
			if ((target->m_suspend_exec = (char *) calloc(i+1, sizeof(char))) == NULL) {
				fprintf(stderr, "calloc() failed!\n");
				return ERR_OUT_OF_MEMORY;
			}
			strncpy(target->m_suspend_exec, tmp_s, i);
			
			
			if ((tmp_setting = config_lookup(source, "main.suspend_args")) != NULL) {
				if ((config_setting_type(tmp_setting) == CONFIG_TYPE_ARRAY) || (config_setting_type(tmp_setting) == CONFIG_TYPE_LIST)) {
					tmp_i = config_setting_length(tmp_setting);
					if (target->m_verbose == true) {
						fprintf(stderr, "The argument list in the config file has %d elements.\n", tmp_i);
					}
					
					if ((target->m_suspend_args = (char **) calloc(tmp_i+2, sizeof(char *))) == NULL) {
						fprintf(stderr, "calloc() failed!\n");
						return ERR_OUT_OF_MEMORY;
					}
					
					tmp_s = target->m_suspend_exec;
					i = 0;
					
					do {
						j = strlen(tmp_s);
						if ((target->m_suspend_args[i] = (char *) calloc(j+1, sizeof(char))) == NULL) {
							fprintf(stderr, "calloc() failed!\n");
							return ERR_OUT_OF_MEMORY;
						}
						strncpy(target->m_suspend_args[i], tmp_s, j);
					
						if ((i<tmp_i) && ((tmp_s = config_setting_get_string_elem(tmp_setting, i)) == NULL)) {
							fprintf(stderr, "Element %d in the argument list is not a STRING!\n", i+1);
							if (target->m_verbose == true) {
								fprintf(stderr, "Discarding any further arguments.\n");
							}
							i++;//we have to set up 'i' to set the last entry after the last known good one to be NULL
							break;
						}
					} while ((i++)<tmp_i);
					target->m_suspend_args[i] = NULL;
							
				}
				else {
					fprintf(stderr, "The setting main.suspend_args is not of type ARRAY or LIST!\n");
					if (target->m_suspend_args == NULL) {
						if (target->m_verbose == true) {
							fprintf(stderr, "No valid argument list found, reverting to a default empty one.\n");
						}
						if ((i = setup_default_args(target)) != ALL_OK) return i;
					}
				}
			}
			else {
				if (target->m_verbose == true) {
					fprintf(stderr, "No setting named \'main.suspend_args\' was found in the config file.\n");
				}
			}
		}
	}
	else {
		if (target->m_verbose == true) {
			fprintf(stderr, "No setting named \'main.suspend_exec\' was found in the config file.\n");
		}	
	}
	
	return ALL_OK;
}

void close_config(config_t *conf_main) {
	config_destroy(conf_main);
}

#endif //WITH_LIBCONFIG
 
void cleanup_daemon() {
	int i=0;
	
	if (pm0_conf.m_verbose == true) {
		syslog(LOG_INFO, "Cleaning up after daemon_task().\n");
	}
	
	if (remove(PID_FILE) != 0) {
		syslog(LOG_ERR, "remove() failed on \'%s\': %s\n", PID_FILE, strerror(errno));
	}
	if (pm0_conf.m_suspend_exec != NULL) free(pm0_conf.m_suspend_exec);
	if (pm0_conf.m_suspend_args != NULL) {
		while (pm0_conf.m_suspend_args[i] != NULL) free(pm0_conf.m_suspend_args[i++]);
		free(pm0_conf.m_suspend_args);
	}
	closelog();
}


void cleanup_main() {
	int i=0;
	
	if (pm0_conf.m_verbose == true) {
		fprintf(stderr, "Cleaning up after main().\n");
	}
	
	if (pm0_conf.m_suspend_exec != NULL) free(pm0_conf.m_suspend_exec);
	if (pm0_conf.m_suspend_args != NULL) {
		while (pm0_conf.m_suspend_args[i] != NULL) free(pm0_conf.m_suspend_args[i++]);
		free(pm0_conf.m_suspend_args);
	}
}


void close_file(int fd, char *str) {
		if (close(fd) == -1) {
				syslog(LOG_ERR, "close() failed on \'%s\': %s\n", str, strerror(errno));
				cleanup_daemon();
		}
}

void fclose_file(FILE *file, char *str) {
		if (fclose(file) == -1) {
				fprintf(stderr, "fclose() failed on \'%s\': %s", str, strerror(errno));
		}
}

//=========== DAEMON ==========

void daemon_task() {
	sigset_t listen_set;
	struct stat buf;
	unsigned long d_pid = getpid();//daemon-pid - kernel expects it to be "unsigned long"
	int dev_file, pid_file, sig;
	char pid_buf[PID_TXT_LENGTH] = {0};
	
	openlog(EXEC_NAME, LOG_NDELAY | LOG_PID, LOG_DAEMON);
	
	if (pm0_conf.m_verbose == true) {
		syslog(LOG_INFO, "The %s daemon has started with PID %lu.\n", EXEC_NAME, d_pid);
	}
	
	//create, fill and close PID file
	
	if ((pid_file = open(PID_FILE, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1) {
		syslog(LOG_ERR, "open() failed on \'%s\': %s\n", PID_FILE, strerror(errno));
		cleanup_daemon();
		exit(ERR_OPEN_FAIL);
	}
	
	snprintf(pid_buf, PID_TXT_LENGTH, "%lu", d_pid);
	
	if (write(pid_file, (void *) pid_buf, strlen(pid_buf)) < 0) {
		syslog(LOG_ERR, "write() failed on \'%s\': %s\n", PID_FILE, strerror(errno));
		close_file(pid_file, PID_FILE);
		cleanup_daemon();
		exit(ERR_WRITE_FAIL);
	}
	
	close_file(pid_file, PID_FILE);
	
	//inspect and open device file for later use

	if (pm0_conf.m_verbose == true) {
		syslog(LOG_INFO, "Checking and opening device file \'%s\'.\n", DEV_FILE);
	}
	
	if (stat(DEV_FILE, &buf) == -1) {
			syslog(LOG_ERR, "stat() failed on \'%s\': %s\n", DEV_FILE, strerror(errno));
			cleanup_daemon();
			exit(ERR_STAT_OTHER);
	}
	else {
		if ((buf.st_mode & S_IFMT) != S_IFCHR) {
			syslog(LOG_ERR, "\'%s\' is not a character device!\n", DEV_FILE);
			cleanup_daemon();
			exit(ERR_DEV_FILE);
		}
	}
	

	if ((dev_file = open(DEV_FILE, O_NONBLOCK)) == -1) {
		syslog(LOG_ERR, "open() failed on \'%s\': %s\n", DEV_FILE, strerror(errno));
		cleanup_daemon();
		exit(ERR_OPEN_FAIL);
	}
	
	//kick off our ioctls
	
	if (pm0_conf.m_verbose == true) {
		syslog(LOG_INFO, "Registering %s power management daemon in kernel.\n", EXEC_NAME);
	}
	
	if (ioctl(dev_file, IOCTL_PM0_REGISTER_PID, &d_pid) == -1) {
		syslog(LOG_ERR, "IOCTL_PM0_REGISTER_PID failed: %s\n", strerror(errno));
		close_file(dev_file, DEV_FILE);
		cleanup_daemon();
		exit(ERR_IOCTL_PID);
	}
	
	if (pm0_conf.m_verbose == true) {
		syslog(LOG_INFO, "Setting suspend timeout for hard disks to %lu minute(s).\n", pm0_conf.m_timeout);
	}
	
	if (ioctl(dev_file, IOCTL_PM0_SET_IDLETIME, pm0_conf.m_timeout) == -1) {
		syslog(LOG_ERR, "IOCTL_PM0_SET_IDLETIME failed: %s\n", strerror(errno));
		close_file(dev_file, DEV_FILE);
		cleanup_daemon();
		exit(ERR_IOCTL_TIMEOUT);
	}
	
	//close our device file
	
	close_file(dev_file, DEV_FILE);
	
	
	//register listeners for signals from the kernel
	sigemptyset(&listen_set);
	sigaddset(&listen_set, SIGUSR1);
	sigaddset(&listen_set, SIGUSR2);
	sigaddset(&listen_set, SIGCHLD);
	sigaddset(&listen_set, SIGTERM);
	sigaddset(&listen_set, SIGQUIT);
	if (sigprocmask(SIG_BLOCK, &listen_set, NULL) != 0) {
		syslog(LOG_ERR, "sigwait() failed: %s\n", strerror(errno));
		cleanup_daemon();
		exit(ERR_SIGPROCMASK_FAIL);
	}
	
	/*
	The SIGUSR1 and SIGUSR2 signals are set aside for you to use any way you want.
	They're useful for interprocess communication. Since these signals are normally fatal,
	you should write a signal handler for them in the program that receives the signal. 
	*/
	
	if (pm0_conf.m_verbose == true) {
		syslog(LOG_INFO, "Starting signal processing loop.\n");
	}
	
	while (true) {
			if (sigwait(&listen_set, &sig) == 0) {
					switch (sig) {
						case SIGUSR1:
							syslog(LOG_NOTICE, "SATA HDD-1 standby initiated...\n");
							if (pm0_conf.m_suspend_exec != NULL) exec_suspend(1);
							break;
						case SIGUSR2:
							syslog(LOG_NOTICE, "SATA HDD-0 standby initiated...\n");
							if (pm0_conf.m_suspend_exec != NULL) exec_suspend(0);
							break;
						case SIGCHLD:
							while (waitpid(-1, NULL, WNOHANG) > 0);
							break;
						case SIGQUIT:
						case SIGTERM:
							if (pm0_conf.m_verbose == true) {
								syslog(LOG_INFO, "Caught SIGTERM/SIGQUIT, now exiting...\n");
							}
							cleanup_daemon();
							exit(ALL_OK);
							break;
						default:
							break;
					}
			}
			else {
					syslog(LOG_ERR, "sigwait() failed: %s\n", strerror(errno));
					cleanup_daemon();
					exit(ERR_SIGWAIT_FAIL);
			}
	}


} 
 
 
//=========== MAIN ==========

int main(int argc, char **argv, char **env) {
	char const *const optstr = "hvt:";
	pid_t c_pid;//child-pid
	struct stat buf;
	int c, i, j;

#ifdef WITH_LIBCONFIG
	config_t config_parser;
	FILE *config_file;
#endif //WITH_LIBCONFIG
	
#ifdef _GNU_SOURCE
		static struct option long_opts[] = {
			{"help", 0, NULL, 'h'},
			{"verbose", 0, NULL, 'v'},
			{"timeout", 1, NULL, 't'},
			{"config", 1, NULL, 'c'},
			{"exec", 1, NULL, 'x'},
			{NULL, 0, NULL, 0},
		};
#endif
		
	while (true) {
#ifdef _GNU_SOURCE
		c = getopt_long(argc, argv, optstr, long_opts, NULL);
#else /* not _GNU_SOURCE */ 
		c = getopt(argc, argv, optstr);
#endif /* _GNU_SOURCE */
		if (c == -1) break;

		switch (c) {
			case 'h':
				help(stdout, EXEC_NAME);
				exit(ALL_OK);
				break;
			case 'v':
				pm0_conf.m_verbose = true;
				break;
			case 'c':
				pm0_conf.m_conf_file = optarg;
				break;
			case 't':
				pm0_conf.m_timeout = strtoul(optarg, NULL, 10);
				break;
			case 'x':
				i = strlen(optarg);
				if ((pm0_conf.m_suspend_exec = (char *) calloc(i+1, sizeof(char))) == NULL) {
					fprintf(stderr, "calloc() failed!\n");
					cleanup_main();
					exit(ERR_OUT_OF_MEMORY);
				}
				strncpy(pm0_conf.m_suspend_exec, optarg, i);
				break;
			case '?':
			default:
				help(stderr, EXEC_NAME);
				exit(ERR_INVALID_ARG);
		}
		if (c == 'x') break;
	}

	//were there any arguments entered that belong to suspend_exec?
	if ((c=='x') && ((optind+=2) < argc)) {
		if ((pm0_conf.m_suspend_args = (char **) calloc((argc-optind)+2, sizeof(char *))) == NULL) {
			fprintf(stderr, "calloc() failed!\n");
			cleanup_main();
			exit(ERR_OUT_OF_MEMORY);
		}
		
		for (i=1; optind < argc; i++,optind++) {
			j = strlen(argv[optind]);
			if ((pm0_conf.m_suspend_args[i] = (char *) calloc(j+1, sizeof(char))) == NULL) {
				fprintf(stderr, "calloc() failed!\n");
				cleanup_main();
				exit(ERR_OUT_OF_MEMORY);
			}
			strncpy(pm0_conf.m_suspend_args[i], argv[optind], j);
		}
	}
	
	//was suspend_exec defined on the command line?
	if (pm0_conf.m_suspend_exec != NULL) {
		//if yes, were any arguments defined for it?
		if (pm0_conf.m_suspend_args == NULL) {
			if (pm0_conf.m_verbose == true) {
					fprintf(stderr, "No argument list was supplied for \'%s\', setting up default empty one.\n", pm0_conf.m_suspend_exec);
			}
			if ((i = setup_default_args(&pm0_conf)) != ALL_OK) {
				cleanup_main();
				exit(i);
			}
		}
	}	
	else {
		pm0_conf.m_suspend_args = NULL;
	}	
	

#ifdef WITH_LIBCONFIG

	if (pm0_conf.m_verbose == true) {
		fprintf(stderr, "Processing configuration file \'%s\'.\n", pm0_conf.m_conf_file);
	}		
	if ((config_file = fopen(pm0_conf.m_conf_file, "r")) == NULL) {
		fprintf(stderr, "fopen() failed on \'%s\': %s\n", pm0_conf.m_conf_file, strerror(errno));
		cleanup_main();
		exit(ERR_FOPEN_FAIL);
	}
	
	if ((i = init_config(&config_parser, config_file)) != ALL_OK) {
		cleanup_main();
		fclose_file(config_file, pm0_conf.m_conf_file);
		exit(i);
	}
	
	if ((i = read_config(&config_parser, &pm0_conf)) != ALL_OK) {
		cleanup_main();
		fclose_file(config_file, pm0_conf.m_conf_file);
		close_config(&config_parser);
		exit(i);
	}
	
	fclose_file(config_file, pm0_conf.m_conf_file);
	close_config(&config_parser);
	
#endif //WITH_LIBCONFIG
	
	if (pm0_conf.m_timeout == 0) {
		fprintf(stderr, "Setting the timeout argument is mandatory!\n");
		help(stderr, EXEC_NAME);
		
		cleanup_main();
		return ERR_INVALID_ARG;
	}
	
	if (stat(PID_FILE, &buf) == 0) {
		fprintf(stderr, "%s seems to be already running! If not, then \'%s\' needs to be deleted!\n", EXEC_NAME, PID_FILE);
		
		cleanup_main();
		exit(ERR_ALREADY_RUNNING);
	}
	else {
		if (errno != ENOENT) {
			fprintf(stderr, "stat() failed on \'%s\': %s\n", PID_FILE, strerror(errno));
			
			cleanup_main();
			exit(ERR_STAT_OTHER);
		}
	}
	
	fprintf(stdout, "Starting %s daemon: ", EXEC_NAME);
	if ((c_pid = fork()) == -1) {
		fprintf(stdout, "failed!\n");
		fprintf(stderr, "fork() failed: %s\n", strerror(errno));
		
		cleanup_main();
		exit(ERR_FORK_FAIL);
	}
	if (c_pid > 0) {
		fprintf(stdout, "success.\n");
		
		cleanup_main();
		exit(ALL_OK);
	}
	else {			
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
		
		daemon_task();
	}
		
}
