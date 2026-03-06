/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Jilay Sandeep Pandya
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Linker fragment for the CAN UART replay subsystem.
 * Places all STRUCT_SECTION_ITERABLE(can_id_handler, ...) entries into a
 * contiguous ROM section so STRUCT_SECTION_FOREACH() can walk them.
 */

#include <zephyr/linker/iterable_sections.h>

ITERABLE_SECTION_ROM(can_id_handler, Z_LINK_ITERABLE_SUBALIGN)
