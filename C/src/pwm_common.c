#include <stdint.h>
#include <time.h>

#include "pwm_common.h"
#include "gpio.h"
#include "motordriver.h"

uint64_t get_pwm_timestamp()
{
  struct timespec t;
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &t) != 0) {
    return 0;
  }
  return (uint64_t) t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

