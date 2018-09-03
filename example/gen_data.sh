#!/bin/bash

OUT_PATH=/tmp/cio_example_in_data
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

#echo "Removing old data: ${OUT_PATH}"
#if rm -rf ${OUT_PATH}; then
#    echo "Success"
#    echo
#else
#    echo "Failure"
#    exit -1
#fi

echo "Creating new data folder ${OUT_PATH}"
if mkdir ${OUT_PATH}; then
    echo "Success"
    echo
else
    echo "Failure"
    exit -1
fi

echo "Generating ${COUNT} files ${SIZE} Mb each..."
NEW_FILE_SIZE=${SIZE}*1024*1024
BLOCK_COUNT=$(echo "((${NEW_FILE_SIZE}/${BS}))/1" | bc)

for (( i=1; i<=${COUNT}; i++ ))
do
    FILE_NAME="${OUT_PATH}/${i}.raw"
    if [ -f ${FILE_NAME} ]; then
        echo "${FILE_NAME} EXISTS"
        ACTUAL_FILE_SIZE=$(stat -c%s ${FILE_NAME} 2>/dev/null || stat -f%z ${FILE_NAME} 2>/dev/null)
        if [ ${ACTUAL_FILE_SIZE} -eq ${NEW_FILE_SIZE} ]; then
            printf "\r${FILE_NAME} exists, skipping"
            printf "\r                                  "
            continue
        fi
    fi

    if ! dd if=/dev/zero of=${FILE_NAME} bs=4096 count=${BLOCK_COUNT} > /dev/null 2>&1; then
        echo "Failed to generate file"
        exit -1
    fi
    printf "\r${i}"
done

echo
echo "Done"
