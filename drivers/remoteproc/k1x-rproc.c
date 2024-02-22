// SPDX-License-Identifier: GPL-2.0

#include <linux/limits.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_runtime.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/mailbox_client.h>
#include "remoteproc_internal.h"

#define MAX_MEM_BASE	2
#define MAX_MBOX	2

#define K1X_MBOX_VQ0_ID	0
#define K1X_MBOX_VQ1_ID	1

#define BOOTC_MEM_BASE_OFFSET	0
#define SYSCTRL_MEM_BASE_OFFSET	1

#define ESOS_BOOT_ENTRY_REG_OFFSET	0x88
#define ESOS_BOOTUP_REG_OFFSET		0x30
#define ESOS_AON_PER_CLK_RST_CTL_REG	0x2c

#define ESOS_DDR_REGMAP_BASE_REG_OFFSET	0xc0

struct spacemit_mbox {
	const char name[10];
	struct mbox_chan *chan;
	struct mbox_client client;
	struct work_struct vq_work;
	int vq_id;
};

struct spacemit_rproc {
	struct device *dev;
	struct reset_control *core_rst;
	struct clk *core_clk;
	unsigned int ddr_remap_base;
	void __iomem *base[MAX_MEM_BASE];
	struct spacemit_mbox *mb;
	struct workqueue_struct *workqueue;
};

static int spacemit_rproc_mem_alloc(struct rproc *rproc,
				 struct rproc_mem_entry *mem)
{
	void __iomem *va = NULL;

	dev_dbg(&rproc->dev, "map memory: %pa+%zx\n", &mem->dma, mem->len);
	va = ioremap_wc(mem->dma, mem->len);
	if (!va) {
		dev_err(&rproc->dev, "Unable to map memory region: %pa+%zx\n",
			&mem->dma, mem->len);
		return -ENOMEM;
	}

	/* Update memory entry va */
	mem->va = va;

	return 0;
}

static int spacemit_rproc_mem_release(struct rproc *rproc,
				   struct rproc_mem_entry *mem)
{
	dev_dbg(&rproc->dev, "unmap memory: %pa\n", &mem->dma);

	iounmap(mem->va);

	return 0;
}

static int spacemit_rproc_prepare(struct rproc *rproc)
{
	struct spacemit_rproc *priv = rproc->priv;
	struct device *dev = rproc->dev.parent;
	struct device_node *np = dev->of_node;
	struct of_phandle_iterator it;
	struct rproc_mem_entry *mem;
	struct reserved_mem *rmem;
	u32 da;
	int ret, index = 0;

	/* de-assert the audio module */
	reset_control_deassert(priv->core_rst);
	/* enable the power-switch and the clk */
	pm_runtime_get_sync(priv->dev);

	/* Register associated reserved memory regions */
	of_phandle_iterator_init(&it, np, "memory-region", NULL, 0);
	while (of_phandle_iterator_next(&it) == 0) {
		rmem = of_reserved_mem_lookup(it.node);
		if (!rmem) {
			dev_err(&rproc->dev,
				"unable to acquire memory-region\n");
			return -EINVAL;
		}

		if (rmem->base > U64_MAX) {
			dev_err(&rproc->dev,
				"the rmem base is overflow\n");
			return -EINVAL;
		}

		/* find the da */
		ret = of_property_read_u32(it.node, "da_base", &da);
		if (ret) {
			/* no da_base; means that the da = dma */
			da = rmem->base;
		}

		if (strcmp(it.node->name, "vdev0buffer")) {
			mem = rproc_mem_entry_init(dev, NULL,
						   rmem->base,
						   rmem->size, da,
						   spacemit_rproc_mem_alloc,
						   spacemit_rproc_mem_release,
						   it.node->name);
		} else {
			/* Register reserved memory for vdev buffer alloc */
			mem = rproc_of_resm_mem_entry_init(dev, index,
							   rmem->size,
							   rmem->base,
							   it.node->name);
		}

		if (!mem)
			return -ENOMEM;

		rproc_add_carveout(rproc, mem);
		index++;
	}

	return 0;
}

static int spacemit_rproc_start(struct rproc *rproc)
{
	struct spacemit_rproc *priv = rproc->priv;

	/* enable ipc2ap clk & reset--> rcpu side */
	writel(0xff, priv->base[BOOTC_MEM_BASE_OFFSET] + ESOS_AON_PER_CLK_RST_CTL_REG);

	/* set the boot-entry */
	writel(rproc->bootaddr, priv->base[SYSCTRL_MEM_BASE_OFFSET] + ESOS_BOOT_ENTRY_REG_OFFSET);

	/* set ddr map */
	writel(priv->ddr_remap_base, priv->base[SYSCTRL_MEM_BASE_OFFSET] + ESOS_DDR_REGMAP_BASE_REG_OFFSET);

	/* lanching up esos */
	writel(1, priv->base[BOOTC_MEM_BASE_OFFSET] + ESOS_BOOTUP_REG_OFFSET);

	return 0;
}

static int spacemit_rproc_stop(struct rproc *rproc)
{
	struct spacemit_rproc *priv = rproc->priv;

	/* hold the rcpu */
	writel(0, priv->base[BOOTC_MEM_BASE_OFFSET] + ESOS_BOOTUP_REG_OFFSET);

	pm_runtime_put_sync(priv->dev);

	reset_control_assert(priv->core_rst);

	return 0;
}

static int spacemit_rproc_parse_fw(struct rproc *rproc, const struct firmware *fw)
{
	int ret;

	ret = rproc_elf_load_rsc_table(rproc, fw);
	if (ret)
		dev_info(&rproc->dev, "No resource table in elf\n");

	return 0;
}

static u64 spacemit_get_boot_addr(struct rproc *rproc, const struct firmware *fw)
{
	int err;
	unsigned int entry_point;
	struct device *dev = rproc->dev.parent;

	/* get the entry point */
	err = of_property_read_u32(dev->of_node, "esos-entry-point", &entry_point);
	if (err) {
		 dev_err(dev, "failed to get entry point\n");
		 return 0;
	}

	return entry_point;
}

static void spacemit_rproc_kick(struct rproc *rproc, int vqid)
{
	struct spacemit_rproc *ddata = rproc->priv;
	unsigned int i;
	int err;

	if (WARN_ON(vqid >= MAX_MBOX))
		return;

	for (i = 0; i < MAX_MBOX; i++) {
		if (vqid != ddata->mb[i].vq_id)
			continue;
		if (!ddata->mb[i].chan)
			return;
		err = mbox_send_message(ddata->mb[i].chan, "kick");
		if (err < 0)
			dev_err(&rproc->dev, "%s: failed (%s, err:%d)\n",
				__func__, ddata->mb[i].name, err);
		return;
	}
}

static struct rproc_ops spacemit_rproc_ops = {
	.prepare	= spacemit_rproc_prepare,
	.start		= spacemit_rproc_start,
	.stop		= spacemit_rproc_stop,
	.load		= rproc_elf_load_segments,
	.parse_fw	= spacemit_rproc_parse_fw,
	.kick		= spacemit_rproc_kick,
	.find_loaded_rsc_table = rproc_elf_find_loaded_rsc_table,
	.sanity_check	= rproc_elf_sanity_check,
	.get_boot_addr	= spacemit_get_boot_addr,
};

static void k1x_rproc_mb_vq_work(struct work_struct *work)
{
	struct spacemit_mbox *mb = container_of(work, struct spacemit_mbox, vq_work);
	struct rproc *rproc = dev_get_drvdata(mb->client.dev);

	if (rproc_vq_interrupt(rproc, mb->vq_id) == IRQ_NONE)
		dev_dbg(&rproc->dev, "no message found in vq%d\n", mb->vq_id);
}

static void k1x_rproc_mb_callback(struct mbox_client *cl, void *data)
{
	struct rproc *rproc = dev_get_drvdata(cl->dev);
	struct spacemit_mbox *mb = container_of(cl, struct spacemit_mbox, client);
	struct spacemit_rproc *priv = rproc->priv;

	queue_work(priv->workqueue, &mb->vq_work);
}

static struct spacemit_mbox k1x_rpoc_mbox[] = {
	{
		.name = "vq0",
		.vq_id = K1X_MBOX_VQ0_ID,
		.client = {
			.rx_callback = k1x_rproc_mb_callback,
			.tx_block = true,
		},
	},
	{
		.name = "vq1",
		.vq_id = K1X_MBOX_VQ1_ID,
		.client = {
			.rx_callback = k1x_rproc_mb_callback,
			.tx_block = true,
		},
	},
};

static int spacemit_rproc_probe(struct platform_device *pdev)
{
	int ret, i;
	const char *name;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const char *fw_name = "esos.elf";
	struct spacemit_rproc *priv;
	struct mbox_client *cl;
	struct rproc *rproc;

	ret = rproc_of_parse_firmware(dev, 0, &fw_name);
	if (ret < 0 && ret != -EINVAL)
		return ret;

	rproc = devm_rproc_alloc(dev, np->name, &spacemit_rproc_ops,
				fw_name, sizeof(*priv));
	if (!rproc)
		return -ENOMEM;

	priv = rproc->priv;
	priv->dev = dev;

	priv->base[BOOTC_MEM_BASE_OFFSET] = devm_platform_ioremap_resource(pdev, BOOTC_MEM_BASE_OFFSET);
	if (IS_ERR(priv->base[BOOTC_MEM_BASE_OFFSET])) {
		ret = PTR_ERR(priv->base[BOOTC_MEM_BASE_OFFSET]);
		dev_err(dev, "failed to get reg base\n");
		return ret;
	}

	priv->base[SYSCTRL_MEM_BASE_OFFSET] = devm_platform_ioremap_resource(pdev, SYSCTRL_MEM_BASE_OFFSET);
	if (IS_ERR(priv->base[SYSCTRL_MEM_BASE_OFFSET])) {
		ret = PTR_ERR(priv->base[SYSCTRL_MEM_BASE_OFFSET]);
		dev_err(dev, "failed to get reg base\n");
		return ret;
	}

	priv->core_rst = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(priv->core_rst)) {
		ret = PTR_ERR(priv->core_rst);
		dev_err_probe(dev, ret, "fail to acquire rproc reset\n");
		return ret;
	}

	priv->core_clk = devm_clk_get(dev, "core");
	if (IS_ERR(priv->core_clk)) {
		ret = PTR_ERR(priv->core_clk);
		dev_err(dev, "failed to acquire rpoc core\n");
		return ret;
	}

	/* get the ddr-remap base */
	ret = of_property_read_u32(pdev->dev.of_node, "ddr-remap-base", &priv->ddr_remap_base);

	pm_runtime_enable(dev);

	platform_set_drvdata(pdev, rproc);

	priv->workqueue = create_workqueue(dev_name(dev));
	if (!priv->workqueue) {
		dev_err(dev, "cannot create workqueue\n");
		return -EINVAL;
	}

	/* get the mailbox */
	priv->mb = k1x_rpoc_mbox;

	for (i = 0; i < MAX_MBOX; ++i) {
		name = priv->mb[i].name;

		cl = &priv->mb[i].client;
		cl->dev = dev;
		priv->mb[i].chan = mbox_request_channel_byname(cl, name);
		if (IS_ERR(priv->mb[i].chan)) {
			dev_err(dev, "failed to request mbox channel\n");
			return -EINVAL;
		}

		if (priv->mb[i].vq_id >= 0) {
			INIT_WORK(&priv->mb[i].vq_work, k1x_rproc_mb_vq_work);
		}
	}

	rproc->auto_boot = true;
	ret = devm_rproc_add(dev, rproc);
	if (ret) {
		dev_err(dev, "rproc_add failed\n");
	}

	return ret;
}

static void k1x_rproc_free_mbox(struct rproc *rproc)
{
	struct spacemit_rproc *ddata = rproc->priv;
	unsigned int i;

	for (i = 0; i < MAX_MBOX; i++) {
		if (ddata->mb[i].chan)
			mbox_free_channel(ddata->mb[i].chan);
		ddata->mb[i].chan = NULL;
	}
}

static int spacemit_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct spacemit_rproc *ddata = rproc->priv;

	rproc_del(rproc);
	k1x_rproc_free_mbox(rproc);
	destroy_workqueue(ddata->workqueue);
	rproc_free(rproc);

	return 0;
}

static const struct of_device_id spacemit_rproc_of_match[] = {
	{ .compatible = "spacemit,k1-x-rproc" },
	{},
};

MODULE_DEVICE_TABLE(of, spacemit_rproc_of_match);

static struct platform_driver spacemit_rproc_driver = {
	.probe = spacemit_rproc_probe,
	.remove = spacemit_rproc_remove,
	.driver = {
		.name = "spacemit-rproc",
		.of_match_table = spacemit_rproc_of_match,
	},
};

module_platform_driver(spacemit_rproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("sapcemit remote processor control driver");

