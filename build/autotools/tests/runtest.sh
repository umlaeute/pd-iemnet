#!/bin/sh

EXTERNAL=$1
EXTERNAL=${EXTERNAL%.la}
EXTERNAL=${EXTERNAL#./}

pd -path .libs -lib ${EXTERNAL}
