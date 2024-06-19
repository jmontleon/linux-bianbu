/* SPDX-License-Identifier: GPL-2.0 */
#ifndef K1X_ISP_PIPE_DEV_H
#define K1X_ISP_PIPE_DEV_H

struct file_operations *k1xisp_pipe_get_fops(void);
int k1xisp_pipe_dev_init(struct platform_device *pdev,
			 struct k1xisp_pipe_dev *isp_pipe_dev[]);
int k1xisp_pipe_dev_exit(struct platform_device *pdev,
			 struct k1xisp_pipe_dev *isp_pipe_dev[]);
void k1xisp_pipe_dev_irq_handler(void *irq_data);
void k1xisp_pipe_dma_irq_handler(struct k1xisp_pipe_dev *pipe_dev, void *irq_data);
#endif
