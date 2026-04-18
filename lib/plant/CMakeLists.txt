# SPDX-FileCopyrightText: Copyright (c) 2026 Jilay Sandeep Pandya
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_PLANT_MODEL_DC_MOTOR state_space_bldc_motor.c)
