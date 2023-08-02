// SPDX-License-Identifier: GPL-2.0-only

#include <asm/virtext.h>
#include <linux/percpu-defs.h>

#include "vac.h"
#include "svm.h"

static bool erratum_383_found __read_mostly;

unsigned int max_sev_asid;
EXPORT_SYMBOL_GPL(max_sev_asid);

DEFINE_PER_CPU(struct svm_cpu_data, svm_data);
EXPORT_SYMBOL(svm_data);

bool is_erratum_383(void)
{
	int err, i;
	u64 value;

	if (!erratum_383_found)
		return false;

	value = native_read_msr_safe(MSR_IA32_MC0_STATUS, &err);
	if (err)
		return false;

	/* Bit 62 may or may not be set for this mce */
	value &= ~(1ULL << 62);

	if (value != 0xb600000000010015ULL)
		return false;

	/* Clear MCi_STATUS registers */
	for (i = 0; i < 6; ++i)
		native_write_msr_safe(MSR_IA32_MCx_STATUS(i), 0, 0);

	value = native_read_msr_safe(MSR_IA32_MCG_STATUS, &err);
	if (!err) {
		u32 low, high;

		value &= ~(1ULL << 2);
		low    = lower_32_bits(value);
		high   = upper_32_bits(value);

		native_write_msr_safe(MSR_IA32_MCG_STATUS, low, high);
	}

	/* Flush tlb to evict multi-match entries */
	__flush_tlb_all();

	return true;
}
EXPORT_SYMBOL_GPL(is_erratum_383);

static void svm_init_erratum_383(void)
{
	u32 low, high;
	int err;
	u64 val;

	if (!static_cpu_has_bug(X86_BUG_AMD_TLB_MMATCH))
		return;

	/* Use _safe variants to not break nested virtualization */
	val = native_read_msr_safe(MSR_AMD64_DC_CFG, &err);
	if (err)
		return;

	val |= (1ULL << 47);

	low  = lower_32_bits(val);
	high = upper_32_bits(val);

	native_write_msr_safe(MSR_AMD64_DC_CFG, low, high);

	erratum_383_found = true;
}

int svm_hardware_enable(void)
{

	struct svm_cpu_data *sd;
	uint64_t efer;
	struct desc_struct *gdt;
	int me = raw_smp_processor_id();

	rdmsrl(MSR_EFER, efer);
	if (efer & EFER_SVME)
		return -EBUSY;

	sd = per_cpu_ptr(&svm_data, me);
	memset(sd, 0, sizeof(struct svm_cpu_data));
	sd->asid_generation = 1;
	sd->max_asid = cpuid_ebx(SVM_CPUID_FUNC) - 1;
	sd->next_asid = sd->max_asid + 1;
	sd->min_asid = max_sev_asid + 1;
	sd->save_area = alloc_page(GFP_ATOMIC | __GFP_ZERO);
	sd->save_area_pa = __sme_page_pa(sd->save_area);

	gdt = get_current_gdt_rw();
	sd->tss_desc = (struct kvm_ldttss_desc *)(gdt + GDT_ENTRY_TSS);

	wrmsrl(MSR_EFER, efer | EFER_SVME);

	wrmsrl(MSR_VM_HSAVE_PA, sd->save_area_pa);

	if (static_cpu_has(X86_FEATURE_TSCRATEMSR)) {
		/*
		 * Set the default value, even if we don't use TSC scaling
		 * to avoid having stale value in the msr
		 */
		// TODO: Fix this
		//__svm_write_tsc_multiplier(SVM_TSC_RATIO_DEFAULT);
	}


	/*
	 * Get OSVW bits.
	 *
	 * Note that it is possible to have a system with mixed processor
	 * revisions and therefore different OSVW bits. If bits are not the same
	 * on different processors then choose the worst case (i.e. if erratum
	 * is present on one processor and not on another then assume that the
	 * erratum is present everywhere).
	 */
	if (cpu_has(&boot_cpu_data, X86_FEATURE_OSVW)) {
		uint64_t len, status = 0;
		int err;

		len = native_read_msr_safe(MSR_AMD64_OSVW_ID_LENGTH, &err);
		if (!err)
			status = native_read_msr_safe(MSR_AMD64_OSVW_STATUS,
						      &err);

		if (err)
			osvw_status = osvw_len = 0;
		else {
			if (len < osvw_len)
				osvw_len = len;
			osvw_status |= status;
			osvw_status &= (1ULL << osvw_len) - 1;
		}
	} else
		osvw_status = osvw_len = 0;

	svm_init_erratum_383();

	amd_pmu_enable_virt();

	return 0;
}
EXPORT_SYMBOL_GPL(svm_hardware_enable);

void svm_hardware_disable(void)
{
	struct svm_cpu_data *sd;
	int me = raw_smp_processor_id();

	/* Make sure we clean up behind us */
	if (tsc_scaling)
		// TODO: Fix this
		//__svm_write_tsc_multiplier(SVM_TSC_RATIO_DEFAULT);

	cpu_svm_disable();

	sd = per_cpu_ptr(&svm_data, me);
	__free_page(sd->save_area);
	sd->save_area_pa = 0;
	sd->save_area = NULL;

	amd_pmu_disable_virt();
}
EXPORT_SYMBOL_GPL(svm_hardware_disable);

int __init vac_svm_init(void)
{
	return 0;
}

void vac_svm_exit(void)
{
}
