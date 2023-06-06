// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>

#include "../vac.h"

extern unsigned int max_sev_asid;

static int tsc_scaling = true;
module_param(tsc_scaling, int, 0444);

/*
 * Set osvw_len to higher value when updated Revision Guides
 * are published and we know what the new status bits are
 */
static uint64_t osvw_len = 4, osvw_status;

bool is_erratum_383(void);
int svm_hardware_enable(void);
void svm_hardware_disable(void);
