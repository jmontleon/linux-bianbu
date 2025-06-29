ifeq ($(CONFIG_PLATFORM_SPACEMIT), y)
ccflags-y += -DCONFIG_LITTLE_ENDIAN
ccflags-y += -DCONFIG_PLATFORM_SPACEMIT
ccflags-y += -DCONFIG_IOCTL_CFG80211 -DRTW_USE_CFG80211_STA_EVENT
ccflags-y += -DCONFIG_RADIO_WORK
#ccflags-y += -DCONFIG_CONCURRENT_MODE
ifeq ($(shell test $(CONFIG_RTW_ANDROID) -ge 11; echo $$?), 0)
ccflags-y += -DCONFIG_IFACE_NUMBER=2
#ccflags-y += -DCONFIG_SEL_P2P_IFACE=1
endif

ARCH := riscv
#CROSS_COMPILE=/home/wanlong/workspace/k1x/output/toolchain/bin/riscv64-unknown-linux-gnu-
#KSRC=/home/wanlong/workspace/k1x/linux-6.1

ifeq ($(CONFIG_SDIO_HCI), y)
_PLATFORM_FILES = platform/platform_spacemit_sdio.o
endif
endif
