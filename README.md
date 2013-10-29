pm0
===

A D-Link DNS-313 HDD power management utility

This utility enables you to interact with the kernel SATA controller driver found in the D-Link DNS-313 and other products.
It allows you to override the default timeout after which the hard drive(s) enter(s) power saving mode.
After start, the application will run as a daemon, and it also offers you the possibility to run a command of your choice,
when a drive enters power saving mode. The usefulness of this feature is questionable though, because if you try to run
anything it will generate disk I/O, and thus will get at least one of the drives out of power saving mode.
You may also set up a set of paramters to be passed to the command, including the ID of the drive (0 or 1), that has entered
power saving mode. The disk ID may be passed to the command by using the "%d" string as an argument. (See the sample config
file for an example.)
The default path for the configuration file is "/etc/pm0.conf".
After entering daemon mode, the program will log any messages to the syslog.

Usage
=====

pm0 -t|--timeout <min> [-c|--config filename] [-h|--help] [-v|--verbose] [-x|--exec cmd [args]]
Options:        -t|--timeout <minutes>:         Set HDD suspend timeout.
                -c|--config:                    Use a different config file
                -h|--help:                      Show this screen
                -v|--verbose:                   Turn on verbose logging and output
                -x|--exec:                      Execute a program with arguments on suspend
                
Usage examples
==============

pm0 --timeout 10 --config /etc/pm0.conf --verbose
pm0 --help

Downloads
=========

The files for this project may be downloaded from https://github.com/jszigetvari/pm0 or the files may be
cloned from git, using the following command:
git clone git://github.com/jszigetvari/pm0
