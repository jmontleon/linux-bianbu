#ifndef __SPACEMIT_MAILBOX_H__
#define __SPACEMIT_MAILBOX_H__

#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/spinlock.h>

#define SPACEMIT_NUM_CHANNELS	4
#define SPACEMIT_TX_ACK_OFFSET	4

typedef struct mbox_reg_desc {
	unsigned int ipc_dw;
	unsigned int ipc_wdr;
	unsigned int ipc_isrw;
	unsigned int ipc_icr;
	unsigned int ipc_iir;
	unsigned int ipc_rdr;
} mbox_reg_desc_t;

struct spacemit_mailbox {
	struct mbox_controller controller;
	struct reset_control *reset;
	mbox_reg_desc_t *regs;
	spinlock_t lock;
	struct device *dev;
};

struct spacemit_mb_con_priv {
	struct spacemit_mailbox *smb;
};

#endif /* __SPACEMIT_MAILBOX_H__ */
