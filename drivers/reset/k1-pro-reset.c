// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Device Tree support for Spacemit SoCs
 *
 * Copyright (c) 2023 Spacemit Inc.
 */
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/io.h>
#include <dt-bindings/reset/k1-pro-reset.h>
#include <linux/clk-provider.h>

#define LOG_INFO(fmt, arg...)    pr_info("[K1-RESET][%s][%d]:" fmt "\n", __func__, __LINE__, ##arg)

#define CPU0_SW_RESET000   0x000
#define CPU1_SW_RESET004   0x004
#define CPUSUB_SW_RESET008 0x008
#define SYS_SW_RESET100    0x100
#define QSPI_SW_RESET104   0x104
#define UART_SW_RESET108   0x108
#define I2C_SW_RESET10C    0x10C
#define TIMER_SW_RESET110  0x110
#define CAN_SW_RESET114    0x114
#define I2S_SW_RESET118    0x118
#define MEM_SW_RESET200    0x200
#define SERDES_SW_RESET300 0x300
#define PCIE_SW_RESET304   0x304
#define SATA_SW_RESET308   0x308
#define USB_SW_RESET30C    0x30C
#define XGMII_SW_RESET310  0x310
#define PHY_SW_RESET314    0x314
#define VPU_SW_RESET400    0x400
#define SEC_SW_RESET500    0x500
#define USB_GMAC_SW_RESET600  0x600
#define DDR_MBUS_SW_RESET700  0x700
//mcu regs
#define MCU_SW_RESET008  0x008


struct k1pro_reset_signal {
    u32 offset;
    u32 bit;
};

struct k1pro_reset_variant {
    const struct k1pro_reset_signal *signals;
    u32 signals_num;
    struct reset_control_ops ops;
};

struct k1pro_reset {
    struct reset_controller_dev rcdev;
    void __iomem *reg_base;
    void __iomem *mcu_reg_base;
    const struct k1pro_reset_signal *signals;
};

struct k1pro_reset k1pro_reset_controller;

static const struct k1pro_reset_signal
    k1pro_reset_signals[RESET_NUMBER] = {
    [RESET_CPUC0C1]     = { CPU0_SW_RESET000, BIT(5) },
    [RESET_CPUC0C2]     = { CPU0_SW_RESET000, BIT(6) },
    [RESET_CPUC0C3]     = { CPU0_SW_RESET000, BIT(7) },
    [RESET_CPUC0TCM]    = { CPU0_SW_RESET000, BIT(8) },
    [RESET_CPUC1APB]    = { CPU1_SW_RESET004, BIT(0) },
    [RESET_CPUC1L2C]    = { CPU1_SW_RESET004, BIT(1) },
    [RESET_CPUC1PIC]    = { CPU1_SW_RESET004, BIT(2) },
    [RESET_CPUC1ACEM]   = { CPU1_SW_RESET004, BIT(3) },
    [RESET_CPUC1C0]     = { CPU1_SW_RESET004, BIT(4) },
    [RESET_CPUC1C1]     = { CPU1_SW_RESET004, BIT(5) },
    [RESET_CPUC1C2]     = { CPU1_SW_RESET004, BIT(6) },
    [RESET_CPUC1C3]     = { CPU1_SW_RESET004, BIT(7) },
    [RESET_CPU_GSR]     = { CPUSUB_SW_RESET008, BIT(0) },
    [RESET_CPU_CCI]     = { CPUSUB_SW_RESET008, BIT(1) },
    [RESET_SERDES2CPU]  = { CPUSUB_SW_RESET008, BIT(2) },
    [RESET_SYS_GSR]     = { SYS_SW_RESET100, BIT(0) },
    [RESET_MBOX]        = { SYS_SW_RESET100, BIT(1) },
    [RESET_SPINLOCK]    = { SYS_SW_RESET100, BIT(2) },
    [RESET_DMAC]        = { SYS_SW_RESET100, BIT(3) },
    [RESET_GPIO]        = { SYS_SW_RESET100, BIT(4) },
    [RESET_WDT]         = { SYS_SW_RESET100, BIT(5) },
    [RESET_PWM]         = { SYS_SW_RESET100, BIT(6) },
    [RESET_PVTC]        = { SYS_SW_RESET100, BIT(7) },
    [RESET_BMU]         = { SYS_SW_RESET100, BIT(8) },
    [RESET_SYSREG]      = { SYS_SW_RESET100, BIT(9) },
    [RESET_QSPI0]       = { QSPI_SW_RESET104, BIT(0) },
    [RESET_QSPI1]       = { QSPI_SW_RESET104, BIT(1) },
    [RESET_QSPI2]       = { QSPI_SW_RESET104, BIT(2) },
    [RESET_UART0]       = { UART_SW_RESET108, BIT(0) },
    [RESET_UART1]       = { UART_SW_RESET108, BIT(1) },
    [RESET_UART2]       = { UART_SW_RESET108, BIT(2) },
    [RESET_UART3]       = { UART_SW_RESET108, BIT(3) },
    [RESET_UART4]       = { UART_SW_RESET108, BIT(4) },
    [RESET_I2C0]        = { I2C_SW_RESET10C, BIT(0) },
    [RESET_I2C1]        = { I2C_SW_RESET10C, BIT(1) },
    [RESET_I2C2]        = { I2C_SW_RESET10C, BIT(2) },
    [RESET_I2C3]        = { I2C_SW_RESET10C, BIT(3) },
    [RESET_I2C4]        = { I2C_SW_RESET10C, BIT(4) },
    [RESET_TIMER]       = { TIMER_SW_RESET110, BIT(0) },
    [RESET_CAN]         = { CAN_SW_RESET114, BIT(0) },
    [RESET_I2S0]        = { I2S_SW_RESET118, BIT(0) },
    [RESET_I2S1]        = { I2S_SW_RESET118, BIT(1) },
    [RESET_MEM_GSR]     = { MEM_SW_RESET200, BIT(0) },
    [RESET_SDIO0]       = { MEM_SW_RESET200, BIT(1) },
    [RESET_SDIO1]       = { MEM_SW_RESET200, BIT(2) },
    [RESET_EMMC0]       = { MEM_SW_RESET200, BIT(3) },
    [RESET_EMMC1]       = { MEM_SW_RESET200, BIT(4) },
    [RESET_MEM2MBUS]    = { MEM_SW_RESET200, BIT(5) },
    [RESET_SERDES_GSR]  = { SERDES_SW_RESET300, BIT(0) },
    [RESET_SERDES2MBUS] = { SERDES_SW_RESET300, BIT(1) },
    [RESET_PCIE0_POWERUP]   = { PCIE_SW_RESET304, BIT(0) },
    [RESET_PCIE0_PERST]     = { PCIE_SW_RESET304, BIT(1) },
    [RESET_PCIE1_POWERUP]   = { PCIE_SW_RESET304, BIT(2) },
    [RESET_PCIE1_PERST]     = { PCIE_SW_RESET304, BIT(3) },
    [RESET_PCIE2_POWERUP]   = { PCIE_SW_RESET304, BIT(4) },
    [RESET_PCIE2_PERST]     = { PCIE_SW_RESET304, BIT(5) },
    [RESET_PCIE3_POWERUP]   = { PCIE_SW_RESET304, BIT(6) },
    [RESET_PCIE3_PERST]     = { PCIE_SW_RESET304, BIT(7) },
    [RESET_PCIE4_POWERUP]   = { PCIE_SW_RESET304, BIT(8) },
    [RESET_PCIE4_PERST]     = { PCIE_SW_RESET304, BIT(9) },
    [RESET_PCIE5_POWERUP]   = { PCIE_SW_RESET304, BIT(10) },
    [RESET_PCIE5_PERST]     = { PCIE_SW_RESET304, BIT(11) },
    [RESET_SATA]        = { SATA_SW_RESET308, BIT(0) },
    [RESET_SATA_PORT0]  = { SATA_SW_RESET308, BIT(1) },
    [RESET_SATA_PORT1]  = { SATA_SW_RESET308, BIT(2) },
    [RESET_SATA_PORT2]  = { SATA_SW_RESET308, BIT(3) },
    [RESET_SATA_PORT3]  = { SATA_SW_RESET308, BIT(4) },
    [RESET_USB30_VCC]   = { USB_SW_RESET30C, BIT(0) },
    [RESET_USB31_VCC]   = { USB_SW_RESET30C, BIT(1) },
    [RESET_XGMII0]      = { XGMII_SW_RESET310, BIT(0) },
    [RESET_XGMII1]      = { XGMII_SW_RESET310, BIT(1) },
    [RESET_COMBO_PHY0]  = { PHY_SW_RESET314, BIT(0) },
    [RESET_COMBO_PHY1]  = { PHY_SW_RESET314, BIT(1) },
    [RESET_VPU_GSR]     = { VPU_SW_RESET400, BIT(0) },
    [RESET_VPU]         = { VPU_SW_RESET400, BIT(1) },
    [RESET_VBUS2MBUS]   = { VPU_SW_RESET400, BIT(2) },
    [RESET_SEC_GSR]     = { SEC_SW_RESET500, BIT(0) },
    [RESET_TRNG]        = { SEC_SW_RESET500, BIT(1) },
    [RESET_PKE]         = { SEC_SW_RESET500, BIT(2) },
    [RESET_HASH]        = { SEC_SW_RESET500, BIT(3) },
    [RESET_SKE_CONFIG]  = { SEC_SW_RESET500, BIT(4) },
    [RESET_SKE_DMA]     = { SEC_SW_RESET500, BIT(5) },
    [RESET_SKE_CORE]    = { SEC_SW_RESET500, BIT(6) },
    [RESET_EFUSE]       = { SEC_SW_RESET500, BIT(7) },
    [RESET_USB_GMAC_GSR]  = { USB_GMAC_SW_RESET600, BIT(0) },
    [RESET_USB2_AHB]      = { USB_GMAC_SW_RESET600, BIT(1) },
    [RESET_USB2_PHY]      = { USB_GMAC_SW_RESET600, BIT(2) },
    [RESET_USB31]         = { USB_GMAC_SW_RESET600, BIT(3) },
    [RESET_USB2_PHY_POR]  = { USB_GMAC_SW_RESET600, BIT(4) },
    [RESET_USB2_PHY_PORT] = { USB_GMAC_SW_RESET600, BIT(5) },
    [RESET_USB3_PHY_PORT] = { USB_GMAC_SW_RESET600, BIT(6) },
    [RESET_GMAC_CSR]      = { USB_GMAC_SW_RESET600, BIT(7) },
    [RESET_GMAC_DMA]      = { USB_GMAC_SW_RESET600, BIT(8) },
    [RESET_USB_GMAC2VBUS] = { USB_GMAC_SW_RESET600, BIT(9) },
    [RESET_DDR_GSR]       = { DDR_MBUS_SW_RESET700, BIT(0) },
    [RESET_DDR0]          = { DDR_MBUS_SW_RESET700, BIT(1) },
    [RESET_DDR1]          = { DDR_MBUS_SW_RESET700, BIT(2) },
    [RESET_DDR_PHY0]      = { DDR_MBUS_SW_RESET700, BIT(3) },
    [RESET_DDR_PHY1]      = { DDR_MBUS_SW_RESET700, BIT(4) },
    //mcu
    [RESET_MCU_GSR]       = { MCU_SW_RESET008, BIT(0) },
    [RESET_MCU_CORE]      = { MCU_SW_RESET008, BIT(1) },
    [RESET_MCU_BUS]       = { MCU_SW_RESET008, BIT(2) },
    [RESET_MCU2MBUS]      = { MCU_SW_RESET008, BIT(12) },
};

static struct k1pro_reset *to_k1pro_reset(
    struct reset_controller_dev *rcdev)
{
    return container_of(rcdev, struct k1pro_reset, rcdev);
}

static void k1pro_reset_set(struct reset_controller_dev *rcdev,
    u32 id, bool assert)
{
    unsigned int value;
    struct k1pro_reset *reset = to_k1pro_reset(rcdev);
    LOG_INFO("assert = %d, id = %d", assert, id);

    if(id >= RESET_MCU_GSR){
        value = readl(reset->mcu_reg_base + reset->signals[id].offset);
    } else {
        value = readl(reset->reg_base + reset->signals[id].offset);
    }

    if(assert == true) {
        value &= ~reset->signals[id].bit;
    } else {
        value |= reset->signals[id].bit;
    }

    if(id >= RESET_MCU_GSR){
        writel(value, reset->mcu_reg_base + reset->signals[id].offset);
    } else {
        writel(value, reset->reg_base + reset->signals[id].offset);
    }
}

static int k1pro_reset_assert(struct reset_controller_dev *rcdev,
    unsigned long id)
{
    k1pro_reset_set(rcdev, id, true);
    return 0;
}

static int k1pro_reset_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
    k1pro_reset_set(rcdev, id, false);
    return 0;
}

static const struct k1pro_reset_variant k1pro_reset_data = {
    .signals = k1pro_reset_signals,
    .signals_num = ARRAY_SIZE(k1pro_reset_signals),
    .ops = {
        .assert   = k1pro_reset_assert,
        .deassert = k1pro_reset_deassert,
    },
};

static void k1pro_reset_init(struct device_node *np)
{
    struct k1pro_reset *reset = &k1pro_reset_controller;
    LOG_INFO("init reset");
    reset->reg_base = of_iomap(np, 0);
    reset->mcu_reg_base = of_iomap(np, 1);
    if (!(reset->reg_base &&  reset->mcu_reg_base)) {
        pr_err("%s: could not map reset region\n", __func__);
        return;
    }
    reset->signals = k1pro_reset_data.signals;
    reset->rcdev.owner     = THIS_MODULE;
    reset->rcdev.nr_resets = k1pro_reset_data.signals_num;
    reset->rcdev.ops       = &k1pro_reset_data.ops;
    reset->rcdev.of_node   = np;
    LOG_INFO("register");
    reset_controller_register(&reset->rcdev);
}

CLK_OF_DECLARE(k1_reset, "spacemit,k1pro-reset", k1pro_reset_init);

