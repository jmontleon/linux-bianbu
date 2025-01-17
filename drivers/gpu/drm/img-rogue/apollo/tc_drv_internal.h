/*
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _TC_DRV_INTERNAL_H
#define _TC_DRV_INTERNAL_H

#include "tc_drv.h"

#include <linux/version.h>

#if defined(TC_FAKE_INTERRUPTS)
#define FAKE_INTERRUPT_TIME_MS 20
#include <linux/timer.h>
#include <linux/time.h>
#endif

#define DRV_NAME "tc"

/* This is a guess of what's a minimum sensible size for the ext heap
 * It is only used for a warning if the ext heap is smaller, and does
 * not affect the functional logic in any way
 */
#define TC_EXT_MINIMUM_MEM_SIZE (10*1024*1024)

#if defined(SUPPORT_DMA_HEAP)
 #if defined(SUPPORT_FAKE_SECURE_DMA_HEAP)
  #define TC_DMA_HEAP_COUNT 3
 #else
  #define TC_DMA_HEAP_COUNT 2
 #endif
#elif defined(SUPPORT_ION)
 #if defined(SUPPORT_RGX) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
  #define TC_ION_HEAP_BASE_COUNT 3
 #else
  #define TC_ION_HEAP_BASE_COUNT 2
 #endif

 #if defined(SUPPORT_FAKE_SECURE_ION_HEAP)
  #define TC_ION_HEAP_COUNT (TC_ION_HEAP_BASE_COUNT + 1)
 #else
  #define TC_ION_HEAP_COUNT TC_ION_HEAP_BASE_COUNT
 #endif
#endif /* defined(SUPPORT_ION) */

/* Convert a byte offset to a 32 bit dword offset */
#define DWORD_OFFSET(byte_offset)  ((byte_offset)>>2)

#define HEX2DEC(v)                 ((((v) >> 4) * 10) + ((v) & 0x0F))

enum tc_version_t {
	TC_INVALID_VERSION,
	APOLLO_VERSION_TCF_2,
	APOLLO_VERSION_TCF_5,
	APOLLO_VERSION_TCF_BONNIE,
	ODIN_VERSION_TCF_BONNIE,
	ODIN_VERSION_FPGA,
	ODIN_VERSION_ORION,
	ODIN_VERSION_VALI,
};

struct tc_interrupt_handler {
	bool enabled;
	void (*handler_function)(void *data);
	void *handler_data;
};

struct tc_region {
	resource_size_t base;
	resource_size_t size;
};

struct tc_io_region {
	struct tc_region region;
	void __iomem *registers;
};

struct tc_device {
	struct pci_dev *pdev;

	enum tc_version_t version;
	bool odin;
	bool vali;
	bool orion;

	int mem_mode;

	struct tc_io_region tcf;
	struct tc_io_region tcf_pll;

	struct tc_region tc_mem;

	struct platform_device *pdp_dev;

	resource_size_t pdp_heap_mem_base;
	resource_size_t pdp_heap_mem_size;

	struct platform_device *ext_dev[RGX_NUM_DRIVERS_SUPPORTED];

	resource_size_t ext_heap_mem_base;
	resource_size_t ext_heap_mem_size;

	struct platform_device *dma_dev;

	struct dma_chan *dma_chans[2];
	unsigned int dma_refcnt[2];
	unsigned int dma_nchan;
	struct mutex dma_mutex;

#if defined(SUPPORT_FAKE_SECURE_ION_HEAP) || \
	defined(SUPPORT_FAKE_SECURE_DMA_HEAP)
	resource_size_t secure_heap_mem_base;
	resource_size_t secure_heap_mem_size;
#endif

	int mtrr;
	spinlock_t interrupt_handler_lock;
	spinlock_t interrupt_enable_lock;

	struct tc_interrupt_handler
		interrupt_handlers[TC_INTERRUPT_COUNT];

#if defined(TC_FAKE_INTERRUPTS)
	struct timer_list timer;
#endif

#if defined(SUPPORT_DMA_HEAP)
	struct dma_heap *dma_heaps[TC_DMA_HEAP_COUNT];
#elif defined(SUPPORT_ION)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0))
	struct ion_device *ion_device;
#endif
	struct ion_heap *ion_heaps[TC_ION_HEAP_COUNT];
	int ion_heap_count;
#endif /* defined(SUPPORT_ION) */

	bool fbc_bypass;

	struct dentry *debugfs_tc_dir;
	struct dentry *debugfs_rogue_name;
};

int tc_mtrr_setup(struct tc_device *tc);
void tc_mtrr_cleanup(struct tc_device *tc);

int tc_is_interface_aligned(u32 eyes, u32 clk_taps, u32 train_ack);

int tc_iopol32_nonzero(u32 mask, void __iomem *addr);

int request_pci_io_addr(struct pci_dev *pdev, u32 index,
	resource_size_t offset, resource_size_t length);
void release_pci_io_addr(struct pci_dev *pdev, u32 index,
	resource_size_t start, resource_size_t length);

int setup_io_region(struct pci_dev *pdev,
	struct tc_io_region *region, u32 index,
	resource_size_t offset,	resource_size_t size);

#if defined(TC_FAKE_INTERRUPTS)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
void tc_irq_fake_wrapper(struct timer_list *t);
#else
void tc_irq_fake_wrapper(unsigned long data);
#endif
#endif /* defined(TC_FAKE_INTERRUPTS) */

#endif /* _TC_DRV_INTERNAL_H */
