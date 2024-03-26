#!/bin/sh

DTB=$(grep BR2_LINUX_KERNEL_INTREE_DTS_NAME $BR2_CONFIG | cut -d"\"" -f2)

if [ "$DTB" != "at91-sam9x75_curiosity" ]; then
    cp -p ${BINARIES_DIR}/$DTB.dtb ${BINARIES_DIR}/at91-sam9x75_curiosity.dtb
fi

if [ ! -e ${BINARIES_DIR}/rootfs.ubi ]; then
    BOARD_DIR="$(dirname $0)"
    ./support/scripts/genimage.sh -c ${BOARD_DIR}/genimage.cfg
else
echo "./host/bin/sam-ba -w images -x ${BR2_EXTERNAL_MCHP_PATH}/board/microchip/sam9x75_curiosity/nandflash-usb.qml" > ${BASE_DIR}/flash.sh
chmod +x ${BASE_DIR}/flash.sh
fi
