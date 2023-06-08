/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(VAC_X86_OP) || !defined(VAC_X86_OP_OPTIONAL)
BUILD_BUG_ON(1)
#endif

VAC_X86_OP(hardware_enable);
VAC_X86_OP(hardware_disable);

#undef VAC_X86_OP
#undef VAC_X86_OP_OPTIONAL
#undef VAC_X86_OP_OPTIONAL_RET0
