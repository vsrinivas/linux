// SPDX-License-Identifier: GPL-2.0-only

#include "../vac.h"


void vac_set_vmxarea(struct vmcs *vmcs, int cpu);

struct vmcs *vac_get_vmxarea(int cpu);
