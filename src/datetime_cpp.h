// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024 S4 Snapshot Developers

#ifndef DATETIME_CPP_H
#define DATETIME_CPP_H

#include <cstdint>
#include <string>
#include <functional>

namespace DateTimeCpp {

/**
 * @brief Format elapsed time in milliseconds as "hh:mm:ss"
 *
 * Mimics Qt's QTime(0, 0).addMSecs(ms).toString("hh:mm:ss") behavior.
 *
 * Key behaviors:
 * - Wraps around at 24 hours (86400000 ms) using modulo operation
 * - Always formats as "hh:mm:ss" with 2-digit zero-padding
 * - Negative values wrap from the end of the day
 * - Examples:
 *   - 0 ms → "00:00:00"
 *   - 3661000 ms (1h 1m 1s) → "01:01:01"
 *   - 86400000 ms (24h) → "00:00:00" (wraps)
 *   - 90061000 ms (25h 1m 1s) → "01:01:01" (wraps)
 *
 * @param milliseconds Elapsed time in milliseconds
 * @return Formatted time string "hh:mm:ss"
 */
std::string formatElapsedTime(std::int64_t milliseconds);

/**
 * @brief Format timestamp as local time "yyyyMMdd_HHmm"
 *
 * Mimics Qt's QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC).toString("yyyyMMdd_HHmm")
 *
 * @param millisecondsSinceEpoch Milliseconds since Unix epoch (1970-01-01 00:00:00 UTC)
 * @return Formatted date-time string "yyyyMMdd_HHmm" in local timezone
 */
std::string formatLocalYmdHm(std::int64_t millisecondsSinceEpoch);

/**
 * @brief Get current local time formatted as "yyyyMMdd_HHmm"
 *
 * Convenience function that calls formatLocalYmdHm() with current time.
 *
 * @return Current local time formatted as "yyyyMMdd_HHmm"
 */
std::string nowLocalYmdHm();

/**
 * @brief Format timestamp as local time "yyyy-MM-dd hh:mm:ss.zzz "
 *
 * Mimics Qt's QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC).toString("yyyy-MM-dd hh:mm:ss.zzz ")
 * Note: Includes trailing space to match Qt behavior
 *
 * @param millisecondsSinceEpoch Milliseconds since Unix epoch (1970-01-01 00:00:00 UTC)
 * @return Formatted date-time string "yyyy-MM-dd hh:mm:ss.zzz " in local timezone
 */
std::string formatLocalYmdHmsMillis(std::int64_t millisecondsSinceEpoch);

/**
 * @brief Get current local time formatted as "yyyy-MM-dd hh:mm:ss.zzz "
 *
 * Convenience function that calls formatLocalYmdHmsMillis() with current time.
 *
 * @return Current local time formatted as "yyyy-MM-dd hh:mm:ss.zzz "
 */
std::string nowLocalYmdHmsMillis();

#ifdef UNIT_TESTS
    struct Hooks {
        std::function<std::string()> nowLocalYmdHm;
    };

    void setHooksForTests(Hooks *hooks);
#endif

} // namespace DateTimeCpp

#endif // DATETIME_CPP_H

// Made with Bob
