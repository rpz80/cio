#!/bin/bash

OUT_PATH=/tmp/cio_example_in_data_
COUNT=100
SIZE=100
BS=4096

while getopts hp:c:s: OPTION
do
    case "${OPTION}" in
        p) OUT_PATH=${OPTARG};;
        c) COUNT=${OPTARG};;
        s) SIZE=${OPTARG};;
        h) echo 'Generate initial dummy files for cio examples'
           echo ' -p output path (/tmp/cio_example_in_data by default)'
           echo ' -c number of files to generate (100 by default)'
           echo ' -s size of each file in MB (100 by default)'
           exit 0
    esac
done

rm -rf ${OUT_PATH}
mkdir ${OUT_PATH}

BLOCK_COUNT=$(echo "((${SIZE}*1024*1024)/${BS})/1" | bc)
for (( i=1; i<=${COUNT}; i++ ))
do
    dd if=/dev/zero of="${OUT_PATH}/${i}.raw" bs=4096 count=${BLOCK_COUNT}
done
