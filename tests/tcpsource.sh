#!/bin/sh

P=$1
PORT=$((P))
if [ ${PORT} -lt 1 ]; then
  echo "usage: $0 <port>" 1>&2
  exit 1
fi

yes abcdefghijklmnopqrstuvwxyz | nc localhost ${PORT} > /dev/null
