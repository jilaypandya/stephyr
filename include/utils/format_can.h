/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Jilay Sandeep Pandya
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INCLUDE_UTILS_FORMAT_CAN_H_
#define INCLUDE_UTILS_FORMAT_CAN_H_

#include <stddef.h>
#include <stdio.h>
#include <zephyr/drivers/can.h>

/**
 * @brief Format the body of a CAN frame RX log entry (no timestamp).
 *
 * Produces: "RX id=0x<id> dlc=<n> data=<b0> ... <b7>\r\n"
 *
 * Use this to build expected strings in tests or as the inner part of a full
 * log line via format_can_frame_line().
 *
 * @param buf    Output buffer.
 * @param buf_len Size of @p buf.
 * @param frame  CAN frame to format.
 */
static inline void format_can_frame_body(char *buf, size_t buf_len, const struct can_frame *frame)
{
	snprintf(buf, buf_len,
		 "RX id=0x%03x dlc=%u"
		 " data=%02x %02x %02x %02x %02x %02x %02x %02x\r\n",
		 frame->id, frame->dlc, frame->data[0], frame->data[1], frame->data[2],
		 frame->data[3], frame->data[4], frame->data[5], frame->data[6], frame->data[7]);
}

/**
 * @brief Format a CAN frame into a full timestamped UART output line.
 *
 * Produces: "[<timestamp>] RX id=0x<id> dlc=<n> data=<b0> ... <b7>\r\n"
 *
 * @param buf       Output buffer.
 * @param buf_len   Size of @p buf.
 * @param timestamp Null-terminated timestamp string (e.g. from format_uptime_hms()).
 * @param frame     CAN frame to format.
 */
static inline void format_can_frame_line(char *buf, size_t buf_len, const char *timestamp,
					 const struct can_frame *frame)
{
	snprintf(buf, buf_len,
		 "[%s] RX id=0x%03x dlc=%u"
		 " data=%02x %02x %02x %02x %02x %02x %02x %02x\r\n",
		 timestamp, frame->id, frame->dlc, frame->data[0], frame->data[1], frame->data[2],
		 frame->data[3], frame->data[4], frame->data[5], frame->data[6], frame->data[7]);
}

#endif /* INCLUDE_UTILS_FORMAT_CAN_H_ */
