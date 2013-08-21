#!/bin/sh

EXTERNAL=$1
EXTERNAL=${EXTERNAL%.la}
EXTERNAL=${EXTERNAL#./}

TESTTYPE=$2


##################################

PD=pd
PDARGS="-nrt -nogui -path .libs"
PDARGS="-noprefs -nostdpath -nosound -nrt -nogui -path .libs"
#PDARGS="-nostdpath -nosound -nrt -nogui -path .libs"
#VALGRIND=valgrind
VALGRIND="valgrind --error-exitcode=1"

do_runtest() {
case "$1" in
 mem*|MEM*)
    ${VALGRIND} ${PD} ${PDARGS} -lib ${EXTERNAL}
 ;;
 DRD|drd)
    ${VALGRIND} --tool=drd ${PD} ${PDARGS} -lib ${EXTERNAL}
 ;;
 HEL*|hel*)
    ${VALGRIND} --tool=helgrind ${PD} ${PDARGS} -lib ${EXTERNAL}
 ;;
 *)
    ${PD} ${PDARGS} -lib ${EXTERNAL}
 ;;
esac
}


#do_runtest
#do_runtest MEM
#do_runtest DRD
#do_runtest HEL
#do_runtest && do_runtest MEM && do_runtest DRD

do_runtest $TESTTYPE
