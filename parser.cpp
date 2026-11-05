/*

 MIT License

 Copyright (c) 2026 pavel.sokolov@gmail.com / CEZEO software Ltd. All rights reserved.

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
 rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
 persons to whom the Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "parser.h"

#include <fmt/args.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>
#include <utility>
#include <variant>

#include "datasource.h"
#include "fitsdk/fit_convert.h"

namespace {

enum DataType : uint32_t {
  // always should be zero
  kTypeFirst = 0,
  kTypeSpeed = 0,
  kTypeDistance = 1,
  kTypeHeartRate = 2,
  kTypeAltitude = 3,
  kTypePower = 4,
  kTypeCadence = 5,
  kTypeTemperature = 6,
  kTypeTimeStamp = 7,
  kTypeLatitude = 8,
  kTypeLongitude = 9,
  kTypeTimeStampNext = 10,
  kTypeGradient = 11,
  // always should be at the end
  kTypeMax,
};

constexpr std::string_view kParamSpeed("speed");
constexpr std::string_view kParamDistance("distance");
constexpr std::string_view kParamHeartrate("heartrate");
constexpr std::string_view kParamAltitude("altitude");
constexpr std::string_view kParamPower("power");
constexpr std::string_view kParamCadence("cadence");
constexpr std::string_view kParamTemperature("temperature");
constexpr std::string_view kParamTimestamp("timestamp");
constexpr std::string_view kParamLatitude("latitude");
constexpr std::string_view kParamLongitude("longitude");
constexpr std::string_view kParamTimestampnext("timestampnext");
constexpr std::string_view kParamGradient("gradient");

constexpr uint32_t DataTypeToMask(const DataType type) {
  return 0x01 << static_cast<uint32_t>(type);
};

constexpr std::array<uint32_t, DataType::kTypeMax> kDataTypeMasks = {
    DataTypeToMask(kTypeSpeed),          // kTypeSpeed
    DataTypeToMask(kTypeDistance),       // kTypeDistance
    DataTypeToMask(kTypeHeartRate),      // kTypeHeartRate
    DataTypeToMask(kTypeAltitude),       // kTypeAltitude
    DataTypeToMask(kTypePower),          // kTypePower
    DataTypeToMask(kTypeCadence),        // kTypeCadence
    DataTypeToMask(kTypeTemperature),    // kTypeTemperature
    DataTypeToMask(kTypeTimeStamp),      // kTypeTimeStamp
    DataTypeToMask(kTypeLatitude),       // kTypeLatitude
    DataTypeToMask(kTypeLongitude),      // kTypeLongitude
    DataTypeToMask(kTypeTimeStampNext),  // kTypeTimeStampNext
    DataTypeToMask(kTypeGradient)        // kTypeGradient
};

constexpr std::array<std::pair<std::string_view, std::string_view>, DataType::kTypeMax> kDataTypes = {
    {{kParamSpeed, "s"},          // kTypeSpeed
     {kParamDistance, "d"},       // kTypeDistance
     {kParamHeartrate, "h"},      // kTypeHeartRate
     {kParamAltitude, "a"},       // kTypeAltitude
     {kParamPower, "p"},          // kTypePower
     {kParamCadence, "c"},        // kTypeCadence
     {kParamTemperature, "t"},    // kTypeTemperature
     {kParamTimestamp, "f"},      // kTypeTimeStamp
     {kParamLatitude, "u"},       // kTypeLatitude
     {kParamLongitude, "o"},      // kTypeLongitude
     {kParamTimestampnext, "n"},  // kTypeTimeStampNext
     {kParamGradient, "g"}}       // kTypeGradient
};

using FormatData = std::array<std::pair<std::string_view, size_t>, DataType::kTypeMax>;

constexpr FormatData kMetricFormat = {
    {{" ㎞/h ", 5},  // kTypeSpeed km/h
     {" ㎞ ", 6},    // kTypeDistance km
     {"❤️ ", 4},      // kTypeHeartRate bpm
     {" m↑ ", 5},    // kTypeAltitude meters
     {"⚡ ", 5},     // kTypePower watt
     {" ↻ ", 4},     // kTypeCadence rotations rpm
     {"°C ", 3},     // kTypeTemperature celsius
     {"", 0},        // kTypeTimeStamp
     {"", 0},        // kTypeLatitude
     {"", 0},        // kTypeLongitude
     {"", 0},        // kTypeTimeStampNext
     {"﹪", 5}}      // kTypeGradient
};

constexpr FormatData kImperialFormat = {
    {{" mp/h ", 5},  // kTypeSpeed km/h
     {" mi ", 6},    // kTypeDistance km
     {"❤️ ", 4},      // kTypeHeartRate bpm
     {" ft↑ ", 5},   // kTypeAltitude meters
     {"⚡ ", 5},     // kTypePower watt
     {" ↻ ", 4},     // kTypeCadence rotations rpm
     {"°F ", 3},     // kTypeTemperature celsius
     {"", 0},        // kTypeTimeStamp
     {"", 0},        // kTypeLatitude
     {"", 0},        // kTypeLongitude
     {"", 0},        // kTypeTimeStampNext
     {"﹪", 5}}      // kTypeGradient
};

constexpr std::string_view kVttHeaderTag("WEBVTT\n\n");
constexpr std::string_view kVttTimeSeparator(" --> ");
constexpr std::string_view kVttOffsetMessage("\n< .fit data is not yet available >");
constexpr std::string_view kVttEndMessage("\n< no more .fit data >");
constexpr std::string_view kVttMessage("\nmade with ❤️ by fitconvert\n\n");

// Slice string with one delimiter char(wchar...)
template <typename InputIterator, typename UnaryFunction>
void each_slice(InputIterator begin, InputIterator end, typename std::iterator_traits<InputIterator>::value_type delimiter, const UnaryFunction& function) {
  InputIterator first = begin, current = begin;
  for (; begin != end; ++begin) {
    if (*begin == delimiter) {
      if (first != current) {
        function(std::basic_string_view<typename std::iterator_traits<InputIterator>::value_type>(first, current));
      }
      first = ++current;
    } else {
      ++current;
    }
  }
  if (first != current) {
    function(std::basic_string_view<typename std::iterator_traits<InputIterator>::value_type>(first, current));
  }
}


DataType NameToDataType(std::string_view name) {
  for (auto i = 0u; i < kDataTypes.size(); i++) {
    if (kDataTypes[i].first == name) {
      return static_cast<DataType>(i);
    }
  }
  return DataType::kTypeMax;
};

// names line delimited by commas
uint32_t DataTypeNamesToMask(std::string_view names) {
  uint32_t types_mask = 0u;
  each_slice(names.begin(), names.end(), ',', [&types_mask](const std::string_view datatype) {
    const auto dt = NameToDataType(datatype);
    if (dt != DataType::kTypeMax) {
      types_mask |= DataTypeToMask(dt);
    } else {
      SPDLOG_ERROR("param name is not supported: {}", datatype);
    }
  });
  return types_mask;
}

// names line delimited by commas
std::vector<size_t> DataTypeNamesToIndexes(std::string_view names) {
  std::vector<size_t> indexes;
  indexes.reserve(kTypeMax);
  each_slice(names.begin(), names.end(), ',', [&indexes](const std::string_view datatype) {
    const auto dt = NameToDataType(datatype);
    if (dt != DataType::kTypeMax) {
      indexes.push_back(dt);
    } else {
      SPDLOG_ERROR("param name is not supported: {}", datatype);
    }
  });
  return indexes;
}

// Return Value
using ProcessedValue = std::variant<int64_t, double>;
// Convert Functions
using ParamProcessor = ProcessedValue (*)(int64_t, bool);

constexpr std::array<std::pair<ParamProcessor, size_t>, DataType::kTypeMax> kProcessors = {
    // kTypeSpeed
    {
        {[](int64_t raw, bool imperial) -> ProcessedValue {
           // FIT_UINT32 enhanced_speed = 1000 * m/s = mm/s
           return static_cast<double>(raw) / (imperial ? 447.2136 : 277.77);
         },
         1},  // kTypeSpeed
        // kTypeDistance
        {[](int64_t raw, bool imperial) -> ProcessedValue {
           return static_cast<double>(raw) / (imperial ? 160934.4 : 100000.0);
         },
         2},  // kTypeDistance
        // kTypeHeartRate
        {[](int64_t raw, bool) -> ProcessedValue {
           // FIT_UINT8 heart_rate = bpm
           return raw;
         },
         0},  // kTypeHeartRate
        // kTypeAltitude
        {[](int64_t raw, bool imperial) -> ProcessedValue {
           // FIT_UINT32 enhanced_altitude = 5 * m + 500
           const int64_t altitude_meters = (raw / 5) - 500;
           return imperial ? (altitude_meters * 3.28084) : altitude_meters;
         },
         0},  // kTypeAltitude
        // kTypePower
        {[](int64_t raw, bool) -> ProcessedValue {
           // FIT_UINT16 power = watts
           return raw;
         },
         0},  // kTypePower
        // kTypeCadence
        {[](int64_t raw, bool) -> ProcessedValue {
           // FIT_UINT8 cadence = rpm
           return raw;
         },
         0},  // kTypeCadence
        // kTypeTemperature
        {[](int64_t raw, bool imperial) -> ProcessedValue {
           // FIT_SINT8 temperature = C
           return imperial ? (raw * 9 / 5 + 32) : raw;
         },
         0},  // kTypeTemperature
        // kTypeTimeStamp
        {[](int64_t raw, bool) -> ProcessedValue {
           return raw;
         },
         0},  // kTypeTimeStamp
        // kTypeLatitude
        {[](int64_t raw, bool) -> ProcessedValue {
           // semicircles to degrees
           return static_cast<double>(raw) * (180.0 / 2147483648.0);
         },
         0},  // kTypeLatitude
        // kTypeLongitude
        {[](int64_t raw, bool) -> ProcessedValue {
           // semicircles to degrees
           return static_cast<double>(raw) * (180.0 / 2147483648.0);
         },
         0},  // kTypeLongitude
        // kTypeTimeStampNext
        {[](int64_t raw, bool) -> ProcessedValue {
           return raw;
         },
         0},  // kTypeTimeStampNext
        // kTypeGradient
        {[](int64_t raw, bool) -> ProcessedValue {
           // FIT_SINT16 grade = 100 * % + 0
           return static_cast<double>(raw) / 100.0;
         },
         1}  // kTypeGradient
    }};

class GradientCalculator {
 public:
  explicit GradientCalculator(size_t window_size = 5) : window_size(window_size), buffer(window_size) {}

  struct RawPoint {
    uint32_t distance;
    uint32_t altitude;
  };

  int16_t AddRawPoint(uint32_t raw_distance, uint32_t raw_altitude) {
    buffer[current_head] = {raw_distance, raw_altitude};
    current_head = (current_head + 1) % window_size;
    if (current_count < window_size) {
      current_count++;
    }

    if (current_count < 3) {
      return 0;
    }

    double sum_x = 0.0;
    double sum_y = 0.0;
    double sum_xx = 0.0;
    double sum_xy = 0.0;

    for (size_t i = 0; i < current_count; ++i) {
      const auto& pt = buffer[i];
      const double x = static_cast<double>(pt.distance);
      const double y = static_cast<double>(pt.altitude);

      sum_x += x;
      sum_y += y;
      sum_xx += x * x;
      sum_xy += x * y;
    }

    const double denominator = (current_count * sum_xx - sum_x * sum_x);

    if (std::abs(denominator) < 1e-5) {
      return 0;
    }

    const double raw_slope = (current_count * sum_xy - sum_x * sum_y) / denominator;
    const double garmin_grade = raw_slope * 200000.0;

    // round
    return static_cast<int16_t>(std::round(garmin_grade));
  }

 private:
  const size_t window_size;
  std::vector<RawPoint> buffer;
  size_t current_head{0};
  size_t current_count{0};
};

class CustomFormater final {
 public:
  explicit CustomFormater(const std::string_view format_string, const std::string_view data_types_order, bool imperial)
      : imperial_(imperial) {
    if (format_string.size() > 0) {
      // {speed:5.1f} km/h;{distance:5.1f} km;{heartrate:3} bpm;{altitude:4} m;{power:4} W;{temperature:3}C;{cadence:3} rpm;{gradient:2.1f}%
      each_slice(format_string.begin(), format_string.end(), ';', [this](const std::string_view format) {
        // SPDLOG_ERROR("format found: {}", format);
        ApplyFormat(format);
      });
    } else {
      data_types_array_ = DataTypeNamesToIndexes(data_types_order);
    }
    collect_data_types_ = DataTypeNamesToMask(data_types_order);
    if (0 == collect_data_types_) {
      collect_data_types_ = std::numeric_limits<uint32_t>::max();
    }

    // SPDLOG_ERROR("dataypes: {}, mask: {}", data_types_array_.size(), collect_data_types_);
  }

  void ApplyFormat(const std::string_view format) {
    // text before the '{' is kept as a label, e.g. "HR {heartrate:3}"
    const size_t brace_pos = format.find('{');
    if (brace_pos == std::string_view::npos) {  // {tag required
      return;
    }
    const std::string_view label = format.substr(0, brace_pos);
    const std::string_view tag = format.substr(brace_pos + 1);  // skip '{'
    for (size_t type_index = kTypeFirst; type_index < kTypeMax; type_index++) {
      const std::string_view name = kDataTypes[type_index].first;
      // the name must be followed by ':' or '}', otherwise 'timestamp' would also match 'timestampnext'
      if (tag.size() <= name.size() || !tag.starts_with(name) || (tag[name.size()] != ':' && tag[name.size()] != '}')) {
        continue;
      }
      std::string field_format{label};
      field_format.push_back('{');
      field_format.append(tag.substr(name.size()));
      if (!ValidateFormat(field_format, type_index)) {
        valid_ = false;
        return;
      }
      formats_[type_index] = std::move(field_format);
      data_types_array_.push_back(type_index);
      fileds_++;
      return;
    }
    SPDLOG_ERROR("unknown field in format: '{}'", format);
    valid_ = false;
  }

  const uint32_t GetDataTypesMask() const { return collect_data_types_; }
  size_t FieldsInitialized() const { return fileds_; }
  bool IsImperial() const { return imperial_; }
  bool IsValid() const { return valid_; }
  const std::vector<size_t>& GetDataTypeIndexes() const { return data_types_array_; }

  const std::string_view GetFormat(const size_t index) const {
    if (index < kTypeMax) {
      return formats_[index];
    }
    return {};
  }

 private:
  uint32_t collect_data_types_{0};
  bool imperial_{false};
  std::vector<size_t> data_types_array_;
  std::string formats_[kTypeMax];

  bool valid_{true};

  // format a sample value of the type the field produces, to reject bad formats before conversion starts
  bool ValidateFormat(const std::string& field_format, const size_t type_index) const {
    try {
      const ProcessedValue sample = kProcessors[type_index].first(0, imperial_);
      std::visit([&](auto&& value) { (void)fmt::format(fmt::runtime(field_format), value); }, sample);
      return true;
    } catch (const fmt::format_error& e) {
      SPDLOG_ERROR("invalid format '{}' for '{}': {}", field_format, kDataTypes[type_index].first, e.what());
    }
    return false;
  }

  size_t fileds_{0};
};

// adapter for fmt::format_to to write directly into a RapidJSON StringBuffer
struct StringBufferAppender {
  rapidjson::StringBuffer& buf;

  using value_type = char;
  using difference_type = std::ptrdiff_t;
  using pointer = void;
  using reference = char;
  using iterator_category = std::output_iterator_tag;

  StringBufferAppender& operator=(char c) {
    buf.Put(c);
    return *this;
  }

  StringBufferAppender& operator*() { return *this; }
  StringBufferAppender& operator++() { return *this; }
  StringBufferAppender& operator++(int) { return *this; }
};

class OutputBuffer final : public rapidjson::StringBuffer {
 public:
  using Base = rapidjson::StringBuffer;

  OutputBuffer() = default;
  OutputBuffer(OutputBuffer&&) noexcept = default;
  OutputBuffer& operator=(OutputBuffer&&) noexcept = default;

  OutputBuffer(const OutputBuffer&) = delete;
  OutputBuffer& operator=(const OutputBuffer&) = delete;

  // append newline
  void NewLine() { Put('\n'); }

  // append formatted
  template <typename... Args>
  void AppendFmt(fmt::format_string<Args...> fmtStr, Args&&... args) {
    fmt::format_to(StringBufferAppender{*this}, fmtStr, std::forward<Args>(args)...);
  }

  void AppendString(std::string_view s) {
    for (char c : s) {
      Put(c);
    }
  }

  void AppendString(const std::string& s) {
    for (char c : s) {
      Put(c);
    }
  }

  void AppendString(const char* s, size_t size) {
    for (size_t i = 0u; i < size; ++i) {
      Put(s[i]);
    }
  }

  // expose current size and data
  const char* data() const noexcept { return GetString(); }
  size_t size() const noexcept { return GetSize(); }
};

constexpr std::string_view kFmtMetricFormat{
    "{speed:5.1f} ㎞/h;{distance:5.1f} ㎞;{heartrate:3}❤️;{altitude:4}m;{power:4}⚡;{temperature:3.1f}°C;{cadence:3}↻;{grade:2.1f}﹪"};
constexpr std::string_view kFmtImperialFormat{
    "{speed:5.1f} mp/h;{distance:5.1f} mi;{heartrate:3}❤️;{altitude:4}m;{power:4}⚡;{temperature:3.1f}°F;{cadence:3}↻;{grade:2.1f}﹪"};

struct Time {
  Time(const int64_t milliseconds_total) {
    if (milliseconds_total > 356400000) {  // 99 hours
      throw std::invalid_argument("unsupported time frame");
    }
    uint64_t ms_remainder = milliseconds_total;
    hours = static_cast<uint8_t>(ms_remainder / 3600000);
    ms_remainder = ms_remainder - (hours * 3600000);
    minutes = static_cast<uint8_t>(ms_remainder / 60000);
    ms_remainder = ms_remainder - (minutes * 60000);
    seconds = static_cast<uint8_t>(ms_remainder / 1000);
    milliseconds = static_cast<uint16_t>(ms_remainder - (seconds * 1000));
  }

  uint16_t milliseconds{0};
  uint16_t hours{0};
  uint8_t minutes{0};
  uint8_t seconds{0};
};

template <typename T>
size_t format_value_suffix(T value,                        //
                           char* buffer_ptr,               //
                           const size_t buffer_size,       //
                           const size_t total_width,       //
                           const std::string_view suffix,  //
                           const size_t precision = 0) {
  static_assert(std::is_arithmetic_v<T>, "format_value_suffix: T must be arithmetic");

  auto* formatted_ptr = buffer_ptr;

  if constexpr (std::is_floating_point_v<T>) {
    auto res = std::to_chars(buffer_ptr, buffer_ptr + buffer_size, value, std::chars_format::fixed, precision);
    if (res.ec != std::errc{}) {
      return 0u;  // conversion failed
    }
    formatted_ptr = res.ptr;
  } else if constexpr (std::is_integral_v<T>) {
    auto res = std::to_chars(buffer_ptr, buffer_ptr + buffer_size, value);
    if (res.ec != std::errc{}) {
      return 0u;  // conversion failed
    }
    formatted_ptr = res.ptr;
  }

  size_t length = static_cast<size_t>(formatted_ptr - buffer_ptr);

  // pad to total width (spaces on left)
  const size_t pad = total_width > length ? total_width - length : 0;
  if (pad > 0u) {
    std::memmove(buffer_ptr + pad, buffer_ptr, length);
    std::memset(buffer_ptr, ' ', pad);
    length += pad;
    formatted_ptr += pad;
  }

  // append suffix
  const size_t suffix_len = suffix.size();
  if (length + suffix_len >= buffer_size) {
    return 0u;  // no space left
  }

  std::memcpy(formatted_ptr, suffix.data(), suffix_len);
  formatted_ptr += suffix_len;
  length += suffix_len;

  return length;
}


size_t format_timestamp(char* buffer_ptr, const size_t buffer_size, const Time& time) {
  if (buffer_size < 16)
    return 0u;

  char* ptr = buffer_ptr;

  auto write_2digits = [&](uint8_t value) -> bool {
    if (value < 0u || value > 99u)
      return false;
    const uint8_t tens = (value / 10u) % 10u;
    const uint8_t ones = value % 10u;
    *ptr++ = static_cast<char>('0' + tens);
    *ptr++ = static_cast<char>('0' + ones);
    return true;
  };

  auto write_3digits = [&](uint16_t value) -> bool {
    if (value < 0u || value > 999u)
      return false;
    const uint8_t hundreds = value / 100u;
    const uint8_t tens = (value / 10u) % 10u;
    const uint8_t ones = value % 10u;
    *ptr++ = static_cast<char>('0' + hundreds);
    *ptr++ = static_cast<char>('0' + tens);
    *ptr++ = static_cast<char>('0' + ones);
    return true;
  };

  if (!write_2digits(time.hours))
    return 0u;
  *ptr++ = ':';
  if (!write_2digits(time.minutes))
    return 0u;
  *ptr++ = ':';
  if (!write_2digits(time.seconds))
    return 0u;
  *ptr++ = '.';
  if (!write_3digits(time.milliseconds))
    return 0u;
  return static_cast<size_t>(ptr - buffer_ptr);
}

struct FitData final {
  FitData() {
    memset(values, 0, sizeof(values));
    static_assert(FIT_UINT32_INVALID == std::numeric_limits<FIT_UINT32>::max());
    static_assert(FIT_UINT16_INVALID == std::numeric_limits<FIT_UINT16>::max());
    static_assert(FIT_BYTE_INVALID == std::numeric_limits<FIT_BYTE>::max());
    static_assert(FIT_SINT8_INVALID == std::numeric_limits<FIT_SINT8>::max());
    static_assert(FIT_SINT32_INVALID == std::numeric_limits<FIT_SINT32>::max());
  };

  FitData(const FIT_RECORD_MESG* fit_record_ptr, const uint32_t collect_data_types) { ApplyData(fit_record_ptr, collect_data_types); }

  void ApplyData(const FIT_RECORD_MESG* fit_record_ptr, const uint32_t collect_data_types) {
    memset(values, 0, sizeof(values));
    available_types = 0u;
    const int64_t msec = static_cast<int64_t>(fit_record_ptr->timestamp) * 1000;
    // timestamps
    ApplyValue(DataType::kTypeTimeStamp, msec, collect_data_types);
    ApplyValue(DataType::kTypeTimeStampNext, msec, collect_data_types);

    // FIT_UINT32 distance = 100 * m = cm
    ApplyValue(DataType::kTypeDistance, fit_record_ptr->distance, collect_data_types);
    // FIT_UINT8 heart_rate = bpm
    ApplyValue(DataType::kTypeHeartRate, fit_record_ptr->heart_rate, collect_data_types);
    // FIT_IONT8 cadence Rotations
    ApplyValue(DataType::kTypeCadence, fit_record_ptr->cadence, collect_data_types);
    // FIT_UINT16 power = watts
    ApplyValue(DataType::kTypePower, fit_record_ptr->power, collect_data_types);

    // FIT_UINT16 altitude = 5 * m + 500
    ApplyValue(DataType::kTypeAltitude, fit_record_ptr->altitude, collect_data_types);
    // FIT_UINT32 enhanced_altitude = 5 * m + 500
    ApplyValue(DataType::kTypeAltitude, fit_record_ptr->enhanced_altitude, collect_data_types);
    // FIT_UINT16 speed = 1000 * m/s = mm/s
    ApplyValue(DataType::kTypeSpeed, fit_record_ptr->speed, collect_data_types);
    // FIT_UINT32 enhanced_speed = 1000 * m/s = mm/s
    ApplyValue(DataType::kTypeSpeed, fit_record_ptr->enhanced_speed, collect_data_types);
    // FIT_SINT8 temperature = C
    ApplyValue(DataType::kTypeTemperature, fit_record_ptr->temperature, collect_data_types);
    // FIT_SINT32 position_lat = semicircles
    ApplyValue(DataType::kTypeLatitude, fit_record_ptr->position_lat, collect_data_types);
    // FIT_SINT32 position_long = semicircles
    ApplyValue(DataType::kTypeLongitude, fit_record_ptr->position_long, collect_data_types);
    // FIT_SINT16 grade = 100 * % + 0
    ApplyValue(DataType::kTypeGradient, fit_record_ptr->grade, collect_data_types);
  }

  void ExportToJson(rapidjson::Writer<rapidjson::StringBuffer>& writer, const CustomFormater& formatter) {
    writer.StartObject();

    for (const auto type_index : formatter.GetDataTypeIndexes()) {
      if (!(available_types & kDataTypeMasks[type_index])) {
        continue;
      }

      ProcessedValue cooked_value = kProcessors[type_index].first(values[type_index], formatter.IsImperial());
      writer.Key(rapidjson::StringRef(kDataTypes[type_index].second.data(), kDataTypes[type_index].second.size()));
      if ((kDataTypeMasks[kTypeLatitude] | kDataTypeMasks[kTypeLongitude]) & kDataTypeMasks[type_index]) {
        writer.SetMaxDecimalPlaces(6);
      } else {
        writer.SetMaxDecimalPlaces(2);
      }
      std::visit(
          [&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, int64_t>) {
              writer.Int64(arg);
            } else if constexpr (std::is_same_v<T, uint64_t>) {
              writer.Uint64(arg);
            } else if constexpr (std::is_same_v<T, double>) {
              writer.Double(arg);
            }
          },
          cooked_value);
    }

    writer.EndObject();
  }

  void ExportToVtt(OutputBuffer& writer, const CustomFormater& formatter) {
    const Time time_from(values[DataType::kTypeTimeStamp]);
    const Time time_to(values[DataType::kTypeTimeStampNext]);
    const FormatData& format = formatter.IsImperial() ? kImperialFormat : kMetricFormat;
    std::array<char, 32u> formatting_buffer;
    {
      const size_t size = format_timestamp(formatting_buffer.data(), formatting_buffer.size(), time_from);
      writer.AppendString(formatting_buffer.data(), size);
    }
    writer.AppendString(kVttTimeSeparator);
    {
      const size_t size = format_timestamp(formatting_buffer.data(), formatting_buffer.size(), time_to);
      writer.AppendString(formatting_buffer.data(), size);
    }
    writer.NewLine();

    for (const auto type_index : formatter.GetDataTypeIndexes()) {
      if (!(available_types & kDataTypeMasks[type_index])) {
        continue;
      }

      ProcessedValue cooked_value = kProcessors[type_index].first(values[type_index], formatter.IsImperial());

      std::visit(
          [&](auto&& arg) {
            const size_t size = format_value_suffix(arg,
                                                    formatting_buffer.data(),
                                                    formatting_buffer.size(),
                                                    format[type_index].second,
                                                    format[type_index].first,
                                                    kProcessors[type_index].second);
            writer.AppendString(formatting_buffer.data(), size);
          },
          cooked_value);
    }

    writer.NewLine();
    writer.NewLine();
  }

  void ExportToVttFmt(OutputBuffer& writer, const CustomFormater& formatter) {
    const Time time_from(values[DataType::kTypeTimeStamp]);
    const Time time_to(values[DataType::kTypeTimeStampNext]);

    std::array<char, 32u> formatting_buffer;
    {
      const size_t size = format_timestamp(formatting_buffer.data(), formatting_buffer.size(), time_from);
      writer.AppendString(formatting_buffer.data(), size);
    }
    writer.AppendString(kVttTimeSeparator);
    {
      const size_t size = format_timestamp(formatting_buffer.data(), formatting_buffer.size(), time_to);
      writer.AppendString(formatting_buffer.data(), size);
    }
    writer.NewLine();

    for (const auto type_index : formatter.GetDataTypeIndexes()) {
      if (!(available_types & kDataTypeMasks[type_index])) {
        continue;
      }

      ProcessedValue cooked_value = kProcessors[type_index].first(values[type_index], formatter.IsImperial());
      const std::string_view active_format = formatter.GetFormat(type_index);

      std::visit(
          [&](auto&& arg) {
            auto result = fmt::format_to_n(formatting_buffer.data(), formatting_buffer.size(), fmt::runtime(active_format), arg);
            writer.AppendString(formatting_buffer.data(), result.size);
          },
          cooked_value);
    }

    writer.NewLine();
    writer.NewLine();
  }

  FitData operator-(const FitData right_value) noexcept {
    FitData diff_record;
    diff_record.available_types = available_types | right_value.available_types;
    for (uint32_t index = DataType::kTypeFirst; index < DataType::kTypeMax; ++index) {
      diff_record.values[index] = values[index] - right_value.values[index];
    }
    return diff_record;
  };

  FitData operator+(const FitData right_value) noexcept {
    FitData summ_record;
    summ_record.available_types = available_types | right_value.available_types;
    for (uint32_t index = DataType::kTypeFirst; index < DataType::kTypeMax; ++index) {
      summ_record.values[index] = values[index] + right_value.values[index];
    }
    return summ_record;
  };

  FitData operator/(const int64_t divider) noexcept {
    FitData divided_record;
    divided_record.available_types = available_types;
    for (uint32_t index = DataType::kTypeFirst; index < DataType::kTypeMax; ++index) {
      divided_record.values[index] = values[index] / divider;
    }
    return divided_record;
  };

  void SetValue(const DataType type, const int64_t data) noexcept { values[type] = data; }

  int64_t GetValue(const DataType type) const noexcept { return values[type]; }

  uint32_t GetTypes() const noexcept { return available_types; }

 private:
  int64_t values[DataType::kTypeMax];
  uint32_t available_types{0u};

 private:
  template <typename T>
  void ApplyValue(const DataType type, const T value, const uint32_t collect_data_types) {
    const uint32_t datatype_mask{kDataTypeMasks[type]};
    if (value != std::numeric_limits<T>::max() && (collect_data_types & datatype_mask)) {
      values[type] = value;
      available_types |= datatype_mask;
    }
  }
};

}  // namespace

std::unique_ptr<FitResult> Convert(std::unique_ptr<DataSource> data_source_ptr,
                                   const std::string_view output_type,
                                   const int64_t offset,
                                   const uint8_t smoothness,
                                   const std::string_view data_types_order,
                                   const bool imperial,
                                   const std::string& format) {
  auto result = std::make_unique<FitResult>();
  result->first = ParseResult::kError;

  const bool json_output = (output_type == kOutputJsonTag);
  const bool vtt_output = (output_type == kOutputVttTag);

  // used_data_types - mask of values DataType values: 0x01 << DataType
  uint32_t used_data_types{0u};
  uint32_t file_items{0u};
  uint32_t non_msg_counter{0u};
  int64_t first_fit_timestamp{0};
  int64_t first_video_timestamp{0};

  const size_t data_source_size = data_source_ptr->GetSize();
  FIT_CONVERT_RETURN fit_status = FIT_CONVERT_CONTINUE;
  FitConvert_Init(FIT_TRUE);
  Buffer data_buffer(4096u * 16u);

  GradientCalculator gradient;
  CustomFormater formatter(format, data_types_order, imperial);
  if (!formatter.IsValid()) {
    return result;
  }

  OutputBuffer write_buffer;
  // start json creation
  rapidjson::Writer<rapidjson::StringBuffer> writer(write_buffer);
  write_buffer.Reserve((data_source_size == 0u ? (2048u * 1024u) : (data_source_size + (data_source_size >> 2u))));
  if (json_output) {
    writer.SetMaxDecimalPlaces(2);
    writer.StartObject();
    // records
    writer.Key("records");
    writer.StartArray();
  } else if (vtt_output) {
    write_buffer.AppendString(kVttHeaderTag);
  }

  // make exporter
  auto MakeExporter = [](auto& write_buffer, auto& writer, auto& vtt_output, auto& json_output, const CustomFormater& formatter) {
    return [&](auto&& x) -> void {
      if (json_output) {
        x->ExportToJson(writer, formatter);
      } else if (vtt_output) {
        if (formatter.FieldsInitialized() > 0) {
          x->ExportToVttFmt(write_buffer, formatter);
        } else {
          x->ExportToVtt(write_buffer, formatter);
        }
      }
    };
  };

  auto Export = MakeExporter(write_buffer, writer, vtt_output, json_output, formatter);

  FitData new_fit_data;
  FitData previous_fit_data;
  FitData* new_fit_data_ptr = &new_fit_data;
  FitData* previous_fot_data_ptr = nullptr;

  while ((DataSource::Status::kError != data_source_ptr->ReadData(data_buffer)) && (fit_status == FIT_CONVERT_CONTINUE) &&
         data_buffer.GetDataSize() > 0u) {
    while (fit_status = FitConvert_Read(data_buffer.GetDataPtr(), static_cast<FIT_UINT32>(data_buffer.GetDataSize())),
           fit_status == FIT_CONVERT_MESSAGE_AVAILABLE) {
      if (FitConvert_GetMessageNumber() != FIT_MESG_NUM_RECORD) {
        non_msg_counter++;
        continue;
      }

      const FIT_UINT8* fit_message_ptr = FitConvert_GetMessageData();
      const FIT_RECORD_MESG* fit_record_ptr = reinterpret_cast<const FIT_RECORD_MESG*>(fit_message_ptr);

      // convert timestamp to milliseconds
      const int64_t type_msec = static_cast<int64_t>(fit_record_ptr->timestamp) * 1000;
      // fit timestamp should not be 0, because it's milliseconds since UTC 00:00 Dec 31 1989
      if (0 == first_fit_timestamp) {
        first_fit_timestamp = type_msec;
        if (offset > 0) {
          first_fit_timestamp += offset;
        } else if (offset < 0) {
          first_video_timestamp = std::abs(offset);
          if (vtt_output) {
            // write message that .fit data is not yet ready
            const Time start(0);
            const Time end(first_video_timestamp);
            std::array<char, 32> formatting_buffer;
            const size_t size_start = format_timestamp(formatting_buffer.data(), formatting_buffer.size(), start);
            write_buffer.AppendString(formatting_buffer.data(), size_start);
            write_buffer.AppendString(kVttTimeSeparator);
            const size_t size_end = format_timestamp(formatting_buffer.data(), formatting_buffer.size(), end);
            write_buffer.AppendString(formatting_buffer.data(), size_end);
            write_buffer.AppendString(kVttOffsetMessage);
            write_buffer.AppendString(kVttMessage);
          }
        }
      }

      if (offset > 0) {
        // positive offset, 'offset' second of the data from .fit file will displayed at the first second of the video
        if (type_msec < first_fit_timestamp) {
          continue;
        }
      }

      // gradient calculation
      auto altitude = std::numeric_limits<decltype(fit_record_ptr->enhanced_altitude)>::max();
      if (fit_record_ptr->enhanced_altitude != std::numeric_limits<decltype(fit_record_ptr->enhanced_altitude)>::max()) {
        altitude = fit_record_ptr->enhanced_altitude;
      } else if (fit_record_ptr->altitude != std::numeric_limits<decltype(fit_record_ptr->altitude)>::max()) {
        altitude = fit_record_ptr->altitude;
      }
      auto distance = std::numeric_limits<decltype(fit_record_ptr->distance)>::max();
      if (fit_record_ptr->distance != std::numeric_limits<decltype(fit_record_ptr->distance)>::max()) {
        distance = fit_record_ptr->distance;
      }
      if (altitude != std::numeric_limits<decltype(altitude)>::max() && distance != std::numeric_limits<decltype(distance)>::max()) {
        FIT_RECORD_MESG* fit_record_modifieable_ptr = const_cast<FIT_RECORD_MESG*>(fit_record_ptr);
        fit_record_modifieable_ptr->grade = gradient.AddRawPoint(distance, altitude);
      }

      new_fit_data_ptr->ApplyData(fit_record_ptr, formatter.GetDataTypesMask());

      // reset timestamp to video data (+offset), ONLY AFTER ApplyData!
      const int64_t new_fit_from_ms = (type_msec - first_fit_timestamp) + first_video_timestamp;
      new_fit_data_ptr->SetValue(DataType::kTypeTimeStamp, new_fit_from_ms);
      // apply to global flags
      used_data_types |= new_fit_data_ptr->GetTypes();
      ++file_items;
      std::swap(previous_fot_data_ptr, new_fit_data_ptr);
      if (new_fit_data_ptr == nullptr) {
        new_fit_data_ptr = &previous_fit_data;
        continue;
      }
      // process new_fit_data_ptr (actually the previous one as we swaped them) and we have new one in previous_fit_data_ptr
      if (smoothness > 0u) {
        const int64_t smoothed_diff_ms = (new_fit_from_ms - new_fit_data_ptr->GetValue(DataType::kTypeTimeStamp)) / (smoothness + 1u);
        new_fit_data_ptr->SetValue(DataType::kTypeTimeStampNext, new_fit_data_ptr->GetValue(DataType::kTypeTimeStamp) + smoothed_diff_ms);
        Export(new_fit_data_ptr);

        FitData diff = *previous_fot_data_ptr - *new_fit_data_ptr;
        diff = diff / (smoothness + 1u);
        for (uint8_t cur_step = 0u; cur_step < smoothness; ++cur_step) {
          *new_fit_data_ptr = *new_fit_data_ptr + diff;
          new_fit_data_ptr->SetValue(DataType::kTypeTimeStampNext, new_fit_data_ptr->GetValue(DataType::kTypeTimeStamp) + smoothed_diff_ms);
          Export(new_fit_data_ptr);
        }
      } else {
        new_fit_data_ptr->SetValue(DataType::kTypeTimeStampNext, new_fit_from_ms);
        Export(new_fit_data_ptr);
      }
    }
  }

  if (fit_status == FIT_CONVERT_END_OF_FILE) {
    // success
    result->first = ParseResult::kSuccess;

    // finish json
    if (previous_fot_data_ptr != nullptr) {
      // save last item
      previous_fot_data_ptr->SetValue(
          DataType::kTypeTimeStampNext,
          previous_fot_data_ptr->GetValue(DataType::kTypeTimeStamp) + 1000);  // last item have no the next, to take time from
      Export(previous_fot_data_ptr);
      if (vtt_output) {
        const Time start(previous_fot_data_ptr->GetValue(DataType::kTypeTimeStampNext));
        const Time end(previous_fot_data_ptr->GetValue(DataType::kTypeTimeStampNext) + 60000);
        std::array<char, 32> formatting_buffer;
        const size_t size_start = format_timestamp(formatting_buffer.data(), formatting_buffer.size(), start);
        write_buffer.AppendString(formatting_buffer.data(), size_start);
        write_buffer.AppendString(kVttTimeSeparator);
        const size_t size_end = format_timestamp(formatting_buffer.data(), formatting_buffer.size(), end);
        write_buffer.AppendString(formatting_buffer.data(), size_end);
        write_buffer.AppendString(kVttEndMessage);
        write_buffer.AppendString(kVttMessage);
      }
    }

    if (json_output) {
      // records
      writer.EndArray();
      writer.Key("types");
      writer.StartObject();
      // types legend
      for (uint32_t type = DataType::kTypeFirst; type < DataType::kTypeMax; ++type) {
        writer.Key(rapidjson::StringRef(kDataTypes[type].first.data(), kDataTypes[type].first.size()));
        writer.Uint64(kDataTypeMasks[type]);
      }
      writer.EndObject();
      writer.Key("fields");
      writer.StartObject();
      // types legend
      for (uint32_t type = DataType::kTypeFirst; type < DataType::kTypeMax; ++type) {
        writer.Key(rapidjson::StringRef(kDataTypes[type].first.data(), kDataTypes[type].first.size()));
        writer.String(rapidjson::StringRef(kDataTypes[type].second.data(), kDataTypes[type].second.size()));
      }
      writer.EndObject();
      writer.Key("usedTypes");
      writer.Uint64(used_data_types);
      writer.Key("timestamp");
      writer.Int64(first_fit_timestamp);
      writer.Key("offset");
      writer.Int64(offset);
      writer.Key("units");
      if (imperial) {
        writer.String(rapidjson::StringRef(kValuesImperial.data(), kValuesImperial.size()));
      } else {
        writer.String(rapidjson::StringRef(kValuesMetric.data(), kValuesMetric.size()));
      }
      writer.EndObject();
    }

    result->second = std::move(write_buffer);

  } else if (fit_status == FIT_CONVERT_ERROR) {
    SPDLOG_ERROR("error decoding file");
  } else if (fit_status == FIT_CONVERT_CONTINUE) {
    SPDLOG_ERROR("unexpected end of file");
  } else if (fit_status == FIT_CONVERT_DATA_TYPE_NOT_SUPPORTED) {
    SPDLOG_ERROR("file is not FIT file");
  } else if (fit_status == FIT_CONVERT_PROTOCOL_VERSION_NOT_SUPPORTED) {
    SPDLOG_ERROR("protocol version not supported");
  }

  // will not work for cout output
  SPDLOG_INFO("fit records processed: {}, source size: {}, non items: {}", file_items, data_source_size, non_msg_counter);
  return result;
}
