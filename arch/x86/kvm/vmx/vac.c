// SPDX-License-Identifier: GPL-2.0-only

#include <linux/percpu-defs.h>
#include "vmx.h"

static DEFINE_PER_CPU(struct vmcs *, vmxarea);

void vac_set_vmxarea(struct vmcs *vmcs, int cpu) {
	per_cpu(vmxarea, cpu) = vmcs;
}

struct vmcs *vac_get_vmxarea(int cpu) {
	return per_cpu(vmxarea, cpu);
}
