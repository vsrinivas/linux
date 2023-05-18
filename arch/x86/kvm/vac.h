/* SPDX-License-Identifier: GPL-2.0 */

#ifndef ARCH_X86_KVM_VAC_H
#define ARCH_X86_KVM_VAC_H

#include <linux/user-return-notifier.h>

/*
 * Restoring the host value for MSRs that are only consumed when running in
 * usermode, e.g. SYSCALL MSRs and TSC_AUX, can be deferred until the CPU
 * returns to userspace, i.e. the kernel can run with the guest's value.
 */
#define KVM_MAX_NR_USER_RETURN_MSRS 16

struct kvm_user_return_msrs {
	struct user_return_notifier urn;
	bool registered;
	struct kvm_user_return_msr_values {
		u64 host;
		u64 curr;
	} values[KVM_MAX_NR_USER_RETURN_MSRS];
};
extern struct kvm_user_return_msrs __percpu *user_return_msrs;

extern u32 __read_mostly kvm_nr_uret_msrs;
extern u32 __read_mostly kvm_uret_msrs_list[KVM_MAX_NR_USER_RETURN_MSRS];

int kvm_add_user_return_msr(u32 msr);
int kvm_find_user_return_msr(u32 msr);
int kvm_set_user_return_msr(unsigned int slot, u64 value, u64 mask);

static inline bool kvm_is_supported_user_return_msr(u32 msr)
{
	return kvm_find_user_return_msr(msr) >= 0;
}

#endif // ARCH_X86_KVM_VAC_H