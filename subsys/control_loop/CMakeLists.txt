# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_CONTROL_LOOP control_loop.c)
zephyr_library_sources_ifdef(CONFIG_CONTROL_LOOP control_loop_event_handlers.c)
