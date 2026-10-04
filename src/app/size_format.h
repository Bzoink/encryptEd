#ifndef ENCRYPTED_APP_SIZE_FORMAT_H
#define ENCRYPTED_APP_SIZE_FORMAT_H

#include <cstddef>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

// Decimal byte units, with one fractional digit above bytes.
inline std::string human_readable_size(std::size_t bytes)
{
    if (bytes < 1000) return std::to_string(bytes) + " B";
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1000.0 && unit < 6) { value /= 1000.0; ++unit; }
    // Promote values that would round to 1000.0 in the smaller unit.
    if (value >= 999.95 && unit < 6) { value /= 1000.0; ++unit; }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(1) << value << ' ' << units[unit];
    return output.str();
}

#endif
