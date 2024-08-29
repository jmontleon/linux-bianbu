# SPDX-License-Identifier: GPL-2.0

spacemit-drm-y := 	spacemit_drm.o \
				spacemit_cmdlist.o \
				spacemit_dpu.o \
				spacemit_planes.o \
				spacemit_dsi.o \
				spacemit_wb.o \
				spacemit_dphy.o \
				spacemit_lib.o \
				spacemit_gem.o \
				spacemit_dmmu.o \
				spacemit_bootloader.o \
				sysfs/sysfs_class.o \
				sysfs/sysfs_dpu.o \
				sysfs/sysfs_dsi.o \
				sysfs/sysfs_dphy.o \
				sysfs/sysfs_mipi_panel.o \
				dpu/dpu_debug.o \
				dpu/dpu_saturn.o \
				dpu/saturn_fbcmem.o \
				dsi/spacemit_dsi_drv.o \
				dsi/spacemit_dptc_drv.o \
				dphy/spacemit_dphy_drv.o


obj-$(CONFIG_DRM_SPACEMIT) += spacemit-drm.o

obj-$(CONFIG_SPACEMIT_MIPI_PANEL) += spacemit_mipi_panel.o
obj-$(CONFIG_SPACEMIT_HDMI) += spacemit_hdmi.o
obj-$(CONFIG_DRM_LT8911EXB) += lt8911exb.o
obj-$(CONFIG_DRM_LT9711) += lt9711.o
