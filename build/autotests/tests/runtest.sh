#!/bin/sh

EXTERNAL=$1
EXTERNAL=${EXTERNAL%.la}
EXTERNAL=${EXTERNAL#./}

TESTTYPE=$2


##################################

PD=pd
#VALGRIND=valgrind
VALGRIND="valgrind --error-exitcode=1"

do_runtest() {
case "$1" in
 mem*|MEM*)
    ${VALGRIND} ${PD} -nrt -nogui -path .libs -lib ${EXTERNAL}
 ;;
 DRD|drd)
    ${VALGRIND} --tool=drd ${PD} -nrt -nogui -path .libs -lib ${EXTERNAL}
 ;;
 HEL*|hel*)
    ${VALGRIND} --tool=helgrind ${PD} -nrt -nogui -path .libs -lib ${EXTERNAL}
 ;;
 *)
    ${PD} -nrt -nogui -path .libs -lib ${EXTERNAL}
 ;;
esac
}


#do_runtest
#do_runtest MEM
#do_runtest DRD
#do_runtest HEL
#do_runtest && do_runtest MEM && do_runtest DRD

do_runtest $TESTTYPE
