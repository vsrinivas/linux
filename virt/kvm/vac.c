// SPDX-License-Identifier: GPL-2.0-only

#include "vac.h"

#include <linux/cpu.h>
#include <linux/percpu.h>
#include <linux/mutex.h>

DEFINE_PER_CPU(cpumask_var_t, cpu_kick_mask);
EXPORT_SYMBOL(cpu_kick_mask);

DEFINE_PER_CPU(struct kvm_vcpu *, kvm_running_vcpu);

#ifdef CONFIG_KVM_GENERIC_HARDWARE_ENABLING
DEFINE_MUTEX(vac_lock);

__visible bool kvm_rebooting;
EXPORT_SYMBOL_GPL(kvm_rebooting);

static DEFINE_PER_CPU(bool, hardware_enabled);
static int kvm_usage_count;

static int __hardware_enable_nolock(void)
{
	if (__this_cpu_read(hardware_enabled))
		return 0;

	if (kvm_arch_hardware_enable()) {
		pr_info("kvm: enabling virtualization on CPU%d failed\n",
			raw_smp_processor_id());
		return -EIO;
	}

	__this_cpu_write(hardware_enabled, true);
	return 0;
}

static void hardware_enable_nolock(void *failed)
{
	if (__hardware_enable_nolock())
		atomic_inc(failed);
}

int kvm_online_cpu(unsigned int cpu)
{
	int ret = 0;

	/*
	 * Abort the CPU online process if hardware virtualization cannot
	 * be enabled. Otherwise running VMs would encounter unrecoverable
	 * errors when scheduled to this CPU.
	 */
	mutex_lock(&vac_lock);
	if (kvm_usage_count)
		ret = __hardware_enable_nolock();
	mutex_unlock(&vac_lock);
	return ret;
}

static void hardware_disable_nolock(void *junk)
{
	/*
	 * Note, hardware_disable_all_nolock() tells all online CPUs to disable
	 * hardware, not just CPUs that successfully enabled hardware!
	 */
	if (!__this_cpu_read(hardware_enabled))
		return;

	kvm_arch_hardware_disable();

	__this_cpu_write(hardware_enabled, false);
}

int kvm_offline_cpu(unsigned int cpu)
{
	mutex_lock(&vac_lock);
	if (kvm_usage_count)
		hardware_disable_nolock(NULL);
	mutex_unlock(&vac_lock);
	return 0;
}

static void hardware_disable_all_nolock(void)
{
	BUG_ON(!kvm_usage_count);

	kvm_usage_count--;
	if (!kvm_usage_count)
		on_each_cpu(hardware_disable_nolock, NULL, 1);
}

void hardware_disable_all(void)
{
	cpus_read_lock();
	mutex_lock(&vac_lock);
	hardware_disable_all_nolock();
	mutex_unlock(&vac_lock);
	cpus_read_unlock();
}

int hardware_enable_all(void)
{
	atomic_t failed = ATOMIC_INIT(0);
	int r = 0;

	/*
	 * When onlining a CPU, cpu_online_mask is set before kvm_online_cpu()
	 * is called, and so on_each_cpu() between them includes the CPU that
	 * is being onlined.  As a result, hardware_enable_nolock() may get
	 * invoked before kvm_online_cpu(), which also enables hardware if the
	 * usage count is non-zero.  Disable CPU hotplug to avoid attempting to
	 * enable hardware multiple times.
	 */
	cpus_read_lock();
	mutex_lock(&vac_lock);

	kvm_usage_count++;
	if (kvm_usage_count == 1) {
		on_each_cpu(hardware_enable_nolock, &failed, 1);

		if (atomic_read(&failed)) {
			hardware_disable_all_nolock();
			r = -EBUSY;
		}
	}

	mutex_unlock(&vac_lock);
	cpus_read_unlock();

	return r;
}

static int kvm_reboot(struct notifier_block *notifier, unsigned long val,
		      void *v)
{
	/*
	 * Some (well, at least mine) BIOSes hang on reboot if
	 * in vmx root mode.
	 *
	 * And Intel TXT required VMX off for all cpu when system shutdown.
	 */
	pr_info("kvm: exiting hardware virtualization\n");
	kvm_rebooting = true;
	on_each_cpu(hardware_disable_nolock, NULL, 1);
	return NOTIFY_OK;
}

static int kvm_suspend(void)
{
	/*
	 * Secondary CPUs and CPU hotplug are disabled across the suspend/resume
	 * callbacks, i.e. no need to acquire vac_lock to ensure the usage count
	 * is stable.  Assert that vac_lock is not held to ensure the system
	 * isn't suspended while KVM is enabling hardware.  Hardware enabling
	 * can be preempted, but the task cannot be frozen until it has dropped
	 * all locks (userspace tasks are frozen via a fake signal).
	 */
	lockdep_assert_not_held(&vac_lock);
	lockdep_assert_irqs_disabled();

	if (kvm_usage_count)
		hardware_disable_nolock(NULL);
	return 0;
}

static void kvm_resume(void)
{
	lockdep_assert_not_held(&vac_lock);
	lockdep_assert_irqs_disabled();

	if (kvm_usage_count)
		WARN_ON_ONCE(__hardware_enable_nolock());
}

struct notifier_block kvm_reboot_notifier = {
	.notifier_call = kvm_reboot,
	.priority = 0,
};

struct syscore_ops kvm_syscore_ops = {
	.suspend = kvm_suspend,
	.resume = kvm_resume,
};

#endif
