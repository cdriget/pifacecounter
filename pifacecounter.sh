#!/bin/sh
#
### BEGIN INIT INFO
# Provides:          pifacecounter
# Required-Start:    $all
# Required-Stop:     $all
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: Start and stop the pifacecounter daemon
# Description:       Controls the pifacecounter daemon
### END INIT INFO
#
# Author:  Christophe DRIGET
# Version: 1.0
# Date:    31/12/2014
#

# Change the next 3 lines to suit where you install your script and what you want to call it
DIR=/home/pi
DAEMON=$DIR/pifacecounter
DAEMON_NAME=pifacecounter
 
# This next line determines what user the script runs as.
# Root generally not recommended but necessary if you are using the Raspberry Pi GPIO from Python.
DAEMON_USER=root
 
# The process ID of the script when it runs is stored here:
PIDFILE=/var/run/$DAEMON_NAME.pid
 
# Exit if the package is not installed
[ -x "$DAEMON" ] || exit 0

# Define LSB log_* functions
. /lib/lsb/init-functions
 
#
# Functions that starts/stops the daemon/service
#
do_start () {
    log_daemon_msg "Starting system $DAEMON_NAME daemon"
    start-stop-daemon --start --background --pidfile $PIDFILE --make-pidfile --user $DAEMON_USER --startas $DAEMON
    log_end_msg $?
}
do_stop () {
    log_daemon_msg "Stopping system $DAEMON_NAME daemon"
    start-stop-daemon --stop --pidfile $PIDFILE --retry 10
    log_end_msg $?
}
 
case "$1" in
 
    start|stop)
        do_${1}
        ;;
 
    restart|reload|force-reload)
        do_stop
        do_start
        ;;
 
    status)
        status_of_proc "$DAEMON_NAME" "$DAEMON" && exit 0 || exit $?
        ;;
    *)
        echo "Usage: /etc/init.d/$DEAMON_NAME {start|stop|restart|status}"
        exit 1
        ;;
 
esac
exit 0
