// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef __DT_BINDINGS_K1_PRO_RST_H__
#define __DT_BINDINGS_K1_PRO_RST_H__

#define RESET_CPUC0C1       0
#define RESET_CPUC0C2       1
#define RESET_CPUC0C3       2
#define RESET_CPUC0TCM      3

#define RESET_CPUC1APB  4
#define RESET_CPUC1L2C  5
#define RESET_CPUC1PIC  6
#define RESET_CPUC1ACEM 7
#define RESET_CPUC1C0   8
#define RESET_CPUC1C1   9
#define RESET_CPUC1C2   10
#define RESET_CPUC1C3   11

#define RESET_CPU_GSR    12
#define RESET_CPU_CCI    13
#define RESET_SERDES2CPU 14

#define RESET_SYS_GSR   15
#define RESET_MBOX      16
#define RESET_SPINLOCK  17
#define RESET_DMAC      18
#define RESET_GPIO      19
#define RESET_WDT       20
#define RESET_PWM       21
#define RESET_PVTC      22
#define RESET_BMU       23
#define RESET_SYSREG    24

#define RESET_QSPI0     25
#define RESET_QSPI1     26
#define RESET_QSPI2     27

#define RESET_UART0     28
#define RESET_UART1     29
#define RESET_UART2     30
#define RESET_UART3     31
#define RESET_UART4     32

#define RESET_I2C0    33
#define RESET_I2C1    34
#define RESET_I2C2    35
#define RESET_I2C3    36
#define RESET_I2C4    37

#define RESET_TIMER   38

#define RESET_CAN     39

#define RESET_I2S0    40
#define RESET_I2S1    41

#define RESET_MEM_GSR  42
#define RESET_SDIO0    43
#define RESET_SDIO1    44
#define RESET_EMMC0    45
#define RESET_EMMC1    46
#define RESET_MEM2MBUS 47

#define RESET_SERDES_GSR  48
#define RESET_SERDES2MBUS 49

#define RESET_PCIE0_POWERUP 50
#define RESET_PCIE0_PERST   51
#define RESET_PCIE1_POWERUP 52
#define RESET_PCIE1_PERST   53
#define RESET_PCIE2_POWERUP 54
#define RESET_PCIE2_PERST   55
#define RESET_PCIE3_POWERUP 56
#define RESET_PCIE3_PERST   57
#define RESET_PCIE4_POWERUP 58
#define RESET_PCIE4_PERST   59
#define RESET_PCIE5_POWERUP 60
#define RESET_PCIE5_PERST   61

#define RESET_SATA        62
#define RESET_SATA_PORT0  63
#define RESET_SATA_PORT1  64
#define RESET_SATA_PORT2  65
#define RESET_SATA_PORT3  66

#define RESET_USB30_VCC   67
#define RESET_USB31_VCC   68

#define RESET_XGMII0      69
#define RESET_XGMII1      70

#define RESET_COMBO_PHY0  71
#define RESET_COMBO_PHY1  72

#define RESET_VPU_GSR     73
#define RESET_VPU         74
#define RESET_VBUS2MBUS   75

#define RESET_SEC_GSR     76
#define RESET_TRNG        77
#define RESET_PKE         78
#define RESET_HASH        79
#define RESET_SKE_CONFIG  80
#define RESET_SKE_DMA     81
#define RESET_SKE_CORE    82
#define RESET_EFUSE       83

#define RESET_USB_GMAC_GSR 84
#define RESET_USB2_AHB     85
#define RESET_USB2_PHY     86
#define RESET_USB31        87
#define RESET_USB2_PHY_POR 88
#define RESET_USB2_PHY_PORT  89
#define RESET_USB3_PHY_PORT  90
#define RESET_GMAC_CSR       91
#define RESET_GMAC_DMA       92
#define RESET_USB_GMAC2VBUS  93

#define RESET_DDR_GSR   94
#define RESET_DDR0      95
#define RESET_DDR1      96
#define RESET_DDR_PHY0  97
#define RESET_DDR_PHY1  98

//mcu
#define RESET_MCU_GSR   99
#define RESET_MCU_CORE  100
#define RESET_MCU_BUS   101
#define RESET_MCU2MBUS  102

#define RESET_NUMBER    103

#endif /* __DT_BINDINGS_K1_PRO_RST_H__ */
