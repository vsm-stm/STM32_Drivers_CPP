/**
 * @file i2c_timing_utility.hpp
 * @brief Compile-time TIMINGR calculation for STM32 I2C v2 (F7/G0).
 *
 * Adapted from STMicroelectronics i2c_timing_utility.c (MCD Application Team).
 *
 * @attention
 * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed by ST under BSD 3-Clause license,
 * the "License"; You may not use this file except in compliance with the
 * License. You may obtain a copy of the License at:
 *                        opensource.org/licenses/BSD-3-Clause
 *
 * Preserves the original nominal speeds, timing constraints and selection:
 * first valid SCLDEL/SDADEL for each PRESC, then minimum period error; ties
 * retain the first PRESC/SCLL/SCLH combination. Analog filter is enabled
 * (50..260 ns), digital filter DNF=0. Rise/fall assumptions are unchanged.
 * No workspace or search code is needed in the running firmware.
 */
#ifndef I2C_TIMING_UTILITY_HPP
#define I2C_TIMING_UTILITY_HPP

#include <cstdint>

namespace I2C_Timing {

/// Return a TIMINGR word, or zero when no supported configuration exists.
consteval uint32_t Calculate(uint32_t clock_hz, uint32_t speed_hz)
{
    struct Limits {
        uint32_t hz, min_hz, max_hz;
        uint32_t data_valid_max, data_setup_min, low_min, high_min, rise, fall;
    };
    constexpr Limits limits[] = {
        {100000, 80000, 120000, 3450, 250, 4700, 4000, 1000, 300},
        {400000, 320000, 480000, 900, 100, 1300, 600, 300, 300},
        {1000000, 800000, 1200000, 450, 50, 500, 260, 100, 100}
    };
    if (!clock_hz) return 0;
    const uint32_t index = speed_hz == 100000 ? 0 : speed_hz == 400000 ? 1
        : speed_hz == 1000000 ? 2 : 3;
    if (index == 3) return 0;
    const auto& spec = limits[index];

    // Integer nanoseconds, rounded as in the original ST calculation. Wide
    // arithmetic only runs in the compiler and prevents intermediate overflow.
    constexpr uint64_t ns_per_second = 1000000000ULL;
    constexpr uint64_t filter_min = 50, filter_max = 260;
    const uint64_t clock_ns = (ns_per_second + clock_hz / 2u) / clock_hz;
    if (!clock_ns) return 0;
    const uint64_t target = ns_per_second / spec.hz;
    const uint64_t period_min = ns_per_second / spec.max_hz;
    const uint64_t period_max = ns_per_second / spec.min_hz;
    const int64_t raw_sda_min = int64_t(spec.fall) - int64_t(filter_min + 3 * clock_ns);
    const int64_t raw_sda_max = int64_t(spec.data_valid_max)
        - int64_t(spec.rise + filter_max + 4 * clock_ns);
    const uint64_t sda_min = raw_sda_min > 0 ? uint64_t(raw_sda_min) : 0;
    const uint64_t sda_max = raw_sda_max > 0 ? uint64_t(raw_sda_max) : 0;
    const uint64_t scl_delay_min = spec.rise + spec.data_setup_min;
    const uint64_t base = filter_min + 2 * clock_ns; // DNF=0.
    const uint64_t edges = spec.rise + spec.fall;
    uint64_t best_error = target;
    uint32_t best = 0;

    for (uint32_t presc = 0; presc < 16; ++presc) {
        const uint64_t step = (presc + 1) * clock_ns;
        // The original loops retain the first valid delay fields per PRESC.
        // These rounded-up quotients select exactly that same pair.
        const uint64_t scl_ticks = (scl_delay_min + step - 1) / step;
        const uint64_t sdadel = (sda_min + step - 1) / step;
        if (!scl_ticks || scl_ticks > 16 || sdadel > 15 || sdadel * step > sda_max)
            continue;
        const uint32_t delay_word = (presc << 28)
            | (uint32_t(scl_ticks - 1) << 20) | (uint32_t(sdadel) << 16);

        for (uint32_t scll = 0; scll < 256; ++scll) {
            const uint64_t low = base + (scll + 1) * step;
            // Even SCLH=0 is too slow. Larger SCLL cannot improve it.
            if (low + base + step + edges > period_max) break;
            if (low <= spec.low_min || clock_ns >= (low - filter_min) / 4)
                continue;
            for (uint32_t sclh = 0; sclh < 256; ++sclh) {
                const uint64_t high = base + (sclh + 1) * step;
                const uint64_t period = low + high + edges;
                if (period > period_max) break;
                if (period < period_min || high < spec.high_min || clock_ns >= high)
                    continue;
                const uint64_t error = period > target ? period - target : target - period;
                if (error < best_error) {
                    best_error = error;
                    best = delay_word | (sclh << 8) | scll;
                }
            }
        }
    }
    return best;
}

} // namespace I2C_Timing
#endif // I2C_TIMING_UTILITY_HPP
