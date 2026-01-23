/*
 * Copyright (c) 2024 Jilay Sandeep Pandya
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef APP_DRIVERS_SCALER_H_
#define APP_DRIVERS_SCALER_H_

#include <zephyr/device.h>

struct scaler_config {
    int scaling_factor;
};

__subsystem struct scaler_driver_api {
    int (*scaler_set)(const struct device *dev, uint8_t register, uint8_t value);
    int (*scaler_get)(const struct device *dev, uint8_t register, uint8_t *value);
};

static inline int scaler_set(const struct device *dev, uint8_t register_addr, uint8_t value)
{
    const struct scaler_driver_api *api =
        (const struct scaler_driver_api *)dev->api;

    return api->scaler_set(dev, register_addr, value);
}

static inline int scaler_get(const struct device *dev, uint8_t register_addr, uint8_t *value)
{
    const struct scaler_driver_api *api =
        (const struct scaler_driver_api *)dev->api;

    return api->scaler_get(dev, register_addr, value);
}

#endif /* APP_DRIVERS_SCALER_H_ */