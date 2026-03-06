/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 Jilay Sandeep Pandya
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INCLUDE_UTILS_FORMAT_TIME_H_
#define INCLUDE_UTILS_FORMAT_TIME_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/** Minimum buffer size required by format_uptime_hms(). */
#define FORMAT_UPTIME_HMS_LEN 13 /* "hh:mm:ss.mmm\0" */

/**
 * @brief Format millisecond uptime into "hh:mm:ss.mmm" in @p buf.
 *
 * @param buf       Output buffer (must be at least FORMAT_UPTIME_HMS_LEN bytes).
 * @param uptime_ms Uptime in milliseconds (from k_uptime_get()).
 */
static inline void format_uptime_hms(char buf[FORMAT_UPTIME_HMS_LEN], int64_t uptime_ms)
{
	uint32_t ms = (uint32_t)(uptime_ms % 1000);
	uint32_t s = (uint32_t)((uptime_ms / 1000) % 60);
	uint32_t m = (uint32_t)((uptime_ms / 60000) % 60);
	uint32_t h = (uint32_t)((uptime_ms / 3600000) % 100); /* clamp to 00-99 */

	snprintf(buf, FORMAT_UPTIME_HMS_LEN, "%02u:%02u:%02u.%03u", h, m, s, ms);
}

#endif /* INCLUDE_UTILS_FORMAT_TIME_H_ */
