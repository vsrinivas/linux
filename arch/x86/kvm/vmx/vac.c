// SPDX-License-Identifier: GPL-2.0-only

#include <linux/percpu-defs.h>
#include <asm/virtext.h>
#include "hyperv.h"
#include "vmx.h"

void vmclear_error(struct vmcs *vmcs, u64 phys_addr) {} // XXX Vac

static DEFINE_PER_CPU(struct vmcs *, vmxarea);
/*
 * We maintain a per-CPU linked-list of VMCS loaded on that CPU. This is needed
 * when a CPU is brought down, and we need to VMCLEAR all VMCSs loaded on it.
 */
static DEFINE_PER_CPU(struct list_head, loaded_vmcss_on_cpu);

DEFINE_PER_CPU(struct vmcs *, current_vmcs);
EXPORT_SYMBOL(current_vmcs);

void vac_set_vmxarea(struct vmcs *vmcs, int cpu) {
	per_cpu(vmxarea, cpu) = vmcs;
}
EXPORT_SYMBOL_GPL(vac_set_vmxarea);

struct vmcs *vac_get_vmxarea(int cpu) {
	return per_cpu(vmxarea, cpu);
}
EXPORT_SYMBOL_GPL(vac_get_vmxarea);

#ifdef CONFIG_KEXEC_CORE
void vac_crash_vmclear_local_loaded_vmcss(void)
{
	int cpu = raw_smp_processor_id();
	struct loaded_vmcs *v;

	list_for_each_entry(v, &per_cpu(loaded_vmcss_on_cpu, cpu),
			    loaded_vmcss_on_cpu_link)
		vmcs_clear(v->vmcs);
}
EXPORT_SYMBOL_GPL(vac_crash_vmclear_local_loaded_vmcss);
#endif /* CONFIG_KEXEC_CORE */

void vac_add_vmcs_to_loaded_vmcss_on_cpu(
		struct list_head *loaded_vmcss_on_cpu_link,
		int cpu)
{
	list_add(loaded_vmcss_on_cpu_link, &per_cpu(loaded_vmcss_on_cpu, cpu));
}
EXPORT_SYMBOL(vac_add_vmcs_to_loaded_vmcss_on_cpu);

static void __loaded_vmcs_clear(void *arg)
{
	struct loaded_vmcs *loaded_vmcs = arg;
	int cpu = raw_smp_processor_id();

	if (loaded_vmcs->cpu != cpu)
		return; /* vcpu migration can race with cpu offline */
	if (per_cpu(current_vmcs, cpu) == loaded_vmcs->vmcs)
		per_cpu(current_vmcs, cpu) = NULL;

	vmcs_clear(loaded_vmcs->vmcs);
	if (loaded_vmcs->shadow_vmcs && loaded_vmcs->launched)
		vmcs_clear(loaded_vmcs->shadow_vmcs);

	list_del(&loaded_vmcs->loaded_vmcss_on_cpu_link);

	/*
	 * Ensure all writes to loaded_vmcs, including deleting it from its
	 * current percpu list, complete before setting loaded_vmcs->cpu to
	 * -1, otherwise a different cpu can see loaded_vmcs->cpu == -1 first
	 * and add loaded_vmcs to its percpu list before it's deleted from this
	 * cpu's list. Pairs with the smp_rmb() in vmx_vcpu_load_vmcs().
	 */
	smp_wmb();

	loaded_vmcs->cpu = -1;
	loaded_vmcs->launched = 0;
}

void vac_loaded_vmcs_clear(struct loaded_vmcs *loaded_vmcs)
{
	int cpu = loaded_vmcs->cpu;

	if (cpu != -1)
		smp_call_function_single(cpu,
			 __loaded_vmcs_clear, loaded_vmcs, 1);
}
EXPORT_SYMBOL_GPL(vac_loaded_vmcs_clear);

static void vmclear_local_loaded_vmcss(void)
{
	int cpu = raw_smp_processor_id();
	struct loaded_vmcs *v, *n;

	list_for_each_entry_safe(v, n, &per_cpu(loaded_vmcss_on_cpu, cpu),
				 loaded_vmcss_on_cpu_link)
		__loaded_vmcs_clear(v);
}

#if IS_ENABLED(CONFIG_HYPERV)
static void hv_reset_evmcs(void)
{
	struct hv_vp_assist_page *vp_ap;

	if (!kvm_is_using_evmcs())
		return;

	/*
	 * KVM should enable eVMCS if and only if all CPUs have a VP assist
	 * page, and should reject CPU onlining if eVMCS is enabled the CPU
	 * doesn't have a VP assist page allocated.
	 */
	vp_ap = hv_get_vp_assist_page(smp_processor_id());
	if (WARN_ON_ONCE(!vp_ap))
		return;

	/*
	 * Reset everything to support using non-enlightened VMCS access later
	 * (e.g. when we reload the module with enlightened_vmcs=0)
	 */
	vp_ap->nested_control.features.directhypercall = 0;
	vp_ap->current_nested_vmcs = 0;
	vp_ap->enlighten_vmentry = 0;
}

#else
static void hv_reset_evmcs(void) {}
#endif

static int kvm_cpu_vmxon(u64 vmxon_pointer)
{
	u64 msr;

	cr4_set_bits(X86_CR4_VMXE);

	asm_volatile_goto("1: vmxon %[vmxon_pointer]\n\t"
			  _ASM_EXTABLE(1b, %l[fault])
			  : : [vmxon_pointer] "m"(vmxon_pointer)
			  : : fault);
	return 0;

fault:
	WARN_ONCE(1, "VMXON faulted, MSR_IA32_FEAT_CTL (0x3a) = 0x%llx\n",
		  rdmsrl_safe(MSR_IA32_FEAT_CTL, &msr) ? 0xdeadbeef : msr);
	cr4_clear_bits(X86_CR4_VMXE);

	return -EFAULT;
}

int vmx_hardware_enable(void)
{
	int cpu = raw_smp_processor_id();
	u64 phys_addr = __pa(vac_get_vmxarea(cpu));
	int r;

	if (cr4_read_shadow() & X86_CR4_VMXE)
		return -EBUSY;

	/*
	 * This can happen if we hot-added a CPU but failed to allocate
	 * VP assist page for it.
	 */
	if (kvm_is_using_evmcs() && !hv_get_vp_assist_page(cpu))
		return -EFAULT;

	intel_pt_handle_vmx(1);

	r = kvm_cpu_vmxon(phys_addr);
	if (r) {
		intel_pt_handle_vmx(0);
		return r;
	}

//	if (enable_ept)			// XXX: VAC: Pending whether ept enable should be (why needed?) into vac
		ept_sync_global();

	return 0;
}
EXPORT_SYMBOL(vmx_hardware_enable);

void vmx_hardware_disable(void)
{
	vmclear_local_loaded_vmcss();

	if (cpu_vmxoff()) {
		kvm_spurious_fault();
	}

	hv_reset_evmcs();

	intel_pt_handle_vmx(0);
}
EXPORT_SYMBOL(vmx_hardware_disable);

static DECLARE_BITMAP(vmx_vpid_bitmap, VMX_NR_VPIDS);
static DEFINE_SPINLOCK(vmx_vpid_lock);

int vac_vmx_init(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		INIT_LIST_HEAD(&per_cpu(loaded_vmcss_on_cpu, cpu));
		//pi_init_cpu(cpu);
		// XXX: Pending moving the posted interrupt list into VAC 
	}

        set_bit(0, vmx_vpid_bitmap); /* 0 is reserved for host */

	return 0;
}

int allocate_vpid(void)
{
        int vpid;

        spin_lock(&vmx_vpid_lock);
        vpid = find_first_zero_bit(vmx_vpid_bitmap, VMX_NR_VPIDS);
        if (vpid < VMX_NR_VPIDS)
                __set_bit(vpid, vmx_vpid_bitmap);
        else
                vpid = 0;
        spin_unlock(&vmx_vpid_lock);
        return vpid;
}
EXPORT_SYMBOL_GPL(allocate_vpid);

void free_vpid(int vpid)
{
        if (vpid == 0)
                return;
        spin_lock(&vmx_vpid_lock);
        __clear_bit(vpid, vmx_vpid_bitmap);
        spin_unlock(&vmx_vpid_lock);
}
EXPORT_SYMBOL_GPL(free_vpid);
