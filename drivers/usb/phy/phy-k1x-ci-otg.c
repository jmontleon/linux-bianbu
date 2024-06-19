// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OTG support for Spacemit k1x SoCs
 *
 * Copyright (c) 2023 Spacemit Inc.
 */
#include <linux/irqreturn.h>
#include <linux/reset.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/usb/otg.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/platform_data/k1x_ci_usb.h>
#include <linux/of_address.h>
#include <dt-bindings/usb/k1x_ci_usb.h>
#include <linux/pm_qos.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/extcon.h>
#include <linux/extcon-provider.h>
#include "phy-k1x-ci-otg.h"

#define MAX_RETRY_TIMES 60
#define RETRY_SLEEP_MS 1000

#define APMU_SD_ROT_WAKE_CLR 0x7C
#define USB_OTG_ID_WAKEUP_EN (1 << 8)
#define USB_OTG_ID_WAKEUP_CLR (1 << 18)

#define PMU_SD_ROT_WAKE_CLR_VBUS_DRV (0x1 << 21)

static const char driver_name[] = "k1x-ci-otg";

static int otg_force_host_mode;
static int otg_state = 0;

static char *state_string[] = {
	"undefined",	"b_idle",     "b_srp_init", "b_peripheral",
	"b_wait_acon",	"b_host",     "a_idle",	    "a_wait_vrise",
	"a_wait_bcon",	"a_host",     "a_suspend",  "a_peripheral",
	"a_wait_vfall", "a_vbus_err",
};

static int mv_otg_set_vbus(struct usb_otg *otg, bool on)
{
	struct mv_otg *mvotg = container_of(otg->usb_phy, struct mv_otg, phy);
	uint32_t temp;

	dev_dbg(&mvotg->pdev->dev, "%s  on= %d ... \n", __func__, on);
	/* set constraint before turn on vbus */
	if (on) {
		pm_stay_awake(&mvotg->pdev->dev);
	}

	if (on) {
		temp = readl(mvotg->apmu_base + APMU_SD_ROT_WAKE_CLR);
		writel(PMU_SD_ROT_WAKE_CLR_VBUS_DRV | temp,
		       mvotg->apmu_base + APMU_SD_ROT_WAKE_CLR);
	} else {
		temp = readl(mvotg->apmu_base + APMU_SD_ROT_WAKE_CLR);
		temp &= ~PMU_SD_ROT_WAKE_CLR_VBUS_DRV;
		writel(temp, mvotg->apmu_base + APMU_SD_ROT_WAKE_CLR);
	}

	gpiod_set_value(mvotg->vbus_gpio, on);

	/* release constraint after turn off vbus */
	if (!on) {
		pm_relax(&mvotg->pdev->dev);
	}

	return 0;
}

static int mv_otg_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	pr_debug("%s ... \n", __func__);
	otg->host = host;

	return 0;
}

static int mv_otg_set_peripheral(struct usb_otg *otg, struct usb_gadget *gadget)
{
	pr_debug("%s ... \n", __func__);
	otg->gadget = gadget;

	return 0;
}

static void mv_otg_run_state_machine(struct mv_otg *mvotg, unsigned long delay)
{
	dev_dbg(&mvotg->pdev->dev, "mv_otg_run_state_machine ... \n");
	dev_dbg(&mvotg->pdev->dev, "transceiver is updated\n");
	if (!mvotg->qwork)
		return;

	queue_delayed_work(mvotg->qwork, &mvotg->work, delay);
}

static void mv_otg_timer_await_bcon(struct timer_list *t)
{
	struct mv_otg *mvotg =
		from_timer(mvotg, t, otg_ctrl.timer[A_WAIT_BCON_TIMER]);

	mvotg->otg_ctrl.a_wait_bcon_timeout = 1;

	dev_info(&mvotg->pdev->dev, "B Device No Response!\n");

	if (spin_trylock(&mvotg->wq_lock)) {
		mv_otg_run_state_machine(mvotg, 0);
		spin_unlock(&mvotg->wq_lock);
	}
}

static int mv_otg_cancel_timer(struct mv_otg *mvotg, unsigned int id)
{
	struct timer_list *timer;

	if (id >= OTG_TIMER_NUM)
		return -EINVAL;

	timer = &mvotg->otg_ctrl.timer[id];

	if (timer_pending(timer))
		del_timer(timer);

	return 0;
}

static int mv_otg_set_timer(struct mv_otg *mvotg, unsigned int id,
				unsigned long interval)
{
	struct timer_list *timer;

	if (id >= OTG_TIMER_NUM)
		return -EINVAL;

	timer = &mvotg->otg_ctrl.timer[id];
	if (timer_pending(timer)) {
		dev_err(&mvotg->pdev->dev, "Timer%d is already running\n", id);
		return -EBUSY;
	}

	timer->expires = jiffies + interval;
	add_timer(timer);

	return 0;
}

static int mv_otg_reset(struct mv_otg *mvotg)
{
	unsigned int loops;
	u32 tmp;

	dev_dbg(&mvotg->pdev->dev, "mv_otg_reset \n");
	/* Stop the controller */
	tmp = readl(&mvotg->op_regs->usbcmd);
	tmp &= ~USBCMD_RUN_STOP;
	writel(tmp, &mvotg->op_regs->usbcmd);

	/* Reset the controller to get default values */
	writel(USBCMD_CTRL_RESET, &mvotg->op_regs->usbcmd);

	loops = 500;
	while (readl(&mvotg->op_regs->usbcmd) & USBCMD_CTRL_RESET) {
		if (loops == 0) {
			dev_err(&mvotg->pdev->dev,
				"Wait for RESET completed TIMEOUT\n");
			return -ETIMEDOUT;
		}
		loops--;
		udelay(20);
	}

	writel(0x0, &mvotg->op_regs->usbintr);
	tmp = readl(&mvotg->op_regs->usbsts);
	writel(tmp, &mvotg->op_regs->usbsts);

	return 0;
}

static void mv_otg_init_irq(struct mv_otg *mvotg)
{
	u32 otgsc;

	mvotg->irq_en = OTGSC_INTR_A_SESSION_VALID | OTGSC_INTR_A_VBUS_VALID;
	mvotg->irq_status = OTGSC_INTSTS_A_SESSION_VALID |
				OTGSC_INTSTS_A_VBUS_VALID;

	if ((mvotg->pdata->extern_attr & MV_USB_HAS_VBUS_DETECTION) == 0) {
		mvotg->irq_en |= OTGSC_INTR_B_SESSION_VALID |
				 OTGSC_INTR_B_SESSION_END;
		mvotg->irq_status |= OTGSC_INTSTS_B_SESSION_VALID |
					 OTGSC_INTSTS_B_SESSION_END;
	}

	if ((mvotg->pdata->extern_attr & MV_USB_HAS_IDPIN_DETECTION) == 0) {
		mvotg->irq_en |= OTGSC_INTR_USB_ID;
		mvotg->irq_status |= OTGSC_INTSTS_USB_ID;
	}

	otgsc = readl(&mvotg->op_regs->otgsc);
	otgsc |= mvotg->irq_en;
	writel(otgsc, &mvotg->op_regs->otgsc);
}

static void mv_otg_start_host(struct mv_otg *mvotg, int on)
{
	struct usb_otg *otg = mvotg->phy.otg;
	struct usb_hcd *hcd;

	dev_dbg(&mvotg->pdev->dev, "%s ...\n", __func__);
	if (!otg->host) {
		int retry = 0;
		while (retry < MAX_RETRY_TIMES) {
			retry++;
			msleep(RETRY_SLEEP_MS);
			if (otg->host)
				break;
		}

		if (!otg->host) {
			dev_err(&mvotg->pdev->dev, "otg->host is not set!\n");
			return;
		}
	}

	dev_info(&mvotg->pdev->dev, "%s host\n", on ? "start" : "stop");

	hcd = bus_to_hcd(otg->host);

	if (on) {
		usb_add_hcd(hcd, hcd->irq, IRQF_SHARED);
		device_wakeup_enable(hcd->self.controller);
	} else {
		usb_remove_hcd(hcd);
	}
}

static void mv_otg_start_peripheral(struct mv_otg *mvotg, int on)
{
	struct usb_otg *otg = mvotg->phy.otg;

	dev_dbg(&mvotg->pdev->dev, "%s ...\n", __func__);
	if (!otg->gadget) {
		int retry = 0;
		while (retry < MAX_RETRY_TIMES) {
			retry++;
			msleep(RETRY_SLEEP_MS);
			if (otg->gadget)
				break;
		}

		if (!otg->gadget) {
			dev_err(&mvotg->pdev->dev, "otg->gadget is not set!\n");
			return;
		}
	}

	dev_info(&mvotg->pdev->dev, "gadget %s\n", on ? "on" : "off");

	if (on)
		usb_gadget_vbus_connect(otg->gadget);
	else
		usb_gadget_vbus_disconnect(otg->gadget);
}

static void otg_clock_enable(struct mv_otg *mvotg)
{
	clk_enable(mvotg->clk);
}

static void otg_reset_assert(struct mv_otg *mvotg)
{
	reset_control_assert(mvotg->reset);
}

static void otg_reset_deassert(struct mv_otg *mvotg)
{
	reset_control_deassert(mvotg->reset);
}

static void otg_clock_disable(struct mv_otg *mvotg)
{
	clk_disable(mvotg->clk);
}

static int mv_otg_enable_internal(struct mv_otg *mvotg)
{
	int retval = 0;

	dev_dbg(&mvotg->pdev->dev,
		 "mv_otg_enable_internal: mvotg->active= %d \n", mvotg->active);
	if (mvotg->active)
		return 0;

	dev_dbg(&mvotg->pdev->dev,
		"otg enabled, will enable clk, release rst\n");

	otg_clock_enable(mvotg);
	otg_reset_assert(mvotg);
	otg_reset_deassert(mvotg);
	retval = usb_phy_init(mvotg->outer_phy);
	if (retval) {
		dev_err(&mvotg->pdev->dev, "failed to initialize phy %d\n",
			retval);
		otg_clock_disable(mvotg);
		return retval;
	}

	mvotg->active = 1;
	otg_state = 1;

	return 0;
}

static int mv_otg_enable(struct mv_otg *mvotg)
{
	if (mvotg->clock_gating)
		return mv_otg_enable_internal(mvotg);

	return 0;
}

static void mv_otg_disable_internal(struct mv_otg *mvotg)
{
	dev_dbg(&mvotg->pdev->dev,
		 "mv_otg_disable_internal: mvotg->active= %d ... \n",
		 mvotg->active);
	if (mvotg->active) {
		dev_dbg(&mvotg->pdev->dev, "otg disabled\n");
		usb_phy_shutdown(mvotg->outer_phy);
		otg_reset_assert(mvotg);
		otg_clock_disable(mvotg);
		mvotg->active = 0;
		otg_state = 0;
	}
}

static void mv_otg_disable(struct mv_otg *mvotg)
{
	if (mvotg->clock_gating)
		mv_otg_disable_internal(mvotg);
}

static void mv_otg_update_inputs(struct mv_otg *mvotg)
{
	struct mv_otg_ctrl *otg_ctrl = &mvotg->otg_ctrl;
	u32 otgsc;

	otgsc = readl(&mvotg->op_regs->otgsc);

	if (mvotg->pdata->extern_attr & MV_USB_HAS_VBUS_DETECTION) {
		unsigned int vbus;
		vbus = extcon_get_state(mvotg->extcon, EXTCON_USB);
		dev_dbg(&mvotg->pdev->dev, "-->%s: vbus = %d\n", __func__,
			 vbus);

		if (vbus == VBUS_HIGH)
			otg_ctrl->b_sess_vld = 1;
		else
			otg_ctrl->b_sess_vld = 0;
	} else {
		dev_err(&mvotg->pdev->dev, "vbus detect was not supported ...");
	}

	if (mvotg->pdata->extern_attr & MV_USB_HAS_IDPIN_DETECTION) {
		unsigned int id;
		/* id = 0 means the otg cable is absent. */
		id = extcon_get_state(mvotg->extcon, EXTCON_USB_HOST);
		dev_dbg(&mvotg->pdev->dev, "-->%s: id = %d\n", __func__, id);

		otg_ctrl->id = !id;
		otg_ctrl->a_vbus_vld = !!id;
	} else {
		dev_err(&mvotg->pdev->dev,
			"id pin detect was not supported ...");
	}

	if (otg_force_host_mode) {
		otg_ctrl->id = 0;
		otg_ctrl->a_vbus_vld = 1;
	}

	dev_dbg(&mvotg->pdev->dev, "id %d\n", otg_ctrl->id);
	dev_dbg(&mvotg->pdev->dev, "b_sess_vld %d\n", otg_ctrl->b_sess_vld);
	dev_dbg(&mvotg->pdev->dev, "a_vbus_vld %d\n", otg_ctrl->a_vbus_vld);
}

static void mv_otg_update_state(struct mv_otg *mvotg)
{
	struct mv_otg_ctrl *otg_ctrl = &mvotg->otg_ctrl;
	int old_state = mvotg->phy.otg->state;

	switch (old_state) {
	case OTG_STATE_UNDEFINED:
		mvotg->phy.otg->state = OTG_STATE_B_IDLE;
		fallthrough;
	case OTG_STATE_B_IDLE:
		if (otg_ctrl->id == 0)
			mvotg->phy.otg->state = OTG_STATE_A_IDLE;
		else if (otg_ctrl->b_sess_vld)
			mvotg->phy.otg->state = OTG_STATE_B_PERIPHERAL;
		break;
	case OTG_STATE_B_PERIPHERAL:
		if (!otg_ctrl->b_sess_vld || otg_ctrl->id == 0)
			mvotg->phy.otg->state = OTG_STATE_B_IDLE;
		break;
	case OTG_STATE_A_IDLE:
		if (otg_ctrl->id)
			mvotg->phy.otg->state = OTG_STATE_B_IDLE;
		else
			mvotg->phy.otg->state = OTG_STATE_A_WAIT_VRISE;
		break;
	case OTG_STATE_A_WAIT_VRISE:
		if (otg_ctrl->a_vbus_vld)
			mvotg->phy.otg->state = OTG_STATE_A_WAIT_BCON;
		break;
	case OTG_STATE_A_WAIT_BCON:
		if (otg_ctrl->id || otg_ctrl->a_wait_bcon_timeout) {
			mv_otg_cancel_timer(mvotg, A_WAIT_BCON_TIMER);
			mvotg->otg_ctrl.a_wait_bcon_timeout = 0;
			mvotg->phy.otg->state = OTG_STATE_A_WAIT_VFALL;
		} else if (otg_ctrl->b_conn) {
			mv_otg_cancel_timer(mvotg, A_WAIT_BCON_TIMER);
			mvotg->otg_ctrl.a_wait_bcon_timeout = 0;
			mvotg->phy.otg->state = OTG_STATE_A_HOST;
		}
		break;
	case OTG_STATE_A_HOST:
		if (otg_ctrl->id || !otg_ctrl->b_conn)
			mvotg->phy.otg->state = OTG_STATE_A_WAIT_BCON;
		else if (!otg_ctrl->a_vbus_vld)
			mvotg->phy.otg->state = OTG_STATE_A_VBUS_ERR;
		break;
	case OTG_STATE_A_WAIT_VFALL:
		if (otg_ctrl->id || (!otg_ctrl->b_conn))
			mvotg->phy.otg->state = OTG_STATE_A_IDLE;
		break;
	case OTG_STATE_A_VBUS_ERR:
		if (otg_ctrl->id) {
			mvotg->phy.otg->state = OTG_STATE_A_WAIT_VFALL;
		}
		break;
	default:
		break;
	}
}

static void mv_otg_work(struct work_struct *work)
{
	struct mv_otg *mvotg;
	struct usb_phy *phy;
	struct usb_otg *otg;
	int old_state;

	mvotg = container_of(to_delayed_work(work), struct mv_otg, work);

run:
	/* work queue is single thread, or we need spin_lock to protect */
	phy = &mvotg->phy;
	otg = mvotg->phy.otg;
	old_state = otg->state;

	if (!mvotg->active)
		return;

	mv_otg_update_inputs(mvotg);
	mv_otg_update_state(mvotg);
	if (old_state != mvotg->phy.otg->state) {
		dev_dbg(&mvotg->pdev->dev, "change from state %s to %s\n",
			state_string[old_state],
			state_string[mvotg->phy.otg->state]);

		switch (mvotg->phy.otg->state) {
		case OTG_STATE_B_IDLE:
			otg->default_a = 0;
			if (old_state == OTG_STATE_B_PERIPHERAL ||
				old_state == OTG_STATE_UNDEFINED)
				mv_otg_start_peripheral(mvotg, 0);
			mv_otg_reset(mvotg);
			mv_otg_disable(mvotg);
			break;
		case OTG_STATE_B_PERIPHERAL:
			mv_otg_enable(mvotg);
			mv_otg_start_peripheral(mvotg, 1);
			break;
		case OTG_STATE_A_IDLE:
			otg->default_a = 1;
			mv_otg_enable(mvotg);
			if (old_state == OTG_STATE_A_WAIT_VFALL)
				mv_otg_start_host(mvotg, 0);
			mv_otg_reset(mvotg);
			break;
		case OTG_STATE_A_WAIT_VRISE:
			mv_otg_set_vbus(otg, 1);
			break;
		case OTG_STATE_A_WAIT_BCON:
			if (old_state != OTG_STATE_A_HOST)
				mv_otg_start_host(mvotg, 1);
			mv_otg_set_timer(mvotg, A_WAIT_BCON_TIMER,
					 T_A_WAIT_BCON);
			/*
			 * Now, we directly enter A_HOST. So set b_conn = 1
			 * here. In fact, it need host driver to notify us.
			 */
			mvotg->otg_ctrl.b_conn = 1;
			break;
		case OTG_STATE_A_HOST:
			break;
		case OTG_STATE_A_WAIT_VFALL:
			/*
			 * Now, we has exited A_HOST. So set b_conn = 0
			 * here. In fact, it need host driver to notify us.
			 */
			mvotg->otg_ctrl.b_conn = 0;
			mv_otg_set_vbus(otg, 0);
			break;
		case OTG_STATE_A_VBUS_ERR:
			break;
		default:
			break;
		}
		goto run;
	} else {
		dev_dbg(&mvotg->pdev->dev,
			"state no change: last_state: %s, current_state: %s\n",
			state_string[old_state],
			state_string[mvotg->phy.otg->state]);
	}
}

static irqreturn_t mv_otg_irq(int irq, void *dev)
{
	struct mv_otg *mvotg = dev;
	u32 otgsc;

	/* if otg clock is not enabled, otgsc read out will be 0 */
	if (!mvotg->active)
		mv_otg_enable(mvotg);

	otgsc = readl(&mvotg->op_regs->otgsc);
	writel(otgsc | mvotg->irq_en, &mvotg->op_regs->otgsc);

	if (!(mvotg->pdata->extern_attr & MV_USB_HAS_IDPIN_DETECTION)) {
		if (mvotg->otg_ctrl.id != (!!(otgsc & OTGSC_STS_USB_ID))) {
			dev_dbg(dev, "mv_otg_irq : ID detect  ... \n");
			mv_otg_run_state_machine(mvotg, 0);
			return IRQ_HANDLED;
		}
	}

	/*
	 * if we have vbus, then the vbus detection for B-device
	 * will be done by mv_otg_inputs_irq().
	 * currently mv_otg_inputs_irq is removed
	 */
	if (mvotg->pdata->extern_attr & MV_USB_HAS_VBUS_DETECTION)
		if ((otgsc & OTGSC_STS_USB_ID) &&
			!(otgsc & OTGSC_INTSTS_USB_ID))
			return IRQ_NONE;

	if ((otgsc & mvotg->irq_status) == 0)
		return IRQ_NONE;

	mv_otg_run_state_machine(mvotg, 0);

	return IRQ_HANDLED;
}

static int mv_otg_vbus_notifier_callback(struct notifier_block *nb,
					 unsigned long val, void *v)
{
	struct usb_phy *mvotg_phy = container_of(nb, struct usb_phy, vbus_nb);
	struct mv_otg *mvotg = container_of(mvotg_phy, struct mv_otg, phy);

	/* The clock may disabled at this time */
	if (!mvotg->active) {
		mv_otg_enable(mvotg);
		mv_otg_init_irq(mvotg);
	}

	mv_otg_run_state_machine(mvotg, 0);

	return 0;
}

static int mv_otg_id_notifier_callback(struct notifier_block *nb,
					   unsigned long val, void *v)
{
	struct usb_phy *mvotg_phy = container_of(nb, struct usb_phy, id_nb);
	struct mv_otg *mvotg = container_of(mvotg_phy, struct mv_otg, phy);

	/* The clock may disabled at this time */
	if (!mvotg->active) {
		mv_otg_enable(mvotg);
		mv_otg_init_irq(mvotg);
	}

	mv_otg_run_state_machine(mvotg, 0);

	return 0;
}

static ssize_t get_a_bus_req(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	struct mv_otg *mvotg = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n", mvotg->otg_ctrl.a_bus_req);
}

static ssize_t set_a_bus_req(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct mv_otg *mvotg = dev_get_drvdata(dev);

	if (count > 2)
		return -1;

	/* We will use this interface to change to A device */
	if (mvotg->phy.otg->state != OTG_STATE_B_IDLE &&
		mvotg->phy.otg->state != OTG_STATE_A_IDLE)
		return -1;

	/* The clock may disabled and we need to set irq for ID detected */
	mv_otg_enable(mvotg);
	mv_otg_init_irq(mvotg);

	if (buf[0] == '1') {
		mvotg->otg_ctrl.a_bus_req = 1;
		mvotg->otg_ctrl.a_bus_drop = 0;
		dev_dbg(&mvotg->pdev->dev, "User request: a_bus_req = 1\n");

		if (spin_trylock(&mvotg->wq_lock)) {
			mv_otg_run_state_machine(mvotg, 0);
			spin_unlock(&mvotg->wq_lock);
		}
	}

	return count;
}

static DEVICE_ATTR(a_bus_req, S_IRUGO | S_IWUSR, get_a_bus_req, set_a_bus_req);

static ssize_t set_a_clr_err(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct mv_otg *mvotg = dev_get_drvdata(dev);
	if (!mvotg->phy.otg->default_a)
		return -1;

	if (count > 2)
		return -1;

	if (buf[0] == '1') {
		mvotg->otg_ctrl.a_clr_err = 1;
		dev_dbg(&mvotg->pdev->dev, "User request: a_clr_err = 1\n");
	}

	if (spin_trylock(&mvotg->wq_lock)) {
		mv_otg_run_state_machine(mvotg, 0);
		spin_unlock(&mvotg->wq_lock);
	}

	return count;
}

static DEVICE_ATTR(a_clr_err, S_IWUSR, NULL, set_a_clr_err);

static ssize_t get_a_bus_drop(struct device *dev, struct device_attribute *attr,
				  char *buf)
{
	struct mv_otg *mvotg = dev_get_drvdata(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n", mvotg->otg_ctrl.a_bus_drop);
}

static ssize_t set_a_bus_drop(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct mv_otg *mvotg = dev_get_drvdata(dev);
	if (!mvotg->phy.otg->default_a)
		return -1;

	if (count > 2)
		return -1;

	if (buf[0] == '0') {
		mvotg->otg_ctrl.a_bus_drop = 0;
		dev_info(&mvotg->pdev->dev, "User request: a_bus_drop = 0\n");
	} else if (buf[0] == '1') {
		mvotg->otg_ctrl.a_bus_drop = 1;
		mvotg->otg_ctrl.a_bus_req = 0;
		dev_info(&mvotg->pdev->dev, "User request: a_bus_drop = 1\n");
		dev_info(&mvotg->pdev->dev,
			 "User request: and a_bus_req = 0\n");
	}

	if (spin_trylock(&mvotg->wq_lock)) {
		mv_otg_run_state_machine(mvotg, 0);
		spin_unlock(&mvotg->wq_lock);
	}

	return count;
}

static DEVICE_ATTR(a_bus_drop, S_IRUGO | S_IWUSR, get_a_bus_drop,
		   set_a_bus_drop);

static ssize_t get_otg_mode(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	char *state = otg_force_host_mode ? "host" : "client";
	return sprintf(buf, "OTG mode: %s\n", state);
}

static ssize_t set_otg_mode(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct mv_otg *mvotg = dev_get_drvdata(dev);
	char *usage = "Usage: $echo host/client to switch otg mode";
	char buff[16], *b;

	strncpy(buff, buf, sizeof(buff));
	b = strim(buff);
	dev_info(dev, "OTG state is %s\n", state_string[mvotg->phy.otg->state]);
	if (!strcmp(b, "host")) {
		if (mvotg->phy.otg->state == OTG_STATE_B_PERIPHERAL) {
			pr_err("Failed to swich mode, pls don't connect to PC!\n");
			return count;
		}
		otg_force_host_mode = 1;
	} else if (!strcmp(b, "client")) {
		otg_force_host_mode = 0;
	} else {
		pr_err("%s\n", usage);
		return count;
	}
	mv_otg_run_state_machine(mvotg, 0);

	return count;
}
static DEVICE_ATTR(otg_mode, S_IRUGO | S_IWUSR, get_otg_mode, set_otg_mode);

static struct attribute *inputs_attrs[] = {
	&dev_attr_a_bus_req.attr,
	&dev_attr_a_clr_err.attr,
	&dev_attr_a_bus_drop.attr,
	&dev_attr_otg_mode.attr,
	NULL,
};

static struct attribute_group inputs_attr_group = {
	.name = "inputs",
	.attrs = inputs_attrs,
};

static int mv_otg_remove(struct platform_device *pdev)
{
	struct mv_otg *mvotg = platform_get_drvdata(pdev);

	device_init_wakeup(&pdev->dev, 0);
	sysfs_remove_group(&mvotg->pdev->dev.kobj, &inputs_attr_group);

	if (mvotg->qwork) {
		flush_workqueue(mvotg->qwork);
		destroy_workqueue(mvotg->qwork);
	}

	mv_otg_disable(mvotg);

	clk_unprepare(mvotg->clk);

	usb_remove_phy(&mvotg->phy);

	return 0;
}

static int mv_otg_dt_parse(struct platform_device *pdev,
			   struct mv_usb_platform_data *pdata)
{
	struct device_node *np = pdev->dev.of_node;

	if (of_property_read_string(np, "spacemit,otg-name",
					&((pdev->dev).init_name)))
		return -EINVAL;

	if (of_property_read_u32(np, "spacemit,udc-mode", &(pdata->mode)))
		return -EINVAL;

	if (of_property_read_u32(np, "spacemit,dev-id", &(pdata->id)))
		pdata->id = PXA_USB_DEV_OTG;

	of_property_read_u32(np, "spacemit,extern-attr", &(pdata->extern_attr));
	pdata->otg_force_a_bus_req =
		of_property_read_bool(np, "spacemit,otg-force-a-bus-req");
	pdata->disable_otg_clock_gating =
		of_property_read_bool(np, "spacemit,disable-otg-clock-gating");

	return 0;
}

static int mv_otg_probe(struct platform_device *pdev)
{
	struct mv_usb_platform_data *pdata;
	struct mv_otg *mvotg;
	struct usb_otg *otg;
	struct resource *r;
	int retval = 0, i;
	struct device_node *np = pdev->dev.of_node;

	dev_info(&pdev->dev, "k1x otg probe enter ...\n");
	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (pdata == NULL) {
		dev_err(&pdev->dev, "failed to allocate platform data\n");
		return -ENODEV;
	}
	mv_otg_dt_parse(pdev, pdata);
	mvotg = devm_kzalloc(&pdev->dev, sizeof(*mvotg), GFP_KERNEL);
	if (!mvotg) {
		dev_err(&pdev->dev, "failed to allocate memory!\n");
		return -ENOMEM;
	}

	otg = devm_kzalloc(&pdev->dev, sizeof(*otg), GFP_KERNEL);
	if (!otg)
		return -ENOMEM;

	platform_set_drvdata(pdev, mvotg);

	mvotg->pdev = pdev;
	mvotg->pdata = pdata;

	mvotg->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(mvotg->clk))
		return PTR_ERR(mvotg->clk);
	clk_prepare(mvotg->clk);

	mvotg->reset = devm_reset_control_get_shared(&pdev->dev, NULL);
	if (IS_ERR(mvotg->reset))
		return PTR_ERR(mvotg->reset);

	mvotg->qwork = create_singlethread_workqueue("mv_otg_queue");
	if (!mvotg->qwork) {
		dev_dbg(&pdev->dev, "cannot create workqueue for OTG\n");
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&mvotg->work, mv_otg_work);

	/* OTG common part */
	mvotg->pdev = pdev;
	mvotg->phy.dev = &pdev->dev;
	mvotg->phy.type = USB_PHY_TYPE_USB2;
	mvotg->phy.otg = otg;
	mvotg->phy.label = driver_name;

	otg->usb_phy = &mvotg->phy;
	otg->state = OTG_STATE_UNDEFINED;
	otg->set_host = mv_otg_set_host;
	otg->set_peripheral = mv_otg_set_peripheral;
	otg->set_vbus = mv_otg_set_vbus;

	for (i = 0; i < OTG_TIMER_NUM; i++)
		timer_setup(&mvotg->otg_ctrl.timer[i], mv_otg_timer_await_bcon,
				0);

	r = platform_get_resource(mvotg->pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		dev_err(&pdev->dev, "no I/O memory resource defined\n");
		retval = -ENODEV;
		goto err_destroy_workqueue;
	}

	mvotg->cap_regs = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (mvotg->cap_regs == NULL) {
		dev_err(&pdev->dev, "failed to map I/O memory\n");
		retval = -EFAULT;
		goto err_destroy_workqueue;
	}

	mvotg->outer_phy =
		devm_usb_get_phy_by_phandle(&pdev->dev, "usb-phy", 0);
	if (IS_ERR_OR_NULL(mvotg->outer_phy)) {
		retval = PTR_ERR(mvotg->outer_phy);
		if (retval != -EPROBE_DEFER)
			dev_err(&pdev->dev, "can not find outer phy\n");
		goto err_destroy_workqueue;
	}

	mvotg->vbus_gpio = devm_gpiod_get(&pdev->dev, "vbus", GPIOD_OUT_LOW);
	if (IS_ERR_OR_NULL(mvotg->vbus_gpio)) {
		dev_err(&pdev->dev, "can not find vbus gpio default state\n");
		goto err_destroy_workqueue;
	}else{
		dev_dbg(&pdev->dev, "Use GPIO to control vbus\n");
	}

	/* we will acces controller register, so enable the udc controller */
	retval = mv_otg_enable_internal(mvotg);
	if (retval) {
		dev_err(&pdev->dev, "mv otg enable error %d\n", retval);
		goto err_destroy_workqueue;
	}

	mvotg->op_regs =
		(struct mv_otg_regs __iomem *)((unsigned long)mvotg->cap_regs +
						   (readl(mvotg->cap_regs) &
						CAPLENGTH_MASK));

	if (pdata->extern_attr &
		(MV_USB_HAS_VBUS_DETECTION | MV_USB_HAS_IDPIN_DETECTION)) {
		dev_info(&mvotg->pdev->dev, "%s : support VBUS/ID detect ...\n",
			 __func__);
		/* TODO: use device tree to parse extcon device name */
		if (of_property_read_bool(np, "extcon")) {
			mvotg->extcon =
				extcon_get_edev_by_phandle(&pdev->dev, 0);
			if (IS_ERR(mvotg->extcon)) {
				dev_err(&pdev->dev,
					"couldn't get extcon device\n");
				mv_otg_disable_internal(mvotg);
				retval = -EPROBE_DEFER;
				goto err_destroy_workqueue;
			}
			dev_info(&pdev->dev, "extcon_dev name: %s \n",
				 extcon_get_edev_name(mvotg->extcon));
		} else {
			dev_err(&pdev->dev, "usb extcon cable is not exist\n");
		}

		if (pdata->extern_attr & MV_USB_HAS_VBUS_DETECTION)
			mvotg->clock_gating = 1;

		/*extcon notifier register will be completed in usb_add_phy_dev*/
		mvotg->phy.vbus_nb.notifier_call =
			mv_otg_vbus_notifier_callback;
		mvotg->phy.id_nb.notifier_call = mv_otg_id_notifier_callback;
	}

	if (pdata->disable_otg_clock_gating)
		mvotg->clock_gating = 0;

	mv_otg_reset(mvotg);
	mv_otg_init_irq(mvotg);

	// r = platform_get_resource(mvotg->pdev, IORESOURCE_IRQ, 0);
	mvotg->irq = platform_get_irq(pdev, 0);
	if (!mvotg->irq) {
		dev_err(&pdev->dev, "no IRQ resource defined\n");
		retval = -ENODEV;
		goto err_disable_clk;
	}

	// mvotg->irq = r->start;
	if (devm_request_irq(&pdev->dev, mvotg->irq, mv_otg_irq, IRQF_SHARED,
				 driver_name, mvotg)) {
		dev_err(&pdev->dev, "Request irq %d for OTG failed\n",
			mvotg->irq);
		mvotg->irq = 0;
		retval = -ENODEV;
		goto err_disable_clk;
	}

	retval = usb_add_phy_dev(&mvotg->phy);
	if (retval < 0) {
		dev_err(&pdev->dev, "can't register transceiver, %d\n", retval);
		goto err_disable_clk;
	}

	np = of_find_compatible_node(NULL, NULL, "spacemit,spacemit-apmu");
	BUG_ON(!np);
	mvotg->apmu_base = of_iomap(np, 0);
	if (mvotg->apmu_base == NULL) {
		dev_err(&pdev->dev, "failed to map apmu base memory\n");
		return -EFAULT;
	}

	retval = sysfs_create_group(&pdev->dev.kobj, &inputs_attr_group);
	if (retval < 0) {
		dev_dbg(&pdev->dev, "Can't register sysfs attr group: %d\n",
			retval);
		goto err_remove_otg_phy;
	}

	spin_lock_init(&mvotg->wq_lock);
	if (spin_trylock(&mvotg->wq_lock)) {
		mv_otg_run_state_machine(mvotg, 2 * HZ);
		spin_unlock(&mvotg->wq_lock);
	}

	dev_info(&pdev->dev, "successful probe OTG device %s clock gating.\n",
		 mvotg->clock_gating ? "with" : "without");

	device_init_wakeup(&pdev->dev, 1);

	return 0;

err_remove_otg_phy:
	usb_remove_phy(&mvotg->phy);
err_disable_clk:
	mv_otg_disable_internal(mvotg);
err_destroy_workqueue:
	flush_workqueue(mvotg->qwork);
	destroy_workqueue(mvotg->qwork);

	return retval;
}

#ifdef CONFIG_PM
static int mv_otg_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct mv_otg *mvotg = platform_get_drvdata(pdev);

	if (!mvotg->clock_gating)
		mv_otg_disable_internal(mvotg);

	mvotg->phy.otg->state = OTG_STATE_UNDEFINED;

	return 0;
}

static int mv_otg_resume(struct platform_device *pdev)
{
	struct mv_otg *mvotg = platform_get_drvdata(pdev);
	u32 otgsc;

	mv_otg_enable_internal(mvotg);

	otgsc = readl(&mvotg->op_regs->otgsc);
	otgsc |= mvotg->irq_en;
	writel(otgsc, &mvotg->op_regs->otgsc);

	if (spin_trylock(&mvotg->wq_lock)) {
		mv_otg_run_state_machine(mvotg, 0);
		spin_unlock(&mvotg->wq_lock);
	}

	return 0;
}
#endif

static const struct of_device_id mv_otg_dt_match[] = {
	{ .compatible = "spacemit,mv-otg" },
	{},
};
MODULE_DEVICE_TABLE(of, mv_udc_dt_match);

static struct platform_driver mv_otg_driver = {
	.probe = mv_otg_probe,
	.remove = mv_otg_remove,
	.driver = {
		   .of_match_table = of_match_ptr(mv_otg_dt_match),
		   .name = driver_name,
		   },
#ifdef CONFIG_PM
	.suspend = mv_otg_suspend,
	.resume = mv_otg_resume,
#endif
};
module_platform_driver(mv_otg_driver);

MODULE_DESCRIPTION("Spacemit K1-x OTG driver");
MODULE_LICENSE("GPL");
