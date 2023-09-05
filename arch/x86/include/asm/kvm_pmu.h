/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ASM_X86_KVM_PMU_H
#define _ASM_X86_KVM_PMU_H

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/mmu_notifier.h>
#include <linux/tracepoint.h>
#include <linux/cpumask.h>
#include <linux/irq_work.h>
#include <linux/irq.h>
#include <linux/workqueue.h>

#include <linux/kvm.h>
#include <linux/kvm_para.h>
#include <linux/kvm_types.h>
#include <linux/perf_event.h>
#include <linux/pvclock_gtod.h>
#include <linux/clocksource.h>
#include <linux/irqbypass.h>
#include <linux/hyperv.h>
#include <linux/kfifo.h>

#include <asm/apic.h>
#include <asm/pvclock-abi.h>
#include <asm/desc.h>
#include <asm/mtrr.h>
#include <asm/msr-index.h>
#include <asm/asm.h>
#include <asm/kvm_page_track.h>
#include <asm/kvm_vcpu_regs.h>
#include <asm/hyperv-tlfs.h>

enum pmc_type {
	KVM_PMC_GP = 0,
	KVM_PMC_FIXED,
};

struct kvm_pmc {
	enum pmc_type type;
	u8 idx;
	bool is_paused;
	bool intr;
	u64 counter;
	u64 prev_counter;
	u64 eventsel;
	struct perf_event *perf_event;
	struct kvm_vcpu *vcpu;
	/*
	 * only for creating or reusing perf_event,
	 * eventsel value for general purpose counters,
	 * ctrl value for fixed counters.
	 */
	u64 current_config;
};

/* More counters may conflict with other existing Architectural MSRs */
#define KVM_INTEL_PMC_MAX_GENERIC	8
#define MSR_ARCH_PERFMON_PERFCTR_MAX	(MSR_ARCH_PERFMON_PERFCTR0 + KVM_INTEL_PMC_MAX_GENERIC - 1)
#define MSR_ARCH_PERFMON_EVENTSEL_MAX	(MSR_ARCH_PERFMON_EVENTSEL0 + KVM_INTEL_PMC_MAX_GENERIC - 1)
#define KVM_PMC_MAX_FIXED	3
#define MSR_ARCH_PERFMON_FIXED_CTR_MAX	(MSR_ARCH_PERFMON_FIXED_CTR0 + KVM_PMC_MAX_FIXED - 1)
#define KVM_AMD_PMC_MAX_GENERIC	6
struct kvm_pmu {
	u8 version;
	unsigned nr_arch_gp_counters;
	unsigned nr_arch_fixed_counters;
	unsigned available_event_types;
	u64 fixed_ctr_ctrl;
	u64 fixed_ctr_ctrl_mask;
	u64 global_ctrl;
	u64 global_status;
	u64 counter_bitmask[2];
	u64 global_ctrl_mask;
	u64 global_ovf_ctrl_mask;
	u64 reserved_bits;
	u64 raw_event_mask;
	struct kvm_pmc gp_counters[KVM_INTEL_PMC_MAX_GENERIC];
	struct kvm_pmc fixed_counters[KVM_PMC_MAX_FIXED];
	struct irq_work irq_work;

	/*
	 * Overlay the bitmap with a 64-bit atomic so that all bits can be
	 * set in a single access, e.g. to reprogram all counters when the PMU
	 * filter changes.
	 */
	union {
		DECLARE_BITMAP(reprogram_pmi, X86_PMC_IDX_MAX);
		atomic64_t __reprogram_pmi;
	};
	DECLARE_BITMAP(all_valid_pmc_idx, X86_PMC_IDX_MAX);
	DECLARE_BITMAP(pmc_in_use, X86_PMC_IDX_MAX);

	u64 ds_area;
	u64 pebs_enable;
	u64 pebs_enable_mask;
	u64 pebs_data_cfg;
	u64 pebs_data_cfg_mask;

	/*
	 * If a guest counter is cross-mapped to host counter with different
	 * index, its PEBS capability will be temporarily disabled.
	 *
	 * The user should make sure that this mask is updated
	 * after disabling interrupts and before perf_guest_get_msrs();
	 */
	u64 host_cross_mapped_mask;

	/*
	 * The gate to release perf_events not marked in
	 * pmc_in_use only once in a vcpu time slice.
	 */
	bool need_cleanup;

	/*
	 * The total number of programmed perf_events and it helps to avoid
	 * redundant check before cleanup if guest don't use vPMU at all.
	 */
	u8 event_count;
};

#endif /* _ASM_X86_KVM_PMU_H */
