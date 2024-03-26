import SAMBA 3.7
import SAMBA.Connection.Serial 3.7
import SAMBA.Device.SAM9X7 3.7

SerialConnection {
	device: SAM9X7 {
		config {
			nandflash {
				ioset: 1
				busWidth: 8
				header: 0xc0914da5
			}
		}
	}

	onConnectionOpened: {

		// initialize NAND flash applet
		initializeApplet("nandflash")

		// erase all memory
		applet.erase(0, applet.memorySize)

		// write files
		applet.write(0x000000, "at91bootstrap.bin", true)
		applet.write(0x040000, "u-boot.bin")
		applet.write(0x100000, "uboot-env.bin")
		applet.write(0x140000, "at91-sam9x75_curiosity_dev.dtb")
		applet.write(0x180000, "zImage")
		applet.write(0x780000, "rootfs.ubi")
	}
}
