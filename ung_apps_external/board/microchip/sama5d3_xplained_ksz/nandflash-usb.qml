import SAMBA 3.8
import SAMBA.Connection.Serial 3.8
import SAMBA.Device.SAMA5D3 3.8

SerialConnection {
	//port: "ttyACM0"
	//port: "COM85"
	//baudRate: 57600

	device: SAMA5D3Xplained {
		// to use a custom config, replace SAMA5D2Xplained by SAMA5D2 and
		// uncomment the following lines, or see documentation for
		// custom board creation.
		//config {
		//	nandflash {
		//		ioset: 1
		//		busWidth: 8
		//		header: 0xc0902405
		//	}
		//}
	}

	onConnectionOpened: {
		// initialize Low-Level applet
		initializeApplet("lowlevel")

		// initialize NAND flash applet
		initializeApplet("nandflash")

		// erase all memory
		applet.erase(0, applet.memorySize)

		// write files
		applet.write(0x000000, "at91bootstrap.bin", true, true)
		applet.write(0x040000, "u-boot.bin")
		applet.write(0x100000, "uboot-env.bin")
		applet.write(0x180000, "at91-sama5d3_xplained.dtb")
		applet.write(0x200000, "zImage")
		applet.write(0x800000, "rootfs.ubi")
	}
}
