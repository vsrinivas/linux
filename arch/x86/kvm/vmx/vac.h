// SPDX-License-Identifier: GPL-2.0-only

#include "../vac.h"

void vac_set_vmxarea(struct vmcs *vmcs, int cpu);
void vac_crash_vmclear_local_loaded_vmcss(void);
void vac_add_vmcs_to_loaded_vmcss_on_cpu(
		struct list_head *loaded_vmcss_on_cpu_link,
		int cpu);
void vac_loaded_vmcs_clear(struct loaded_vmcs *loaded_vmcs);
void vmx_hardware_disable(void);

struct vmcs *vac_get_vmxarea(int cpu);

