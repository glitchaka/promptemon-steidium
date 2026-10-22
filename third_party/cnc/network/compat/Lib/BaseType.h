#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <string>

using Bool = bool;
using Byte = signed char;
using UnsignedByte = unsigned char;
using Short = short;
using UnsignedShort = unsigned short;
using Int = int;
using UnsignedInt = unsigned int;
using Int64 = std::int64_t;
using UnsignedInt64 = std::uint64_t;
using Real = float;

class AsciiString {
public:
    AsciiString() = default;
    AsciiString(const char* value) : value_(value ? value : "") {}

    const char* str() const noexcept { return value_.c_str(); }

    void format(const char* formatString, ...) {
        if (!formatString) {
            value_.clear();
            return;
        }

        char buffer[1024]{};
        va_list args;
        va_start(args, formatString);
        std::vsnprintf(buffer, sizeof(buffer), formatString, args);
        va_end(args);
        value_ = buffer;
    }

    static const AsciiString TheEmptyString;

private:
    std::string value_;
};

inline const AsciiString AsciiString::TheEmptyString{};
