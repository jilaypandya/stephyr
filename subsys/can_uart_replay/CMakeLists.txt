# SPDX-FileCopyrightText: Copyright (c) 2026 Jilay Sandeep Pandya
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_CAN_UART_REPLAY can_uart_replay.c)
zephyr_linker_sources_ifdef(CONFIG_CAN_UART_REPLAY ROM_SECTIONS can_uart_replay.ld)
