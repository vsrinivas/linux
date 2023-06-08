// SPDX-License-Identifier: GPL-2.0-only

#include <linux/static_call.h>
#include <asm/msr.h>
#include <asm/virtext.h>

#include "vac.h"

#define VAC_X86_OP(func)					     \
	DEFINE_STATIC_CALL_NULL(vac_x86_##func,			     \
				*(((struct vac_x86_ops *)0)->func));
#define VAC_X86_OP_OPTIONAL VAC_X86_OP
#define VAC_X86_OP_OPTIONAL_RET0 VAC_X86_OP
#include "vac-x86-ops.h"

struct vac_x86_ops vac_x86_ops;
u32 __read_mostly kvm_uret_msrs_list[KVM_MAX_NR_USER_RETURN_MSRS];
EXPORT_SYMBOL(kvm_uret_msrs_list);
struct kvm_user_return_msrs __percpu *user_return_msrs;
EXPORT_SYMBOL(user_return_msrs);

u32 __read_mostly kvm_nr_uret_msrs;
EXPORT_SYMBOL(kvm_nr_uret_msrs);

static void kvm_on_user_return(struct user_return_notifier *urn)
{
	unsigned int slot;
	struct kvm_user_return_msrs *msrs
		= container_of(urn, struct kvm_user_return_msrs, urn);
	struct kvm_user_return_msr_values *values;
	unsigned long flags;

	/*
	 * Disabling irqs at this point since the following code could be
	 * interrupted and executed through kvm_arch_hardware_disable()
	 */
	local_irq_save(flags);
	if (msrs->registered) {
		msrs->registered = false;
		user_return_notifier_unregister(urn);
	}
	local_irq_restore(flags);
	for (slot = 0; slot < kvm_nr_uret_msrs; ++slot) {
		values = &msrs->values[slot];
		if (values->host != values->curr) {
			wrmsrl(kvm_uret_msrs_list[slot], values->host);
			values->curr = values->host;
		}
	}
}

void kvm_user_return_msr_cpu_online(void)
{
        unsigned int cpu = smp_processor_id();
        struct kvm_user_return_msrs *msrs = per_cpu_ptr(user_return_msrs, cpu);
        u64 value;
        int i;

        for (i = 0; i < kvm_nr_uret_msrs; ++i) {
                rdmsrl_safe(kvm_uret_msrs_list[i], &value);
                msrs->values[i].host = value;
                msrs->values[i].curr = value;
        }
}
EXPORT_SYMBOL_GPL(kvm_user_return_msr_cpu_online);

static inline void drop_user_return_notifiers(void)
{
	unsigned int cpu = smp_processor_id();
	struct kvm_user_return_msrs *msrs = per_cpu_ptr(user_return_msrs, cpu);

	if (msrs->registered)
		kvm_on_user_return(&msrs->urn);
}

static int kvm_probe_user_return_msr(u32 msr)
{
	u64 val;
	int ret;

	preempt_disable();
	ret = rdmsrl_safe(msr, &val);
	if (ret)
		goto out;
	ret = wrmsrl_safe(msr, val);
out:
	preempt_enable();
	return ret;
}

int kvm_add_user_return_msr(u32 msr)
{
	BUG_ON(kvm_nr_uret_msrs >= KVM_MAX_NR_USER_RETURN_MSRS);

	if (kvm_probe_user_return_msr(msr))
		return -1;

	kvm_uret_msrs_list[kvm_nr_uret_msrs] = msr;
	return kvm_nr_uret_msrs++;
}
EXPORT_SYMBOL(kvm_add_user_return_msr);

int kvm_find_user_return_msr(u32 msr)
{
	int i;

	for (i = 0; i < kvm_nr_uret_msrs; ++i) {
		if (kvm_uret_msrs_list[i] == msr)
			return i;
	}
	return -1;
}
EXPORT_SYMBOL(kvm_find_user_return_msr);

int kvm_set_user_return_msr(unsigned int slot, u64 value, u64 mask)
{
	unsigned int cpu = smp_processor_id();
	struct kvm_user_return_msrs *msrs = per_cpu_ptr(user_return_msrs, cpu);
	int err;

	value = (value & mask) | (msrs->values[slot].host & ~mask);
	if (value == msrs->values[slot].curr)
		return 0;
	err = wrmsrl_safe(kvm_uret_msrs_list[slot], value);
	if (err)
		return 1;

	msrs->values[slot].curr = value;
	if (!msrs->registered) {
		msrs->urn.on_user_return = kvm_on_user_return;
		user_return_notifier_register(&msrs->urn);
		msrs->registered = true;
	}
	return 0;
}
EXPORT_SYMBOL(kvm_set_user_return_msr);

int kvm_arch_hardware_enable(void)
{
	int ret;

	kvm_user_return_msr_cpu_online();

	ret = static_call(vac_x86_hardware_enable)();
	if (ret != 0)
		return ret;

	// TODO: Handle unstable TSC

	return 0;
}

void kvm_arch_hardware_disable(void)
{
	static_call(vac_x86_hardware_disable)();
	drop_user_return_notifiers();
}

void vac_x86_ops_init(struct vac_x86_ops *ops)
{
	memcpy(&vac_x86_ops, ops, sizeof(vac_x86_ops));
#define __VAC_X86_OP(func) \
		static_call_update(vac_x86_##func, vac_x86_ops.func);
#define VAC_X86_OP(func) \
		WARN_ON(!vac_x86_ops.func); __VAC_X86_OP(func)
#define VAC_X86_OP_OPTIONAL __VAC_X86_OP
#define VAC_X86_OP_OPTIONAL_RET0(func) \
		static_call_update(vac_x86_##func, (void *)vac_x86_ops.func ? : \
						   (void *)__static_call_return0);
#include "vac-x86-ops.h"
#undef __VAC_X86_OP
}

int __init vac_init(void)
{
#ifdef CONFIG_KVM_INTEL
	if (cpu_has_vmx())
		return vac_vmx_init();
#endif
#ifdef CONFIG_KVM_AMD
	if (cpu_has_svm(NULL))
		return vac_svm_init();
#endif
	return 0;
}

/*
 * Handle a fault on a hardware virtualization (VMX or SVM) instruction.
 *
 * Hardware virtualization extension instructions may fault if a reboot turns
 * off virtualization while processes are running.  Usually after catching the
 * fault we just panic; during reboot instead the instruction is ignored.
 */
noinstr void kvm_spurious_fault(void)
{
        /* Fault while not rebooting.  We want the trace. */
//        BUG_ON(!kvm_rebooting);	// XXX virt vac.c move
	BUG_ON(1);
}
EXPORT_SYMBOL(kvm_spurious_fault);
