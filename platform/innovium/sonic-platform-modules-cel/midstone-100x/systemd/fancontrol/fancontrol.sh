#! /bin/sh

### BEGIN INIT INFO
# Provides:          fancontrol
# Required-Start:    $remote_fs
# Required-Stop:     $remote_fs
# Default-Start:     2 3 4 5
# Default-Stop:
# Short-Description: fancontrol
# Description:       fancontrol configuration selector
### END INIT INFO

. /lib/lsb/init-functions

[ -f /etc/default/rcS ] && . /etc/default/rcS
PATH=/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin
DAEMON=/usr/local/bin/fancontrol
DESC="fan speed regulator"
NAME="fancontrol"
PIDFILE=/var/run/fancontrol.pid
MAIN_CONF=/usr/share/sonic/device/x86_64-cel_midstone-100x-r0/fancontrol
DEVPATH=/sys/bus/i2c/devices/2-000d
test -x $DAEMON || exit 0

init() {
  FANFAULT=$(cat ${DEVPATH}/fan1_fault)
  [ "$FANFAULT"x = "1"x ] && DIRECTION=$(cat ${DEVPATH}/fan1_direction)
  FANDIR=$([ "$DIRECTION"x = "1"x ] && echo "B2F" || echo "F2B")

  CONF=${MAIN_CONF}-${FANDIR}
}

case "$1" in
  start)
    init
    if [ -f $CONF ] ; then
        cp $CONF $MAIN_CONF
            if $DAEMON --check $CONF 1>/dev/null 2>/dev/null ; then
                  log_daemon_msg "Starting $DESC" "$NAME"
                  start-stop-daemon --start --quiet --background --pidfile $PIDFILE --startas $DAEMON $CONF
                  #install fancontrol
                  find /var/lib/docker/overlay*/ -path */sbin/fancontrol -exec cp /usr/local/bin/fancontrol {} \;
                  log_end_msg $?
            else
                  log_failure_msg "Not starting fancontrol, broken configuration file; please re-run pwmconfig."
            fi
      else
            if [ "$VERBOSE" != no ]; then
                   log_warning_msg "Not starting fancontrol; run pwmconfig first."
            fi
      fi
    ;;
  stop)
      log_daemon_msg "Stopping $DESC" "$NAME"
      start-stop-daemon --stop --quiet --pidfile $PIDFILE --oknodo --startas $DAEMON $CONF
      rm -f $PIDFILE
      log_end_msg $?
      ;;
  restart)
    $0 stop
    sleep 3
      $0 start
      ;;
  force-reload)
      if start-stop-daemon --stop --test --quiet --pidfile $PIDFILE --startas $DAEMON $CONF ; then
           $0 restart
      fi
      ;;
  status)
      status_of_proc $DAEMON $NAME $CONF && exit 0 || exit $?
      ;;
  *)
    log_success_msg "Usage: /etc/init.d/fancontrol {start|stop|restart|force-reload|status}"
    exit 1
    ;;
esac

exit 0
