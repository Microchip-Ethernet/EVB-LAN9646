import SAMBA 3.8
import SAMBA.Connection.Serial 3.8
import SAMBA.Device.SAM9X7 3.8

SerialConnection {
	device: SAM9X75CURIOSITY {
		config {
			nandflash {
				header: 0xc2605007
			}
		}
	}

	function getEraseSize(size) {
		/* get smallest erase block size supported by applet */
		var eraseSize
		for (var i = 0; i <= 32; i++) {
			eraseSize = 1 << i
			if ((applet.eraseSupport & eraseSize) !== 0)
				break;
		}
		eraseSize *= applet.pageSize

		/* round up file size to erase block size */
		return (size + eraseSize - 1) & ~(eraseSize - 1)
	}

	function eraseWrite(offset, filename, bootfile, arg) {
		/* get file size */
		var file = File.open(filename, false)
		var size = file.size()
		file.close()

		applet.erase(offset, getEraseSize(size))
		applet.write(offset, filename, bootfile, arg)
	}

	onConnectionOpened: {

		var kernelFileName = "zImage"
		var ubootEnvFileName = "uboot-env.bin"

		print("-I- === Initialize nandflash access ===")
		initializeApplet("nandflash")

		// erase then write files
		print("-I- === Load AT91Bootstrap ===")
		eraseWrite(0x00000000, "at91bootstrap.bin", true, true)

		print("-I- === Load u-boot ===")
		eraseWrite(0x00040000, "u-boot.bin", false, false)

		print("-I- === Load u-boot environment ===")
		eraseWrite(0x00100000, ubootEnvFileName, false, false)
		//erase redundant env to be in a clean and known state
		applet.erase(0x00140000, getEraseSize(0x40000))

		print("-I- === Load Device Tree ===")
		eraseWrite(0x00140000, "at91-sam9x75_curiosity.dtb", false, false)

		print("-I- === Load kernel compressed image ===")
		eraseWrite(0x00200000, kernelFileName, false, false)

		print("-I- === Load root file-system image ===")
		applet.erase(0x00800000, applet.memorySize - 0x00800000)
		applet.write(0x00800000, "rootfs.ubi", false, false)

		print("-I- === Done. ===")
	}
}
