#!/bin/sh

EXTERNAL=$1
EXTERNAL=${EXTERNAL%.la}
EXTERNAL=${EXTERNAL#./}

pd -nrt -nogui -path .libs -lib ${EXTERNAL}
