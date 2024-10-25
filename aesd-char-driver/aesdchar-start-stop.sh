#!/bin/sh
### BEGIN INIT INFO
# Provides:          aesdchar
# Required-Start:    $all
# Required-Stop:     $all
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: Start and stop aesdchar kernel module
# Description:       This script will load the aesdchar kernel module on startup and unload it on shutdown.
### END INIT INFO

# Define paths
AESDCHAR_LOAD=/usr/bin/aesdchar_load
AESDCHAR_UNLOAD=/usr/bin/aesdchar_unload


# Load the functions from /etc/init.d/functions (if available)
[ -r /etc/init.d/functions ] && . /etc/init.d/functions

start() {
    echo -n "Loading aesdchar driver: "
    start-stop-daemon --start --quiet --exec $AESDCHAR_LOAD
    echo "Loaded aesdchar driver."
}

stop() {
    echo -n "Unloading aesdchar driver: "
    start-stop-daemon --stop --quiet --retry=TERM/30/KILL/5 --exec $AESDCHAR_UNLOAD
    echo "Unloaded aesdchar driver."
}

case "$1" in
    start)
        start
        ;;
    stop)
        stop
        ;;
    restart)
        stop
        start
        ;;
    *)
        echo "Usage: $0 {start|stop|restart}"
        exit 1
esac

exit 0