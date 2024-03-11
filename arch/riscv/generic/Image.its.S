/* SPDX-License-Identifier: GPL-2.0 */
/dts-v1/;

/ {
	description = KERNEL_NAME;
	#address-cells = <2>;

	images {
		kernel {
			description = KERNEL_NAME;
			data = /incbin/(IMAGE_BINARY);
			type = "kernel";
			arch = "riscv";
			os = "linux";
			compression = IMAGE_COMPRESSION;
			load = <0x0 0x1400000>;
			entry = <0x0 0x1400000>;
			hash {
				algo = IMAGE_CHECK_ALGORITHM;
			};
		};
	};

	configurations {
		default = "conf-default";

		conf-default {
			description = "Generic Linux kernel";
			kernel = "kernel";
		};
	};
};
