#!/bin/sh


PIPEFILE="pipe.$$"

control_c()
# run if user hits control-c
{
  echo -en "\n*** Ouch! Exiting ***\n"
  rm -f ${PIPEFILE}
  exit $?
}

P=$1
PORT=$((P))
if [ ${PORT} -lt 1 ]; then
  echo "usage: $0 <port>" 1>&2
  exit 1
fi

echo "register Ctrl-C"
trap control_c SIGINT

echo "make pipe"
mknod ${PIPEFILE} p
echo "start client"
cat ${PIPEFILE} | nc -w 10 localhost ${PORT} > ${PIPEFILE}
echo "client quit"
rm ${PIPEFILE}
