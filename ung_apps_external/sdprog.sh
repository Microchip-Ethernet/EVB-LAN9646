#!/bin/sh

LOC=sda
#LOC=mmcblk0

echo "This script will write the SD card image to /dev/$LOC"
echo "Enter 'y' to continue"
read permit
[ "$permit" = "y" ] || exit 0

MOUNTED=$(cat /proc/mounts | grep ${LOC}1)
if [ ! -z "$MOUNTED" ]; then
    sudo umount /dev/${LOC}1
    sudo umount /dev/${LOC}2
fi

sudo dd if=output/images/sdcard.img of=/dev/${LOC} bs=1M
