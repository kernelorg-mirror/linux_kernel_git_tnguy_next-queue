/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Red Hat */

#ifndef _ICE_ADAPTER_H_
#define _ICE_ADAPTER_H_

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/seqlock.h>
#include <linux/srcu.h>
#include <linux/spinlock_types.h>
#include <linux/timecounter.h>

#include <net/devlink.h>

#include "ice_type.h"

struct pci_dev;
struct ice_pf;

/**
 * struct ice_port_list - data used to store the list of adapter ports
 *
 * This structure contains data used to maintain a list of adapter ports.
 * Writers modifying the list *must* acquire the lock, and use SRCU safe list
 * operations. Readers should use srcu_read_lock() on the provided domain.
 *
 * @list: list of ports
 * @lock: protect write access to the list
 * @srcu: Sleepable RCU domain for this adapter
 */
struct ice_port_list {
	struct list_head list;
	/* To synchronize write operations on the port list */
	spinlock_t lock;
	struct srcu_struct srcu;
};

/**
 * struct ice_phc - PTP Hardware Clock fields
 *
 * Structure used for handling the timestamp extension logic for this device.
 * Devices will either use the legacy timestamp extension logic or
 * a timecounter, not both.
 *
 * For simplicity, the timecounter fields are initialized as part of creating
 * the ice_adapter regardless of whether the device will actually use them.
 *
 * Legacy timestamp extension fields:
 * @cached_time: Cached copy of the PHC time for timestamp extension
 *
 * struct timecounter fields:
 * @tc: timecounter representing the current time. Accessed via seq to allow
 *      lockless readers during updates.
 * @cc: cyclecounter underlying the timecounter
 *
 * General fields:
 * @update_time: Monotonic time when PHC was laste updated
 * @seq: seqcount_latch_t used to protect readers, directing them to the
 *       secondary copy during an update.
 * @lock: For protecting updates to the PHC fields.
 * @use_timecounter: if true, the device uses the timecounter
 *
 * Readers are lockless and rely on the latched sequence counter for
 * a consistent view of the most recent update, even if they interrupt
 * a writer.
 *
 * Writers must acquire the lock mutex to ensure that the periodic task cannot
 * race with clock adjustment callbacks.
 */
struct ice_phc {
	/* Legacy timestamp extension fields */
	u64 cached_time[2];

	/* struct timecounter fields */
	struct timecounter tc[2];
	struct cyclecounter cc;

	/* General fields */
	ktime_t update_time[2];
	seqcount_latch_t seq;
	/* Serialize updates to cached_time or the timecounter */
	struct mutex lock;
	bool use_timecounter;
};

/**
 * struct ice_adapter - PCI adapter resources shared across PFs
 * @devlink: ice adapter's devlink (whole dev devlink)
 * @ptp_gltsyn_time_lock: Spinlock protecting access to the GLTSYN_TIME
 *                        register of the PTP clock.
 * @txq_ctx_lock: Spinlock protecting access to the GLCOMM_QTX_CNTX_CTL register
 * @cpi_phy_lock: Per-PHY mutex serializing CPI REQ/ACK transactions.
 *               Index 0 = PHY0, index 1 = PHY1. Used on E825C devices.
 * @cgu_synce_lock: Mutex serializing changes of the CGU registers R10 and R11,
 *                  which all PFs reach. Used on E825C devices.
 * @ps_lock: Mutex to serialize PHY port start/stop across adapter.
 * @phc: PTP Hardware Clock fields
 * @ctrl_pf_lock: Protect control PF lifetime for sleepable users
 * @ctrl_pf: Control PF of the adapter
 * @rebuild_lock: serialize PFR recovery across PFs of the same adapter
 * @ports: Ports list
 */
struct ice_adapter {
	struct devlink *devlink;

	/* For access to the GLTSYN_TIME register */
	spinlock_t ptp_gltsyn_time_lock;
	/* For access to GLCOMM_QTX_CNTX_CTL register */
	spinlock_t txq_ctx_lock;
	/* Serialize CPI REQ/ACK transactions per PHY (E825C only) */
	struct mutex cpi_phy_lock[ICE_E825_MAX_PHYS];
	/* Serialize changes of the SyncE CGU registers (E825C only) */
	struct mutex cgu_synce_lock;
	/* Serialize PFR recovery touching shared FW global state */
	struct mutex rebuild_lock;

	/* For serializing PHY port start/stop sequences */
	struct mutex ps_lock;

	struct ice_phc phc;

	struct rw_semaphore ctrl_pf_lock;
	struct ice_pf __rcu *ctrl_pf;
	struct ice_port_list ports;
};

struct ice_adapter *ice_adapter_get(struct pci_dev *pdev);
void ice_adapter_put(struct ice_adapter *adapter);

#endif /* _ICE_ADAPTER_H */
