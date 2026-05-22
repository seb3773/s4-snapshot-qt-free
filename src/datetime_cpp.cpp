// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024 S4 Snapshot Developers

#include "datetime_cpp.h"
#include <iomanip>
#include <sstream>
#include <ctime>
#include <chrono>

namespace DateTimeCpp {

// Constants matching Qt's internal values
constexpr std::int64_t MSECS_PER_SEC = 1000;
constexpr std::int64_t MSECS_PER_MIN = 60000;      // 60 * 1000
constexpr std::int64_t MSECS_PER_HOUR = 3600000;   // 60 * 60 * 1000
constexpr std::int64_t MSECS_PER_DAY = 86400000;   // 24 * 60 * 60 * 1000

#ifdef UNIT_TESTS
static Hooks *g_hooks = nullptr;

void setHooksForTests(Hooks *hooks) {
    g_hooks = hooks;
}
#endif

std::string formatElapsedTime(std::int64_t milliseconds) {
    // Mimic Qt's QTime behavior: wrap around at 24 hours using modulo
    // QTime::addMSecs uses: QRoundingDown::qMod<MSECS_PER_DAY>(ds() + ms)
    // For positive values, this is equivalent to: (milliseconds % MSECS_PER_DAY)
    // For negative values, we need to handle wrapping correctly
    
    // Normalize to 0-86399999 range (0 to 23:59:59.999)
    std::int64_t normalized = milliseconds % MSECS_PER_DAY;
    if (normalized < 0) {
        normalized += MSECS_PER_DAY;
    }
    
    // Extract hours, minutes, seconds
    // Matching Qt's extraction logic from QTime::hour(), minute(), second()
    int hours = static_cast<int>(normalized / MSECS_PER_HOUR);
    int minutes = static_cast<int>((normalized % MSECS_PER_HOUR) / MSECS_PER_MIN);
    int seconds = static_cast<int>((normalized / MSECS_PER_SEC) % 60);
    
    // Format as "hh:mm:ss" with zero-padding
    // Matching Qt's QString::asprintf("%02d:%02d:%02d", hour(), minute(), second())
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hours << ':'
        << std::setfill('0') << std::setw(2) << minutes << ':'
        << std::setfill('0') << std::setw(2) << seconds;
    
    return oss.str();
}

std::string formatLocalYmdHm(std::int64_t millisecondsSinceEpoch) {
#ifdef UNIT_TESTS
    if (g_hooks && g_hooks->nowLocalYmdHm) {
        return g_hooks->nowLocalYmdHm();
    }
#endif

    // Convert milliseconds to seconds (time_t)
    std::time_t seconds = static_cast<std::time_t>(millisecondsSinceEpoch / 1000);
    
    // Convert to local time
    std::tm *localTime = std::localtime(&seconds);
    if (!localTime) {
        return "19700101_0000"; // Fallback for invalid time
    }
    
    // Format as "yyyyMMdd_HHmm"
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << (1900 + localTime->tm_year)
        << std::setw(2) << (1 + localTime->tm_mon)
        << std::setw(2) << localTime->tm_mday
        << '_'
        << std::setw(2) << localTime->tm_hour
        << std::setw(2) << localTime->tm_min;
    
    return oss.str();
}

std::string formatLocalYmdHmsMillis(std::int64_t millisecondsSinceEpoch) {
    // Convert milliseconds to seconds (time_t)
    std::time_t seconds = static_cast<std::time_t>(millisecondsSinceEpoch / 1000);
    int millis = static_cast<int>(millisecondsSinceEpoch % 1000);
    if (millis < 0) {
        millis += 1000;
        seconds -= 1;
    }
    
    // Convert to local time
    std::tm *localTime = std::localtime(&seconds);
    if (!localTime) {
        return "1970-01-01 00:00:00.000 "; // Fallback for invalid time
    }
    
    // Format as "yyyy-MM-dd hh:mm:ss.zzz " (note trailing space)
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << (1900 + localTime->tm_year) << '-'
        << std::setw(2) << (1 + localTime->tm_mon) << '-'
        << std::setw(2) << localTime->tm_mday << ' '
        << std::setw(2) << localTime->tm_hour << ':'
        << std::setw(2) << localTime->tm_min << ':'
        << std::setw(2) << localTime->tm_sec << '.'
        << std::setw(3) << millis << ' ';
    
    return oss.str();
}

std::string nowLocalYmdHm() {
    // Get current time in milliseconds since epoch
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    
    return formatLocalYmdHm(millis);
}

std::string nowLocalYmdHmsMillis() {
    // Get current time in milliseconds since epoch
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    
    return formatLocalYmdHmsMillis(millis);
}

} // namespace DateTimeCpp

// Made with Bob
