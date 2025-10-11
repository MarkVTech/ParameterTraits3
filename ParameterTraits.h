#pragma once
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <system_error>
#include <charconv>
#include <array>
#include <cstring>

//
// Parameter identities
//
enum class ParameterID : uint16_t
{
    TemperatureSetpoint,
    HighTemperatureAlarm,
    FanDutyCycle
};

//
// Parameter types
//
struct TemperatureSetpoint
{
    float value;
};

struct HighTemperatureAlarm
{
    float threshold;
};

struct FanDutyCycle
{
    float percent;
};

//
// Non-allocating parse helper (from_chars -> fallback strtof)
//
namespace detail
{
inline bool parse_float(const char* in, float& out)
{
    if (!in)
    {
        return false;
    }

#if defined(__cpp_lib_to_chars) && (__cpp_lib_to_chars >= 201611)
    const char* first = in;
    const char* last  = in + std::strlen(in);
    auto r = std::from_chars(first, last, out, std::chars_format::general);
    if (r.ec == std::errc{} && r.ptr != first)
    {
        return true;
    }
    // fall through to strtof if lib doesn't fully support FP from_chars or input is quirky
#endif
    char* end{};
    float v = std::strtof(in, &end);
    if (end == in)
    {
        return false;
    }
    out = v;
    return true;
}

template <typename T>
inline int safe_snp(const char* fmt, const T& value, char* out, size_t n)
{
    if (n == 0)
    {
        return 0;
    }
    int written = std::snprintf(out, n, fmt, value);
    // Return 0 on truncation so caller can handle gracefully.
    return (written >= 0 && static_cast<size_t>(written) < n) ? written : 0;
}
}

//
// ParameterTraits<T>
//
template <typename T>
struct ParameterTraits;

// TemperatureSetpoint
template <>
struct ParameterTraits<TemperatureSetpoint>
{
    using UnderlyingType = float;

    static constexpr std::string_view name = "TemperatureSetpoint";
    static constexpr TemperatureSetpoint default_v { 37.5f };

    static bool validate(const TemperatureSetpoint& x)
    {
        return x.value >= 0.0f && x.value <= 100.0f;
    }

    static bool parse(const char* in, TemperatureSetpoint& out)
    {
        float v{};
        if (!detail::parse_float(in, v)) return false;
        out.value = v;
        return validate(out);
    }

    static int serialize(const TemperatureSetpoint& x, char* out, size_t n)
    {
        return detail::safe_snp("%.2f", x.value, out, n);
    }
};

// HighTemperatureAlarm
template <>
struct ParameterTraits<HighTemperatureAlarm>
{
    using UnderlyingType = float;

    static constexpr std::string_view name = "HighTemperatureAlarm";
    static constexpr HighTemperatureAlarm default_v { 80.0f };

    static bool validate(const HighTemperatureAlarm& x)
    {
        return x.threshold >= 0.0f && x.threshold <= 150.0f;
    }

    static bool parse(const char* in, HighTemperatureAlarm& out)
    {
        float v{};
        if (!detail::parse_float(in, v)) return false;
        out.threshold = v;
        return validate(out);
    }

    static int serialize(const HighTemperatureAlarm& x, char* out, size_t n)
    {
        return detail::safe_snp("%.2f", x.threshold, out, n);
    }
};

// FanDutyCycle
template <>
struct ParameterTraits<FanDutyCycle>
{
    using UnderlyingType = float;

    static constexpr std::string_view name = "FanDutyCycle";
    static constexpr FanDutyCycle default_v { 50.0f };

    static bool validate(const FanDutyCycle& x)
    {
        return x.percent >= 0.0f && x.percent <= 100.0f;
    }

    static bool parse(const char* in, FanDutyCycle& out)
    {
        float v{};
        if (!detail::parse_float(in, v)) return false;
        out.percent = v;
        return validate(out);
    }

    static int serialize(const FanDutyCycle& x, char* out, size_t n)
    {
        return detail::safe_snp("%.2f", x.percent, out, n);
    }
};

//
// Convenience compile-time dispatch
//
template <typename T>
constexpr std::string_view param_name() { return ParameterTraits<T>::name; }

template <typename T>
constexpr T param_default() { return ParameterTraits<T>::default_v; }

template <typename T>
bool param_parse(const char* in, T& out) { return ParameterTraits<T>::parse(in, out); }

template <typename T>
bool param_validate(const T& x) { return ParameterTraits<T>::validate(x); }

template <typename T>
int param_serialize(const T& x, char* out, size_t n) { return ParameterTraits<T>::serialize(x, out, n); }

//
// Type-erased runtime handlers and factory. Allows a homogeneous registry.
//
struct Handler
{
    ParameterID id;
    const char* name;
    size_t      size;

    bool (*validate)(const void*);
    bool (*parse)(const char*, void*);
    int  (*serialize)(const void*, char*, size_t);
};

//
// A Handler gives us a runtime grip on parameter data and methods.
// Using function objects (created by lambda syntax) ensures no heap is
// used.
//
template <typename T>
constexpr Handler makeHandler(ParameterID id)
{
    return Handler{
        id,
        ParameterTraits<T>::name.data(),
        sizeof(T),
        // validate
        [](const void* p) -> bool {
            return ParameterTraits<T>::validate(*static_cast<const T*>(p));
        },
        // parse
        [](const char* in, void* p) -> bool {
            return ParameterTraits<T>::parse(in, *static_cast<T*>(p));
        },
        // serialize
        [](const void* p, char* out, size_t n) -> int {
            return ParameterTraits<T>::serialize(*static_cast<const T*>(p), out, n);
        }
    };
}

//
// A compile time array; each entry corresponds to the Handler for
// the parameter indicated by the ParameterID enum.
//
static constexpr Handler registry[] {
    makeHandler<TemperatureSetpoint>(ParameterID::TemperatureSetpoint),
    makeHandler<HighTemperatureAlarm>(ParameterID::HighTemperatureAlarm),
    makeHandler<FanDutyCycle>(ParameterID::FanDutyCycle)
};

static constexpr auto registryCount = std::size(registry);

inline const Handler* find_by_id(ParameterID id)
{
    for (auto const& h : registry)
        if (h.id == id) return &h;
    return nullptr;
}

inline const Handler* find_by_name(std::string_view name)
{
    for (auto const& h : registry)
        if (name == h.name) return &h;
    return nullptr;
}
