#!/bin/sh

DTB=$(grep BR2_LINUX_KERNEL_INTREE_DTS_NAME $BR2_CONFIG | cut -d"\"" -f2 | cut -d"/" -f2)

if [ "$DTB" != "at91-sam9x75_curiosity" ]; then
    cp -p ${BINARIES_DIR}/$DTB.dtb ${BINARIES_DIR}/at91-sam9x75_curiosity.dtb
fi

# Not building for nandflash
if [ ! -e ${BINARIES_DIR}/rootfs.ubi ]; then
    ENV_IN=${BR2_EXTERNAL_KSZ_PATH}/u-boot-mmc.txt
    ENV_OUT=${BINARIES_DIR}/uboot.env
    ENV_SIZE=0x4000
    ENV_REDUN=
else
    ENV_IN=${BR2_EXTERNAL_KSZ_PATH}/u-boot-nand.txt
    ENV_OUT=${BINARIES_DIR}/uboot-env.bin
    ENV_SIZE=0x20000
    ENV_REDUN=-r
fi

if [ ! -e ${BINARIES_DIR}/rootfs.ubi ]; then
    if [ ! -e ${ENV_OUT} ]; then
        cp -p ${BINARIES_DIR}/uboot-env.bin ${ENV_OUT}
    fi
    BOARD_DIR="$(dirname $0)"
    ./support/scripts/genimage.sh -c ${BOARD_DIR}/genimage.cfg
else
echo "./host/bin/sam-ba -w images -x ${BR2_EXTERNAL_KSZ_PATH}/board/microchip/sam9x75_curiosity/nandflash-dev.qml" > ${BASE_DIR}/flash.sh
chmod +x ${BASE_DIR}/flash.sh
fi
