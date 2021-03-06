// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2017 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//         Keyon Jie <yang.jie@linux.intel.com>
//         Rander Wang <rander.wang@intel.com>

#include <sof/drivers/ipc.h>
#include <sof/drivers/spi.h>
#include <sof/lib/mailbox.h>
#include <sof/lib/memory.h>
#include <sof/lib/wait.h>
#include <sof/list.h>
#include <sof/schedule/edf_schedule.h>
#include <sof/schedule/schedule.h>
#include <sof/schedule/task.h>
#include <sof/spinlock.h>
#include <ipc/header.h>
#include <stddef.h>
#include <stdint.h>

/* No private data for IPC */
enum task_state ipc_platform_do_cmd(void *data)
{
	struct ipc *ipc = data;
	struct sof_ipc_cmd_hdr *hdr;
	struct sof_ipc_reply reply;

	/* perform command */
	hdr = mailbox_validate();
	ipc_cmd(hdr);

	mailbox_hostbox_read(&reply, SOF_IPC_MSG_MAX_SIZE,
			     0, sizeof(reply));
	spi_push(spi_get(SOF_SPI_INTEL_SLAVE), &reply, sizeof(reply));

	// TODO: signal audio work to enter D3 in normal context
	/* are we about to enter D3 ? */
	if (ipc->pm_prepare_D3) {
		platform_shared_commit(ipc, sizeof(*ipc));

		while (1)
			wait_for_interrupt(0);
	}

	return SOF_TASK_STATE_COMPLETED;
}

void ipc_platform_complete_cmd(void *data)
{
}

void ipc_platform_send_msg(void)
{
	struct ipc *ipc = ipc_get();
	struct ipc_msg *msg;
	uint32_t flags;

	spin_lock_irq(&ipc->lock, flags);

	/* any messages to send ? */
	if (list_is_empty(&ipc->msg_list))
		goto out;

	/* now send the message */
	msg = list_first_item(&ipc->msg_list, struct ipc_msg,
			      list);
	mailbox_dspbox_write(0, msg->tx_data, msg->tx_size);
	list_item_del(&msg->list);
	tracev_ipc("ipc: msg tx -> 0x%x", msg->header);

	/* now interrupt host to tell it we have message sent */

	list_item_append(&msg->list, &ipc->empty_list);

out:
	platform_shared_commit(ipc, sizeof(*ipc));

	spin_unlock_irq(&ipc->lock, flags);
}

int platform_ipc_init(struct ipc *ipc)
{
	ipc_set_drvdata(ipc, NULL);

	/* schedule */
	schedule_task_init_edf(&ipc->ipc_task, &ipc_task_ops, ipc, 0, 0);

	platform_shared_commit(ipc, sizeof(*ipc));

	return 0;
}
