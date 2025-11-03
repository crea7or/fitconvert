/*

 MIT License

 Copyright (c) 2022 pavel.sokolov@gmail.com / CEZEO software Ltd. All rights reserved.

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

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "datasource.h"
#include "fitsdk/fit_convert.h"

namespace {

constexpr std::string_view kVttHeaderTag("WEBVTT\n\n");
constexpr std::string_view kNoValue("---");


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
    for (char c : s)
      Put(c);
  }

  void AppendString(const std::string& s) {
    for (char c : s)
      Put(c);
  }

  // expose current size and data
  const char* data() const noexcept { return GetString(); }
  size_t size() const noexcept { return GetSize(); }
};

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
  // always should be at the end
  kTypeMax,
};

constexpr uint32_t DataTypeToMask(const DataType type) {
  return 0x01 << static_cast<uint32_t>(type);
};

constexpr std::array<uint32_t, DataType::kTypeMax> kDataTypeMasks = {
    DataTypeToMask(kTypeSpeed),         // kTypeSpeed
    DataTypeToMask(kTypeDistance),      // kTypeDistance
    DataTypeToMask(kTypeHeartRate),     // kTypeHeartRate
    DataTypeToMask(kTypeAltitude),      // kTypeAltitude
    DataTypeToMask(kTypePower),         // kTypePower
    DataTypeToMask(kTypeCadence),       // kTypeCadence
    DataTypeToMask(kTypeTemperature),   // kTypeTemperature
    DataTypeToMask(kTypeTimeStamp),     // kTypeTimeStamp
    DataTypeToMask(kTypeLatitude),      // kTypeLatitude
    DataTypeToMask(kTypeLongitude),     // kTypeLongitude
    DataTypeToMask(kTypeTimeStampNext)  // kTypeTimeStampNext
};

constexpr std::array<std::pair<std::string_view, std::string_view>, DataType::kTypeMax> kDataTypes = {
    {{"speed", "s"},        // kTypeSpeed
     {"distance", "d"},     // kTypeDistance
     {"heartrate", "h"},    // kTypeHeartRate
     {"altitude", "a"},     // kTypeAltitude
     {"power", "p"},        // kTypePower
     {"cadence", "c"},      // kTypeCadence
     {"temperature", "t"},  // kTypeTemperature
     {"timestamp", "f"},    // kTypeTimeStamp
     {"latitude", "u"},     // kTypeLatitude
     {"longitude", "o"},    // kTypeLongitude
     {"timestamp", "t"}}    // kTypeTimeStampNext
};

using FormatData = std::array<std::pair<std::string_view, size_t>, DataType::kTypeMax>;

constexpr FormatData kDataTypeFormatIso = {
    {{" {} km/h ", 11},   // kTypeSpeed km/h
     {"⇄ {} km ", 13},    // kTypeDistance km
     {" ❤️{} ", 11},       // kTypeHeartRate bpm
     {"🏔️{} m ", 15},     // kTypeAltitude meters
     {"⚡{} ", 8},        // kTypePower watt
     {" ↻ {} ", 9},       // kTypeCadence rotations rpm
     {"🌡️{}°C", 12},      // kTypeTemperature celsius
     {"{}", 5},           // kTypeTimeStamp
     {"semicircles", 5},  // kTypeLatitude
     {"semicircles", 5},  // kTypeLongitude
     {"{}", 5}}           // kTypeTimeStampNext
};

constexpr FormatData kDataTypeFormatImp = {
    {{" {} mph ", 10},    // kTypeSpeed km/h
     {"⇄ {} mi ", 13},    // kTypeDistance km
     {" ❤️{} ", 11},       // kTypeHeartRate bpm
     {"🏔️{} ft ", 16},    // kTypeAltitude meters
     {"⚡{} ", 8},        // kTypePower watt
     {" ↻ {} ", 9},       // kTypeCadence rotations rpm
     {"🌡️{}°F", 12},      // kTypeTemperature celsius
     {"{}", 5},           // kTypeTimeStamp
     {"semicircles", 5},  // kTypeLatitude
     {"semicircles", 5},  // kTypeLongitude
     {"{}", 5}}           // kTypeTimeStampNext
};

struct DataTagUnit {
  DataTagUnit() = default;
  DataTagUnit(std::string_view tag, std::string_view units) : data_tag(std::move(tag)), data_units(std::move(units)) {}

  bool IsValid() const { return false == data_tag.empty() && false == data_units.empty(); }

  std::string_view data_tag;
  std::string_view data_units;
};

struct Time {
  int64_t milliseconds{0};
  int64_t hours{0};
  int64_t minutes{0};
  int64_t seconds{0};
};

Time GetTime(const int64_t milliseconds_total) {
  Time time_struct;
  int64_t ms_remainder = milliseconds_total;
  time_struct.hours = ms_remainder / 3600000;
  ms_remainder = ms_remainder - (time_struct.hours * 3600000);
  time_struct.minutes = ms_remainder / 60000;
  ms_remainder = ms_remainder - (time_struct.minutes * 60000);
  time_struct.seconds = ms_remainder / 1000;
  time_struct.milliseconds = ms_remainder - (time_struct.seconds * 1000);
  return time_struct;
};

struct ValueByType {
  bool Valid() const { return dt != DataType::kTypeMax; };
  int64_t value{0};
  DataType dt{DataType::kTypeMax};
};

std::string NumberToStringPrecision(const int64_t number, const double divider, const size_t total_symbols, const size_t dot_limit) {
  const double double_number = static_cast<double>(number);
  std::string str_result(std::to_string(double_number / divider));
  str_result = str_result.substr(0, total_symbols);
  const size_t dot_string_size = str_result.size();
  const size_t dot_position = str_result.find('.');
  if (dot_position != std::string::npos) {
    const size_t after_dot_position = dot_limit + 1;
    if ((dot_string_size - dot_position) > after_dot_position) {
      str_result = str_result.substr(0, (dot_position + after_dot_position));
    }
  }

  const size_t string_size = str_result.size();
  if (string_size > 0 && str_result.at(string_size - 1) == '.') {
    str_result = str_result.substr(0, string_size - 1);
  }
  return str_result;
}

std::string_view DataTypeToName(const DataType type) {
  if (type >= DataType::kTypeFirst && type < kDataTypes.size()) {
    return kDataTypes[type].first;
  }
  return "";
};

DataType NameToDataType(std::string_view name) {
  for (auto i = 0; i < kDataTypes.size(); i++) {
    if (kDataTypes[i].first == name) {
      return static_cast<DataType>(i);
    }
  }
  return DataType::kTypeMax;
};

std::string_view DataTypeToUnit(const DataType type) {
  if (type >= DataType::kTypeFirst && type < kDataTypes.size()) {
    return kDataTypes[type].second;
  }
  return "";
};

void HeaderItem(std::vector<DataTagUnit>& header, const uint32_t header_bitmask, const DataType type) {
  const uint32_t type_bitmask = DataTypeToMask(type);
  if ((header_bitmask & type_bitmask) != 0) {
    header.emplace_back(DataTypeToName(type), DataTypeToUnit(type));
  }
};

bool CheckType(const uint32_t types, const DataType to_check) {
  return types & DataTypeToMask(to_check);
};

template <class T>
std::string FormatValue(const DataType datatype, const T& data, const FormatData& format) {
  std::string formatted{fmt::format(format[datatype].first, data)};
  if (formatted.size() < format[datatype].second) {
    formatted.append(format[datatype].second - formatted.size(), ' ');
  }
  return formatted;
}

struct FitData {
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
    available_types = 0;
    values[DataType::kTypeTimeStamp] = static_cast<int64_t>(fit_record_ptr->timestamp) * 1000;

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
  }

  void ExportToJson(rapidjson::Writer<rapidjson::StringBuffer>& writer, const bool imperial) {
    writer.StartObject();

    if (ExportToJsonCheck(writer, DataType::kTypeTimeStamp)) {
      writer.Int64(values[DataType::kTypeTimeStamp]);
    }

    if (ExportToJsonCheck(writer, DataType::kTypeTimeStampNext)) {
      writer.Int64(values[DataType::kTypeTimeStampNext]);
    }

    if (ExportToJsonCheck(writer, DataType::kTypeDistance)) {
      // FIT_UINT32 distance = 100 * m = cm
      const double distance = static_cast<double>(values[DataType::kTypeDistance]) / (imperial ? 160934.4 : 100000.0);
      writer.Double(distance);
    }

    if (ExportToJsonCheck(writer, DataType::kTypeHeartRate)) {
      // FIT_UINT8 heart_rate = bpm{
      writer.Uint(values[DataType::kTypeHeartRate]);
    }

    if (ExportToJsonCheck(writer, DataType::kTypeCadence)) {
      // FIT_UINT8 cadence = rpm
      writer.Uint(values[DataType::kTypeCadence]);
    }

    if (ExportToJsonCheck(writer, DataType::kTypePower)) {
      // FIT_UINT16 power = watts
      writer.Uint(values[DataType::kTypePower]);
    }

    if (ExportToJsonCheck(writer, DataType::kTypeAltitude)) {
      // FIT_UINT32 enhanced_altitude = 5 * m + 500
      const int64_t altitude_meters = (values[DataType::kTypeAltitude] / 5.0) - 500.0;
      const int64_t altitude = (imperial ? (altitude_meters * 3.28084) : altitude_meters);
      writer.Int(altitude);
    }

    if (ExportToJsonCheck(writer, DataType::kTypeSpeed)) {
      // FIT_UINT32 enhanced_speed = 1000 * m/s = mm/s
      const double speed = static_cast<double>(values[DataType::kTypeSpeed]) / (imperial ? 447.2136 : 277.77);
      writer.Double(speed);
    }

    if (ExportToJsonCheck(writer, DataType::kTypeTemperature)) {
      // FIT_SINT8 temperature = C
      const int64_t temperature = imperial ? (values[DataType::kTypeTemperature] * 9 / 5 + 32) : values[DataType::kTypeTemperature];
      writer.Int(temperature);
    }

    /*
    if (ExportToJsonCheck(writer, DataType::kTypeLatitude)) {
      // FIT_SINT32 position_lat = semicircles
      writer.Int(values[DataType::kTypeLatitude]);
    }

    if (ExportToJsonCheck(writer, DataType::kTypeLongitude)) {
      // FIT_SINT32 position_long = semicircles
      writer.Int(values[DataType::kTypeLongitude]);
    }
    */

    writer.EndObject();
  }

  void ExportToVtt(OutputBuffer& writer, const bool imperial) {
    const Time time_from(GetTime(values[DataType::kTypeTimeStamp]));
    const Time time_to(GetTime(values[DataType::kTypeTimeStampNext]));
    const char milliseconds_delimiter('.');
    const FormatData& format = imperial ? kDataTypeFormatImp : kDataTypeFormatIso;

    writer.AppendFmt("{:0>2d}:{:0>2d}:{:0>2d}{}{:0>3d} --> {:0>2d}:{:0>2d}:{:0>2d}{}{:0>3d}\n",
                     time_from.hours,
                     time_from.minutes,
                     time_from.seconds,
                     milliseconds_delimiter,
                     time_from.milliseconds,
                     time_to.hours,
                     time_to.minutes,
                     time_to.seconds,
                     milliseconds_delimiter,
                     time_to.milliseconds);

    if (available_types & kDataTypeMasks[DataType::kTypeDistance]) {
      const auto distance(NumberToStringPrecision(values[DataType::kTypeDistance], imperial ? 160934.4 : 100000.0, 5, 2));
      writer.AppendString(FormatValue(DataType::kTypeDistance, distance, format));
    }

    if (available_types & kDataTypeMasks[DataType::kTypeHeartRate]) {
      // FIT_UINT8 heart_rate = bpm{
      writer.AppendString(FormatValue(DataType::kTypeHeartRate, values[DataType::kTypeHeartRate], format));
    }

    if (available_types & kDataTypeMasks[DataType::kTypeCadence]) {
      // FIT_UINT8 cadence = rpm
      writer.AppendString(FormatValue(DataType::kTypeCadence, values[DataType::kTypeCadence], format));
    }

    if (available_types & kDataTypeMasks[DataType::kTypePower]) {
      // FIT_UINT16 power = watts
      writer.AppendString(FormatValue(DataType::kTypePower, values[DataType::kTypePower], format));
    }

    if (available_types & kDataTypeMasks[DataType::kTypeAltitude]) {
      // FIT_UINT32 enhanced_altitude = 5 * m + 500
      const int64_t altitude_meters = (values[DataType::kTypeAltitude] / 5) - 500;
      const int64_t altitude = imperial ? (altitude_meters * 3.28084) : altitude_meters;
      writer.AppendString(FormatValue(DataType::kTypeAltitude, altitude, format));
    }

    if (available_types & kDataTypeMasks[DataType::kTypeSpeed]) {
      // FIT_UINT32 enhanced_speed = 1000 * m/s = mm/s
      const auto speed(NumberToStringPrecision(values[DataType::kTypeSpeed], imperial ? 447.2136 : 277.77, 4, 1));

      writer.AppendString(FormatValue(DataType::kTypeSpeed, speed, format));
    }

    if (available_types & kDataTypeMasks[DataType::kTypeTemperature]) {
      // FIT_SINT8 temperature = C
      const int64_t temperature = imperial ? (values[DataType::kTypeTemperature] * 9 / 5 + 32) : values[DataType::kTypeTemperature];
      writer.AppendString(FormatValue(DataType::kTypeTemperature, temperature, format));
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
  uint32_t available_types{0};

 private:
  template <typename T>
  void ApplyValue(const DataType type, const T value, const uint32_t collect_data_types) {
    const uint32_t datatype_mask{kDataTypeMasks[type]};
    if (value != std::numeric_limits<T>::max() && (collect_data_types & datatype_mask)) {
      values[type] = value;
      available_types |= datatype_mask;
    }
  }

  bool ExportToJsonCheck(rapidjson::Writer<rapidjson::StringBuffer>& writer, DataType type) {
    if (available_types & kDataTypeMasks[type]) {
      writer.Key(rapidjson::StringRef(kDataTypes[type].second.data(), kDataTypes[type].second.size()));
      return true;
    }
    return false;
  }
};

}  // namespace

// names line delimited by commas
uint32_t DataTypeNamesToMask(std::string_view names) {
  std::vector<std::string_view> types;
  size_t start = 0;
  size_t end = 0;

  while ((end = names.find(",", start)) != std::string::npos) {
    const std::string_view tag(names.substr(start, end - start));
    if (tag.size() > 0) {
      types.emplace_back(tag);
    }
    start = end + 1;
  }
  const std::string_view last_tag(names.substr(start, names.size()));
  if (last_tag.size() > 0) {
    types.emplace_back(last_tag);  // last token
  }

  uint32_t types_mask = 0;
  for (auto type : types) {
    const auto dt = NameToDataType(type);
    if (dt != DataType::kTypeMax) {
      types_mask |= DataTypeToMask(dt);
    }
  }

  return types_mask;
}


std::unique_ptr<FitResult> Convert(std::unique_ptr<DataSource> data_source_ptr,
                                   const std::string_view output_type,
                                   const int64_t offset,
                                   const uint8_t smoothness,
                                   const uint32_t collect_data_types,
                                   const bool imperial) {
  auto result = std::make_unique<FitResult>();
  result->first = ParseResult::kError;

  const bool json_output = (output_type == kOutputJsonTag);
  const bool vtt_output = (output_type == kOutputVttTag);

  // used_data_types - mask of values DataType values: 0x01 << DataType
  uint32_t used_data_types{0};
  uint32_t file_items{0};
  uint32_t non_msg_counter{0};
  int64_t first_fit_timestamp{0};
  int64_t first_video_timestamp{0};

  const size_t data_source_size = data_source_ptr->GetSize();
  FIT_CONVERT_RETURN fit_status = FIT_CONVERT_CONTINUE;
  FitConvert_Init(FIT_TRUE);
  Buffer data_buffer(4096 * 16);

  OutputBuffer write_buffer;
  // start json creation
  rapidjson::Writer<rapidjson::StringBuffer> writer(write_buffer);
  write_buffer.Reserve((data_source_size == 0 ? (2048 * 1024) : (data_source_size + (data_source_size >> 2))));
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
  auto MakeExporter = [](auto& write_buffer, auto& writer, auto& vtt_output, auto& json_output, auto& imperial) {
    return [&](auto&& x) noexcept -> void {
      if (json_output) {
        x->ExportToJson(writer, imperial);
      } else if (vtt_output) {
        x->ExportToVtt(write_buffer, imperial);
      }
    };
  };
  auto Export = MakeExporter(write_buffer, writer, vtt_output, json_output, imperial);


  FitData new_fit_data;
  FitData previous_fit_data;
  FitData* new_fit_data_ptr = &new_fit_data;
  FitData* previous_fot_data_ptr = nullptr;

  while ((DataSource::Status::kError != data_source_ptr->ReadData(data_buffer)) && (fit_status == FIT_CONVERT_CONTINUE) &&
         data_buffer.GetDataSize() > 0) {
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
        }
      }

      if (offset > 0) {
        // positive offset, 'offset' second of the data from .fit file will displayed at the first second of the video
        if (type_msec < first_fit_timestamp) {
          continue;
        }
      }

      // fill data from .fit
      new_fit_data_ptr->ApplyData(fit_record_ptr, collect_data_types);

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
      if (smoothness > 0) {
        const int64_t smoothed_diff_ms = (new_fit_from_ms - new_fit_data_ptr->GetValue(DataType::kTypeTimeStamp)) / (smoothness + 1);
        new_fit_data_ptr->SetValue(DataType::kTypeTimeStampNext, new_fit_data_ptr->GetValue(DataType::kTypeTimeStamp) + smoothed_diff_ms);
        Export(new_fit_data_ptr);

        FitData diff = *previous_fot_data_ptr - *new_fit_data_ptr;
        diff = diff / (smoothness + 1);
        for (uint8_t cur_step = 0; cur_step < smoothness; ++cur_step) {
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
      writer.Uint64(used_data_types);
      writer.Key("timestamp");
      writer.Int64(first_fit_timestamp);
      writer.Key("offset");
      writer.Int64(offset);
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
