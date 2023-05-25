// SPDX-License-Identifier: GPL-2.0-only

#include <linux/percpu-defs.h>
#include "svm.h"

DEFINE_PER_CPU(struct svm_cpu_data, svm_data);
EXPORT_SYMBOL(svm_data);
