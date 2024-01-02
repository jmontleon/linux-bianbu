# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y += -I$(srctree)/drivers/media/platform/spacemit/camera/cam_util
subdir-ccflags-y += -I$(srctree)/drivers/media/platform/spacemit/camera/cam_plat
# subdir-ccflags-y += -DCONFIG_ARCH_ZYNQMP
subdir-ccflags-y += -DCONFIG_ARCH_SPACEMIT

obj-$(CONFIG_SPACEMIT_K1X_CAMERA_V2) += cam_plat_v2.o
cam_plat_v2-objs = cam_plat/cam_plat.o cam_util/cam_dbg.o

obj-$(CONFIG_SPACEMIT_K1X_CCIC_V2) += cam_ccic_v2.o
cam_ccic_v2-objs += cam_ccic/ccic_hwreg.o
cam_ccic_v2-objs += cam_ccic/csiphy.o
cam_ccic_v2-objs += cam_ccic/ccic_drv.o
cam_ccic_v2-objs += cam_ccic/dptc_drv.o

obj-$(CONFIG_SPACEMIT_K1X_CPP_V2) += cam_cpp_v2.o
cam_cpp_v2-objs += cam_cpp/k1x_cpp.o
#cam_cpp_v2-objs += cam_cpp/cpp_compat_ioctl32.o
cam_cpp_v2-objs += cam_cpp/cpp-v2p0.o
cam_cpp_v2-objs += cam_cpp/cpp_dmabuf.o
cam_cpp_v2-objs += cam_cpp/cpp_iommu.o

obj-$(CONFIG_SPACEMIT_K1X_SENSOR_V2) += cam_sensor_v2.o
cam_sensor_v2-objs += cam_sensor/cam_sensor.o

obj-$(CONFIG_SPACEMIT_K1X_ISP_V2) += cam_isp_v2.o
cam_isp_v2-objs += cam_isp/k1x_isp_drv.o cam_isp/k1x_isp_reg.o cam_isp/k1x_isp_statistic.o cam_isp/k1x_isp_pipe.o

obj-$(CONFIG_SPACEMIT_K1X_VI_V2) += cam_vi_v2.o
cam_vi_v2-objs += vi/mlink.o vi/cam_block.o vi/vdev.o vi/subdev.o vi/vsensor.o
cam_vi_v2-objs += vi/k1xvi/hw-seq/hw_dma.o vi/k1xvi/hw-seq/hw_isp.o vi/k1xvi/hw-seq/hw_postpipe.o vi/k1xvi/hw-seq/hw_iommu.o
cam_vi_v2-objs += vi/k1xvi/fe_isp.o vi/k1xvi/k1xvi.o
#
#obj-$(CONFIG_LEDS_AW36515) += leds-aw36515.o
#leds-aw36515-objs += flash/leds-aw36515.o
