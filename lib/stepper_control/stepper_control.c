#include <app/lib/stepper_control.h>
#include <zephyr/kernel.h>

uint64_t timeout_to_ns(k_timeout_t timeout) {
  uint64_t total_ns;

  // Handle special timeout values
  if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {
    return UINT64_MAX;  // Infinite timeout
  }

  if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
    return 0;  // No wait timeout
  }

  // Extract tick count from k_timeout_t
  k_ticks_t ticks = timeout.ticks;

  // Use clock constants to convert ticks to ns
  // NSEC_PER_SEC and CONFIG_SYS_CLOCK_TICKS_PER_SEC are defined in Zephyr
  total_ns = ((uint64_t)ticks * NSEC_PER_SEC) / CONFIG_SYS_CLOCK_TICKS_PER_SEC;

  return total_ns;
}

int stepper_control_move_to(const struct device *dev, int32_t micro_steps, k_timeout_t timeout) {
  /* calculate micro-step interval based on timeout and micro_steps */
  if (micro_steps == 0) {
    return 0;
  }

  uint64_t total_ns;

  if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {
    // Handle infinite timeout case
    total_ns = UINT64_MAX;
  } else if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
    // Handle no wait case
    total_ns = 0;
  } else {
    // Convert timeout to nanoseconds
    k_ticks_t ticks = k_timeout_to_ticks(timeout);
    total_ns = k_ticks_to_ns_floor64(ticks);
  }

  uint64_t microstep_interval_ns = (total_ns / (uint64_t)(abs(micro_steps)));
  stepper_set_microstep_interval(dev, microstep_interval_ns);
  stepper_move_to(dev, micro_steps);
  k_poll_event_init();
  return 0;
}