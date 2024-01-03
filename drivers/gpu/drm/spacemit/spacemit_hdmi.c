// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Spacemit Co., Ltd.
 *
 */

#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hdmi.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/component.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>


#include "spacemit_hdmi.h"
#include "spacemit_lib.h"
#include "spacemit_dpu.h"

struct hdmi_data_info {
	int vic;
	bool sink_has_audio;
	unsigned int enc_in_format;
	unsigned int enc_out_format;
	unsigned int colorimetry;
};

struct spacemit_hdmi_i2c {
	u8 segment_addr;

	struct mutex lock;
	struct completion cmp;
};

struct spacemit_hdmi {
	struct device *dev;
	struct drm_device *drm_dev;

	int irq;
	struct clk *pclk;
	void __iomem *regs;

	struct drm_connector	connector;
	struct drm_encoder encoder;

	unsigned int tmds_rate;

	struct hdmi_data_info	hdmi_data;
	struct drm_display_mode previous_mode;
};

#define encoder_to_spacemit_hdmi(encoder) \
	container_of(encoder, struct spacemit_hdmi, encoder)

#define connector_to_spacemit_hdmi(connector) \
	container_of(connector, struct spacemit_hdmi, connector)

static inline u8 hdmi_readb(struct spacemit_hdmi *hdmi, u16 offset)
{
	return readl_relaxed(hdmi->regs + (offset));
}

static inline void hdmi_writeb(struct spacemit_hdmi *hdmi, u16 offset, u32 val)
{
	void __iomem *hdmi_addr = (void __iomem *)ioremap(0xc0400500, 200);
	//writel_relaxed(val, hdmi->regs + (offset));
	writel_relaxed(val, hdmi_addr + (offset));
}

static inline void hdmi_modb(struct spacemit_hdmi *hdmi, u16 offset,
			     u32 msk, u32 val)
{
	u8 temp = hdmi_readb(hdmi, offset) & ~msk;

	temp |= val & msk;
	hdmi_writeb(hdmi, offset, temp);
}

static void spacemit_hdmi_set_pwr_mode(struct spacemit_hdmi *hdmi, int mode)
{
	//normal/ low power
}

static void spacemit_hdmi_reset(struct spacemit_hdmi *hdmi)
{
}

static int spacemit_hdmi_config_video_vsi(struct spacemit_hdmi *hdmi,
				      struct drm_display_mode *mode)
{
	union hdmi_infoframe frame;
	int rc;

	rc = drm_hdmi_vendor_infoframe_from_display_mode(&frame.vendor.hdmi,
							 &hdmi->connector,
							 mode);

	return 0;
}

static int spacemit_hdmi_upload_frame(struct spacemit_hdmi *hdmi, int setup_rc,
				  union hdmi_infoframe *frame, u32 frame_index,
				  u32 mask, u32 disable, u32 enable)
{
	if (setup_rc >= 0) {
		u8 packed_frame[0x11];
		ssize_t rc;

		rc = hdmi_infoframe_pack(frame, packed_frame,
					 sizeof(packed_frame));
		if (rc < 0)
			return rc;
	}

	return setup_rc;
}

static int spacemit_hdmi_config_video_avi(struct spacemit_hdmi *hdmi,
				      struct drm_display_mode *mode)
{
	union hdmi_infoframe frame;
	int rc;

	rc = drm_hdmi_avi_infoframe_from_display_mode(&frame.avi,
						      &hdmi->connector,
						      mode);

	if (hdmi->hdmi_data.enc_out_format == HDMI_COLORSPACE_YUV444)
		frame.avi.colorspace = HDMI_COLORSPACE_YUV444;
	else if (hdmi->hdmi_data.enc_out_format == HDMI_COLORSPACE_YUV422)
		frame.avi.colorspace = HDMI_COLORSPACE_YUV422;
	else
		frame.avi.colorspace = HDMI_COLORSPACE_RGB;

	return spacemit_hdmi_upload_frame(hdmi, rc, &frame, INFOFRAME_AVI, 0, 0, 0);
}

static int spacemit_hdmi_config_video_timing(struct spacemit_hdmi *hdmi,
					 struct drm_display_mode *mode)
{
	return 0;
}

static int spacemit_hdmi_setup(struct spacemit_hdmi *hdmi,
			   struct drm_display_mode *mode)
{
	void __iomem *hdmi_addr = (void __iomem *)ioremap(0xC0400500, 1);
	// hdmi config
	writel(0x40, hdmi_addr + 0x34);
	writel(0x20200000, hdmi_addr + 0xe8);
	writel(0x508d425a, hdmi_addr + 0xec);
	writel(0x861, hdmi_addr + 0xf0);
	writel(0x3, hdmi_addr + 0xe4);

	writel(0x28008320, hdmi_addr + 0x20);
	writel(0x1e00a20d, hdmi_addr + 0x24);
	writel(0x0e404000, hdmi_addr + 0x28);


	spacemit_hdmi_config_video_timing(hdmi, mode);

	spacemit_hdmi_config_video_avi(hdmi, mode);
	spacemit_hdmi_config_video_vsi(hdmi, mode);

	return 0;
}

static void spacemit_hdmi_encoder_mode_set(struct drm_encoder *encoder,
				       struct drm_display_mode *mode,
				       struct drm_display_mode *adj_mode)
{
	struct spacemit_hdmi *hdmi = encoder_to_spacemit_hdmi(encoder);
	DRM_INFO("%s()\n", __func__);
	/* Store the display mode for plugin/DPMS poweron events */
	drm_mode_copy(&hdmi->previous_mode, adj_mode);
}

static void spacemit_hdmi_encoder_enable(struct drm_encoder *encoder)
{
	struct spacemit_hdmi *hdmi = encoder_to_spacemit_hdmi(encoder);
	DRM_INFO("%s()\n", __func__);
	spacemit_hdmi_set_pwr_mode(hdmi, NORMAL);
	spacemit_hdmi_setup(hdmi, &hdmi->previous_mode);
}

static void spacemit_hdmi_encoder_disable(struct drm_encoder *encoder)
{
	struct spacemit_hdmi *hdmi = encoder_to_spacemit_hdmi(encoder);

	spacemit_hdmi_set_pwr_mode(hdmi, LOWER_PWR);
}

static bool spacemit_hdmi_encoder_mode_fixup(struct drm_encoder *encoder,
					 const struct drm_display_mode *mode,
					 struct drm_display_mode *adj_mode)
{
	return true;
}

static int
spacemit_hdmi_encoder_atomic_check(struct drm_encoder *encoder,
			       struct drm_crtc_state *crtc_state,
			       struct drm_connector_state *conn_state)
{
	return 0;
}

static struct drm_encoder_helper_funcs spacemit_hdmi_encoder_helper_funcs = {
	.enable     = spacemit_hdmi_encoder_enable,
	.disable    = spacemit_hdmi_encoder_disable,
	.mode_fixup = spacemit_hdmi_encoder_mode_fixup,
	.mode_set   = spacemit_hdmi_encoder_mode_set,
	.atomic_check = spacemit_hdmi_encoder_atomic_check,
};

static enum drm_connector_status
spacemit_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static int spacemit_hdmi_connector_get_modes(struct drm_connector *connector)
{
	// struct spacemit_hdmi *hdmi = connector_to_spacemit_hdmi(connector);
	// struct edid *edid;
	int ret = 0;

	return ret;
}

static enum drm_mode_status
spacemit_hdmi_connector_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	return MODE_OK;
}

static int
spacemit_hdmi_probe_single_connector_modes(struct drm_connector *connector,
				       uint32_t maxX, uint32_t maxY)
{
	return drm_helper_probe_single_connector_modes(connector, 1920, 1080);
}

static void spacemit_hdmi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs spacemit_hdmi_connector_funcs = {
	.fill_modes = spacemit_hdmi_probe_single_connector_modes,
	.detect = spacemit_hdmi_connector_detect,
	.destroy = spacemit_hdmi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct drm_connector_helper_funcs spacemit_hdmi_connector_helper_funcs = {
	.get_modes = spacemit_hdmi_connector_get_modes,
	.mode_valid = spacemit_hdmi_connector_mode_valid,
};

static int spacemit_hdmi_register(struct drm_device *drm, struct spacemit_hdmi *hdmi)
{
	struct drm_encoder *encoder = &hdmi->encoder;
	struct device *dev = hdmi->dev;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);

	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	drm_encoder_helper_add(encoder, &spacemit_hdmi_encoder_helper_funcs);
	drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_TMDS);

	hdmi->connector.polled = DRM_CONNECTOR_POLL_HPD;

	drm_connector_helper_add(&hdmi->connector,
				 &spacemit_hdmi_connector_helper_funcs);

	drm_connector_init(drm, &hdmi->connector,
				 &spacemit_hdmi_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);

	drm_connector_attach_encoder(&hdmi->connector, encoder);

	return 0;
}

static int spacemit_hdmi_bind(struct device *dev, struct device *master,
				 void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct spacemit_hdmi *hdmi;
	// int irq;
	int ret;

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	hdmi->dev = dev;
	hdmi->drm_dev = drm;

	hdmi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hdmi->regs))
		return PTR_ERR(hdmi->regs);

	spacemit_hdmi_reset(hdmi);

	ret = spacemit_hdmi_register(drm, hdmi);

	dev_set_drvdata(dev, hdmi);

	return 0;
}

static void spacemit_hdmi_unbind(struct device *dev, struct device *master,
			     void *data)
{
	struct spacemit_hdmi *hdmi = dev_get_drvdata(dev);

	hdmi->connector.funcs->destroy(&hdmi->connector);
}

static const struct component_ops spacemit_hdmi_ops = {
	.bind	= spacemit_hdmi_bind,
	.unbind	= spacemit_hdmi_unbind,
};

static int spacemit_hdmi_probe(struct platform_device *pdev)
{
	DRM_INFO("%s()\n", __func__);
	return component_add(&pdev->dev, &spacemit_hdmi_ops);
}

static int spacemit_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &spacemit_hdmi_ops);

	return 0;
}

static const struct of_device_id spacemit_hdmi_dt_ids[] = {
	{ .compatible = "spacemit,hdmi",
	},
	{},
};
MODULE_DEVICE_TABLE(of, spacemit_hdmi_dt_ids);

struct platform_driver spacemit_hdmi_driver = {
	.probe  = spacemit_hdmi_probe,
	.remove = spacemit_hdmi_remove,
	.driver = {
		.name = "spacemit-hdmi-drv",
		.of_match_table = spacemit_hdmi_dt_ids,
	},
};

module_platform_driver(spacemit_hdmi_driver);
