// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Spacemit Co., Ltd.
 *
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_of.h>
#include <linux/component.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/kernel.h>

#include "spacemit_drm.h"
#include "spacemit_dmmu.h"
#include "spacemit_gem.h"

#define DRIVER_NAME	"spacemit"
#define DRIVER_DESC	"Spacemit SoCs' DRM Driver"
#define DRIVER_DATE	"20231115"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

void spacemit_drm_atomic_commit_tail(struct drm_atomic_state *old_state)
{
	struct drm_device *dev = old_state->dev;

	drm_atomic_helper_commit_modeset_disables(dev, old_state);

	drm_atomic_helper_commit_modeset_enables(dev, old_state);

	drm_atomic_helper_commit_planes(dev, old_state,
					DRM_PLANE_COMMIT_ACTIVE_ONLY);

	spacemit_wb_atomic_commit(dev, old_state);

	drm_atomic_helper_wait_for_flip_done(dev, old_state);

	drm_atomic_helper_commit_hw_done(old_state);

	drm_atomic_helper_cleanup_planes(dev, old_state);
}

static const struct drm_mode_config_helper_funcs spacemit_drm_mode_config_helper = {
	.atomic_commit_tail = spacemit_drm_atomic_commit_tail,
};

static const struct drm_mode_config_funcs spacemit_drm_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static void spacemit_drm_mode_config_init(struct drm_device *drm)
{
	drm_mode_config_init(drm);

	/*HW has no limitation of min width and min height,
	  even for YUV format, which is limitated in plane check*/
	drm->mode_config.min_width = 1;
	drm->mode_config.min_height = 1;
	drm->mode_config.max_width = 4096;
	drm->mode_config.max_height = 4096;
	//drm->mode_config.allow_fb_modifiers = true;

	drm->mode_config.funcs = &spacemit_drm_mode_config_funcs;
	drm->mode_config.helper_private = &spacemit_drm_mode_config_helper;
}

static const struct file_operations spacemit_drm_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release		= drm_release,
	.unlocked_ioctl	= drm_ioctl,
	.compat_ioctl	= drm_compat_ioctl,
	.poll			= drm_poll,
	.read			= drm_read,
	.llseek		= no_llseek,
	.mmap		= spacemit_gem_mmap,
};

const struct vm_operations_struct spacemit_gem_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static struct drm_driver spacemit_drm_drv = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET |
					DRIVER_ATOMIC | DRIVER_HAVE_IRQ,
	.fops = &spacemit_drm_fops,

	.dumb_create = spacemit_gem_dumb_create,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.gem_prime_import_sg_table = spacemit_gem_prime_import_sg_table,
	.gem_prime_mmap = drm_gem_prime_mmap,

	.name		= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.date			= DRIVER_DATE,
	.major		= DRIVER_MAJOR,
	.minor		= DRIVER_MINOR,
};

static int spacemit_drm_bind(struct device *dev)
{
	struct drm_device *drm;
	struct spacemit_drm_private *priv;
	int err;

	DRM_INFO("%s()\n", __func__);

	drm = drm_dev_alloc(&spacemit_drm_drv, dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	priv = dev_get_drvdata(dev);
	priv->ddev = drm;
	drm->dev_private = priv;
	priv->dev = dev;

	spacemit_drm_mode_config_init(drm);

	/* bind and init sub drivers */
	err = component_bind_all(drm->dev, drm);
	if (err) {
		DRM_ERROR("failed to bind all component.\n");
		goto err_dc_cleanup;
	}

	/* vblank init */
	err = drm_vblank_init(drm, drm->mode_config.num_crtc);
	if (err) {
		DRM_ERROR("failed to initialize vblank.\n");
		goto err_unbind_all;
	}
	// /* with irq_enabled = true, we can use the vblank feature. */
	// drm->irq_enabled = true;

	/* reset all the states of crtc/plane/encoder/connector */
	drm_mode_config_reset(drm);

	/* init kms poll for handling hpd */
	drm_kms_helper_poll_init(drm);

	/* force detection after connectors init */
	drm_helper_hpd_irq_event(drm);

	err = drm_dev_register(drm, 0);
	if (err < 0)
		goto err_kms_helper_poll_fini;

	return 0;

err_kms_helper_poll_fini:
	drm_kms_helper_poll_fini(drm);
err_unbind_all:
	component_unbind_all(drm->dev, drm);
err_dc_cleanup:
	drm_mode_config_cleanup(drm);
	drm_dev_put(drm);
	return err;
}

static void spacemit_drm_put_dev(struct drm_device *dev)
{
	DRM_DEBUG("\n");

	if (!dev) {
		DRM_ERROR("cleanup called no dev\n");
		return;
	}

	drm_dev_unregister(dev);
	drm_dev_put(dev);
}

static void spacemit_drm_unbind(struct device *dev)
{
	DRM_INFO("%s()\n", __func__);
	spacemit_drm_put_dev(dev_get_drvdata(dev));
}

static const struct component_master_ops drm_component_ops = {
	.bind = spacemit_drm_bind,
	.unbind = spacemit_drm_unbind,
};

static int compare_of(struct device *dev, void *data)
{
	struct device_node *np = data;

	DRM_DEBUG("compare %s\n", np->full_name);

	return dev->of_node == np;
}

int spacemit_drm_of_component_probe(struct device *dev,
			   int (*compare_of)(struct device *, void *),
			   const struct component_master_ops *m_ops)
{
	struct device_node *ep, *port, *remote;
	struct component_match *match = NULL;
	int i;

	if (!dev->of_node)
		return -EINVAL;

	/*
	 * Bind the crtc's ports first, so that drm_of_find_possible_crtcs()
	 * called from encoder's .bind callbacks works as expected
	 */
	for (i = 0; ; i++) {
		port = of_parse_phandle(dev->of_node, "ports", i);
		if (!port)
			break;

		if (of_device_is_available(port->parent))
			drm_of_component_match_add(dev, &match, compare_of,
						   port);

		of_node_put(port);
	}

	if (i == 0) {
		dev_err(dev, "missing 'ports' property\n");
		return -ENODEV;
	}

	if (!match) {
		dev_err(dev, "no available port\n");
		return -ENODEV;
	}

	/*
	 * For bound crtcs, bind the encoders attached to their remote endpoint
	 */
	for (i = 0; ; i++) {
		port = of_parse_phandle(dev->of_node, "ports", i);
		if (!port)
			break;

		if (!of_device_is_available(port->parent)) {
			of_node_put(port);
			continue;
		}

		for_each_child_of_node(port, ep) {
			remote = of_graph_get_remote_port_parent(ep);
			if (!remote || !of_device_is_available(remote)) {
				of_node_put(remote);
				continue;
			} else if (!of_device_is_available(remote->parent)) {
				dev_warn(dev, "parent device of %pOF is not available\n",
					 remote);
				of_node_put(remote);
				continue;
			}

			drm_of_component_match_add(dev, &match, compare_of,
						   remote);
			of_node_put(remote);
		}
		of_node_put(port);
	}

	return component_master_add_with_match(dev, m_ops, match);
}

static int spacemit_drm_probe(struct platform_device *pdev)
{
	int ret;
	struct spacemit_drm_private *priv;
	struct device_node *np = pdev->dev.of_node;
	struct resource *r;

	DRM_ERROR("## spacemit_drm_probe\n");

	ret = dma_coerce_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		DRM_ERROR("dma_set_mask_and_coherent failed (%d)\n", ret);

	/* Allocate and initialize the driver private structure. */
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, priv);

	// if (of_property_read_u32(np, "hw_ver", &priv->hw_ver))
	// 	return -EINVAL;

	priv->contig_mem = false;

	priv->num_pipes = of_count_phandle_with_args(np, "ports", NULL);
	if (priv->num_pipes <= 0) {
		DRM_ERROR("no ports are defined\n");
		return -EINVAL;
	}

	priv->hwdev = (struct spacemit_hw_device *)of_device_get_match_data(&pdev->dev);

	priv->cmdlist_groups = kzalloc(sizeof(struct cmdlist *) * \
			       priv->hwdev->rdma_nums, GFP_KERNEL);
	if (!priv->cmdlist_groups)
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->hwdev->phy_addr = r->start;
	priv->hwdev->base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(priv->hwdev->base))
		return PTR_ERR(priv->hwdev->base);

	return spacemit_drm_of_component_probe(&pdev->dev, compare_of,  &drm_component_ops);
}

static int spacemit_drm_remove(struct platform_device *pdev)
{
	DRM_ERROR("## spacemit_drm_remove\n");
	component_master_del(&pdev->dev, &drm_component_ops);
	return 0;
}

static void spacemit_drm_shutdown(struct platform_device *pdev)
{
	struct spacemit_drm_private *priv = dev_get_drvdata(&pdev->dev);
	struct drm_device *drm = priv->ddev;

	DRM_ERROR("## spacemit_drm_shutdown\n");

	if (!drm) {
		DRM_WARN("drm device is not available, no shutdown\n");
		return;
	}

	drm_atomic_helper_shutdown(drm);
}

static const struct of_device_id drm_match_table[] = {
	{
		.compatible = "spacemit,saturn",
		.data = &spacemit_dp_devices[SATURN],
	},
	{
		.compatible = "spacemit,saturn-le",
		.data = &spacemit_dp_devices[SATURN_LE],
	},
	{},

};
MODULE_DEVICE_TABLE(of, drm_match_table);

static struct platform_driver spacemit_drm_driver = {
	.probe = spacemit_drm_probe,
	.remove = spacemit_drm_remove,
	.shutdown = spacemit_drm_shutdown,
	.driver = {
		.name = "spacemit-drm-drv",
		.of_match_table = drm_match_table,
	},
};

static struct platform_driver * const spacemit_drm_drivers[] = {
	&spacemit_drm_driver,
	&spacemit_dpu_driver,
	&spacemit_dsi_driver,
	&spacemit_wb_driver,
	&spacemit_dphy_driver,
};

#ifdef MODULE
extern int dsi_core_register(void);
extern int dpu_core_register(void);
extern int dphy_core_register(void);
extern int display_class_init(void);
void drm_core_register(void)
{
	display_class_init();
	dsi_core_register();
	dpu_core_register();
	dphy_core_register();
}
#endif
static int __init spacemit_drm_drivers_init(void)
{
#ifdef MODULE
	drm_core_register();
#endif
	return platform_register_drivers(spacemit_drm_drivers,
					 ARRAY_SIZE(spacemit_drm_drivers));
}

static void __exit spacemit_drm_drivers_exit(void)
{
	platform_unregister_drivers(spacemit_drm_drivers,
				    ARRAY_SIZE(spacemit_drm_drivers));
}

module_init(spacemit_drm_drivers_init);
module_exit(spacemit_drm_drivers_exit);

MODULE_DESCRIPTION("Spacemit DRM KMS Master Driver");
MODULE_LICENSE("GPL v2");
