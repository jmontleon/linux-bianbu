// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 SPACEMIT Micro Limited
 */

#include "linux/export.h"
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include "spacemit-snd-adsp.h"
#include "spacemit-snd-msgs.h"
#include "spacemit-snd-soc.h"

extern struct snd_soc_jack jack;

static void spacemit_snd_jack_handle_event(struct spacemit_snd_adsp_event_handler *ev_handler,
                                      uint32_t ev, void *arg)
{
	struct spacemit_snd_ipc_jack_event *info = (struct spacemit_snd_ipc_jack_event *)arg;

	if (IPC_GET_NOTIFY_TYPE(ev) != IPC_MSG_NOTIFY_TYPE_JACK_EVENT) {
		pr_err("invalid event 0x%x\n", ev);
		return;
	}

	pr_info("info report_type 0x%x jack_type 0x%x key_type 0x%x\n", info->report_type, info->jack_type, info->key_type);

	if (info->report_type == REPORT_JACK_TYPE) {
		snd_soc_jack_report(&jack, info->jack_type, SND_JACK_HEADSET);
	} else if (info->report_type == REPORT_KEY_TYPE) {
		snd_soc_jack_report(&jack, info->key_type, (SND_JACK_BTN_0 | SND_JACK_BTN_1 | SND_JACK_BTN_2));
	}
}

static struct spacemit_snd_adsp_event_handler jack_event_handler = {
	.handle_event = spacemit_snd_jack_handle_event
};

int spacemit_snd_soc_init(struct snd_soc_card *card)
{
	spacemit_snd_adsp_register_event_handler(&jack_event_handler);
	spacemit_snd_adsp_init(card->name);

	return 0;
}

