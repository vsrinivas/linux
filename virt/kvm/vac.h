/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __KVM_VAC_H__
#define __KVM_VAC_H__

#ifdef CONFIG_KVM_GENERIC_HARDWARE_ENABLING

#include <linux/kvm_host.h>
#include <linux/syscore_ops.h>

int kvm_online_cpu(unsigned int cpu);
int kvm_offline_cpu(unsigned int cpu);
void hardware_disable_all(void);
int hardware_enable_all(void);

extern struct notifier_block kvm_reboot_notifier;

extern struct syscore_ops kvm_syscore_ops;

#else /* CONFIG_KVM_GENERIC_HARDWARE_ENABLING */
static inline int hardware_enable_all(void)
{
	return 0;
}

static inline void hardware_disable_all(void)
{

}
#endif /* CONFIG_KVM_GENERIC_HARDWARE_ENABLING */

DECLARE_PER_CPU(cpumask_var_t, cpu_kick_mask);

#endif
