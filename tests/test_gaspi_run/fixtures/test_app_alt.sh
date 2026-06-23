#!/bin/bash
# Alternate master binary for -b option testing
echo "MASTER_APP"
echo "argc=$(($# + 1))"
i=0
echo "argv[$i]=$0"
for arg in "$@"; do
    i=$((i + 1))
    echo "argv[$i]=$arg"
done
exit 0
