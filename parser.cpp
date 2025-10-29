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
// defines for rapidjson
#ifndef RAPIDJSON_HAS_CXX11_RVALUE_REFS
#define RAPIDJSON_HAS_CXX11_RVALUE_REFS 1
#endif
#ifndef RAPIDJSON_HAS_STDSTRING
#define RAPIDJSON_HAS_STDSTRING 1
#endif

// rapidjson errors handling
#include <stdexcept>

#ifndef RAPIDJSON_ASSERT_THROWS
#define RAPIDJSON_ASSERT_THROWS 1
#endif
#ifdef RAPIDJSON_ASSERT
#undef RAPIDJSON_ASSERT
#endif
#define RAPIDJSON_ASSERT(x) \
  if (x)                    \
    ;                       \
  else                      \
    throw std::runtime_error("Failed: " #x);
// rapidjson errors handling


#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstdint>

#include "datasource.h"
#include "fitsdk/fit_convert.h"
#include "parser.h"

namespace {

constexpr std::string_view kVttHeaderTag("WEBVTT\n\n");
constexpr std::string_view kNoValue("---");

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
    {{"speed", "mm/sec"},           // kTypeSpeed
     {"distance", "cm"},            // kTypeDistance
     {"heartrate", "bpm"},          // kTypeHeartRate
     {"altitude", "m"},             // kTypeAltitude
     {"power", "W"},                // kTypePower
     {"cadence", "rpm"},            // kTypeCadence
     {"temperature", "c"},          // kTypeTemperature
     {"timestamp", "msec"},         // kTypeTimeStamp
     {"latitude", "semicircles"},   // kTypeLatitude
     {"longitude", "semicircles"},  // kTypeLongitude
     {"timestamp", "msec"}}         // kTypeTimeStampNext
};

using FormatData = std::array<std::pair<std::string_view, size_t>, DataType::kTypeMax>;

constexpr FormatData kDataTypeFormatStd = {
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

struct Record {
  int64_t values[DataType::kTypeMax]{};
  uint32_t Valid{0};  // mask of values DataType values: 0x01 << DataType
};

constexpr Record operator-(const Record left_value, const Record right_value) {
  Record diff_record;
  diff_record.Valid = left_value.Valid & right_value.Valid;
  for (uint32_t index = DataType::kTypeFirst; index < DataType::kTypeMax; ++index) {
    diff_record.values[index] = left_value.values[index] - right_value.values[index];
  }
  return diff_record;
};

constexpr Record operator+(const Record left_value, const Record right_value) {
  Record summ_record;
  summ_record.Valid = left_value.Valid & right_value.Valid;
  for (uint32_t index = DataType::kTypeFirst; index < DataType::kTypeMax; ++index) {
    summ_record.values[index] = left_value.values[index] + right_value.values[index];
  }
  return summ_record;
};

constexpr Record operator/(const Record left_value, const int64_t divider) {
  Record divided_record;
  divided_record.Valid = left_value.Valid;
  for (uint32_t index = DataType::kTypeFirst; index < DataType::kTypeMax; ++index) {
    divided_record.values[index] = left_value.values[index] / divider;
  }
  return divided_record;
};

struct Time {
  int64_t hours{0};
  int64_t minutes{0};
  int64_t seconds{0};
  int64_t milliseconds{0};
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

ValueByType GetValueByType(const Record& record, const DataType type) {
  ValueByType result;
  if ((record.Valid & DataTypeToMask(type)) != 0) {
    result.dt = type;
    const uint32_t data_type_index = static_cast<uint32_t>(type);
    result.value = record.values[data_type_index];
  }
  return result;
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

enum class ParseResult {
  kSuccess,
  kError,
};

struct FitResult {
  // parsing status
  ParseResult status{ParseResult::kError};

  // parsed data from file
  std::vector<Record> result;

  // header for all available types of data in this file
  std::vector<DataTagUnit> header;
  // header in bitmask format
  uint32_t header_flags{0};
};

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

void ApplyValue(Record& new_record, const DataType data_type, const int64_t value) {
  const uint32_t data_type_index = static_cast<uint32_t>(data_type);
  new_record.values[data_type_index] = value;
  new_record.Valid |= DataTypeToMask(data_type);
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

std::string ProcessStraightValue(const DataType type, const Record& record, const uint32_t header_flags, const FormatData& format) {
  const auto record_value = GetValueByType(record, type);
  if (header_flags & kDataTypeMasks[type]) {
    if (record_value.Valid()) {
      return FormatValue(type, record_value.value, format);
    } else {
      return FormatValue(type, kNoValue, format);
    }
  }
  return "";
}

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

std::unique_ptr<FitResult> FitParser(std::unique_ptr<DataSource> data_source_ptr,
                                     const uint32_t collect_data_types,
                                     const uint8_t smoothness) {
  auto fit_result = std::make_unique<FitResult>();
  uint32_t used_data_types{0};  // mask of values DataType values: 0x01 << DataType

  const size_t data_source_size = data_source_ptr->GetSize();
  if (data_source_size > 0) {
    fit_result->result.reserve((data_source_size / 60) * (smoothness + 1));
  } else {
    fit_result->result.reserve(8192 * (smoothness + 1));  // average for 2 hours ride
  }

  FIT_CONVERT_RETURN fit_status = FIT_CONVERT_CONTINUE;
  FitConvert_Init(FIT_TRUE);
  Buffer data_buffer(4096);
  size_t errors_counter{0};
  while ((DataSource::Status::kError != data_source_ptr->ReadData(data_buffer)) && (fit_status == FIT_CONVERT_CONTINUE)) {
    while (fit_status = FitConvert_Read(data_buffer.GetDataPtr(), static_cast<FIT_UINT32>(data_buffer.GetDataSize())),
           fit_status == FIT_CONVERT_MESSAGE_AVAILABLE) {
      if (FitConvert_GetMessageNumber() != FIT_MESG_NUM_RECORD) {
        if (errors_counter > data_buffer.GetBufferSize()) {
          // avoid stupid hang, not sure if it's even possible but why not
          throw std::exception("defective .fit file");
        }        
        continue;
      }
      ++errors_counter;

      const FIT_UINT8* fit_message_ptr = FitConvert_GetMessageData();
      const FIT_RECORD_MESG* fit_record_ptr = reinterpret_cast<const FIT_RECORD_MESG*>(fit_message_ptr);

      // allocate struct
      fit_result->result.emplace_back();
      // convert timestamp to milliseconds
      const int64_t type_msec = static_cast<int64_t>(fit_record_ptr->timestamp) * 1000;
      ApplyValue(fit_result->result.back(), DataType::kTypeTimeStamp, type_msec);
      ApplyValue(fit_result->result.back(), DataType::kTypeTimeStampNext, type_msec + 1000);  // default 1sec to next entry
      if (fit_result->result.size() > 1) {
        // fix kTypeTimeStampNext in previous entry
        ApplyValue(fit_result->result[fit_result->result.size() - 2], DataType::kTypeTimeStampNext, type_msec);
      }

      if (fit_record_ptr->distance != FIT_UINT32_INVALID && CheckType(collect_data_types, DataType::kTypeDistance)) {
        // FIT_UINT32 distance = 100 * m = cm
        ApplyValue(fit_result->result.back(), DataType::kTypeDistance, fit_record_ptr->distance);
      }

      if (fit_record_ptr->heart_rate != FIT_BYTE_INVALID && CheckType(collect_data_types, DataType::kTypeHeartRate)) {
        // FIT_UINT8 heart_rate = bpm
        ApplyValue(fit_result->result.back(), DataType::kTypeHeartRate, fit_record_ptr->heart_rate);
      }

      if (fit_record_ptr->cadence != FIT_BYTE_INVALID && CheckType(collect_data_types, DataType::kTypeCadence)) {
        // FIT_UINT8 cadence = rpm
        ApplyValue(fit_result->result.back(), DataType::kTypeCadence, fit_record_ptr->cadence);
      }

      if (fit_record_ptr->power != FIT_UINT16_INVALID && CheckType(collect_data_types, DataType::kTypePower)) {
        // FIT_UINT16 power = watts
        ApplyValue(fit_result->result.back(), DataType::kTypePower, fit_record_ptr->power);
      }

      if (fit_record_ptr->altitude != FIT_UINT16_INVALID && CheckType(collect_data_types, DataType::kTypeAltitude)) {
        // FIT_UINT16 altitude = 5 * m + 500
        ApplyValue(fit_result->result.back(), DataType::kTypeAltitude, fit_record_ptr->altitude);
      }

      if (fit_record_ptr->enhanced_altitude != FIT_UINT32_INVALID && CheckType(collect_data_types, DataType::kTypeAltitude)) {
        // FIT_UINT32 enhanced_altitude = 5 * m + 500
        ApplyValue(fit_result->result.back(), DataType::kTypeAltitude, fit_record_ptr->enhanced_altitude);
      }

      if (fit_record_ptr->speed != FIT_UINT16_INVALID && CheckType(collect_data_types, DataType::kTypeSpeed)) {
        // FIT_UINT16 speed = 1000 * m/s = mm/s
        ApplyValue(fit_result->result.back(), DataType::kTypeSpeed, fit_record_ptr->speed);
      }

      if (fit_record_ptr->enhanced_speed != FIT_UINT32_INVALID && CheckType(collect_data_types, DataType::kTypeSpeed)) {
        // FIT_UINT32 enhanced_speed = 1000 * m/s = mm/s
        ApplyValue(fit_result->result.back(), DataType::kTypeSpeed, fit_record_ptr->enhanced_speed);
      }

      if (fit_record_ptr->temperature != FIT_SINT8_INVALID && CheckType(collect_data_types, DataType::kTypeTemperature)) {
        // FIT_SINT8 temperature = C
        ApplyValue(fit_result->result.back(), DataType::kTypeTemperature, fit_record_ptr->temperature);
      }

      if (fit_record_ptr->position_lat != FIT_SINT32_INVALID && fit_record_ptr->position_long != FIT_SINT32_INVALID &&
          CheckType(collect_data_types, DataType::kTypeLatitude) && CheckType(collect_data_types, DataType::kTypeLongitude)) {
        // FIT_SINT32 position_lat = semicircles
        // FIT_SINT32 position_long = semicircles
        ApplyValue(fit_result->result.back(), DataType::kTypeLatitude, fit_record_ptr->position_lat);
        ApplyValue(fit_result->result.back(), DataType::kTypeLongitude, fit_record_ptr->position_long);
      }

      // apply to global flags
      used_data_types |= fit_result->result.back().Valid;
    }
  }

  if (fit_status == FIT_CONVERT_END_OF_FILE) {
    // success
    fit_result->status = ParseResult::kSuccess;
    fit_result->header_flags = used_data_types;

    HeaderItem(fit_result->header, used_data_types, DataType::kTypeAltitude);
    HeaderItem(fit_result->header, used_data_types, DataType::kTypeCadence);
    HeaderItem(fit_result->header, used_data_types, DataType::kTypeDistance);
    HeaderItem(fit_result->header, used_data_types, DataType::kTypeHeartRate);
    HeaderItem(fit_result->header, used_data_types, DataType::kTypeLatitude);
    HeaderItem(fit_result->header, used_data_types, DataType::kTypeLongitude);
    HeaderItem(fit_result->header, used_data_types, DataType::kTypePower);
    HeaderItem(fit_result->header, used_data_types, DataType::kTypeSpeed);
    HeaderItem(fit_result->header, used_data_types, DataType::kTypeTemperature);
    HeaderItem(fit_result->header, used_data_types, DataType::kTypeTimeStamp);

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
  SPDLOG_INFO("fit records processed: {}, source size: {}", fit_result->result.size(), data_source_size);
  return fit_result;
}

std::string convert(std::unique_ptr<DataSource> data_source_ptr,
                    const std::string_view output_type,
                    const int64_t offset,
                    const uint8_t smoothness,
                    const uint32_t datatypes,
                    const bool imperial) {
  std::unique_ptr<FitResult> fit_result = FitParser(std::move(data_source_ptr), datatypes, smoothness);
  if (fit_result->status != ParseResult::kSuccess) {
    // error reported in parser
    throw std::runtime_error("can not parse .fit file");
  }
  const bool vtt_fomat = kOutputVttTag == output_type;

  if (output_type == kOutputJsonTag) {
    rapidjson::StringBuffer string_buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);
    writer.StartObject();
    // header
    writer.Key("header");
    writer.StartArray();
    // header objects
    for (const auto& header_item : fit_result->header) {
      writer.StartObject();
      writer.Key("data");
      writer.String(header_item.data_tag.data(), static_cast<rapidjson::SizeType>(header_item.data_tag.size()));
      writer.Key("units");
      writer.String(header_item.data_units.data(), static_cast<rapidjson::SizeType>(header_item.data_units.size()));
      writer.EndObject();
    }
    writer.EndArray();
    // records
    writer.Key("records");
    writer.StartArray();

    for (const auto& item : fit_result->result) {
      writer.StartObject();

      for (uint32_t index = DataType::kTypeFirst; index < DataType::kTypeMax; ++index) {
        const auto value_by_type = GetValueByType(item, static_cast<DataType>(index));
        if (value_by_type.Valid()) {
          const auto name = DataTypeToName(value_by_type.dt);
          writer.Key(name.data(), static_cast<rapidjson::SizeType>(name.size()));
          writer.Int64(value_by_type.value);
        }
      }

      writer.EndObject();
    }

    writer.EndArray();
    writer.EndObject();

    return std::string(string_buffer.GetString(), string_buffer.GetSize());

  } else if (vtt_fomat || kOutputSrtTag == output_type) {
    const FormatData& format = imperial ? kDataTypeFormatImp : kDataTypeFormatStd;

    int64_t records_count = 0;
    int64_t first_video_timestamp = 0;
    int64_t first_fit_timestamp = 0;
    int64_t last_subtitle_timestamp = 0;

    const size_t items_total = (smoothness + 1) * fit_result->result.size();
    std::string subtitles_data;
    subtitles_data.reserve(items_total * 128);  // 128 near to maxtimum size of ,vtt or .srt sntry
    char milliseconds_delimiter = ',';

    if (vtt_fomat) {
      milliseconds_delimiter = '.';
      subtitles_data += kVttHeaderTag;
    }

    std::vector<Record> records_to_process;
    records_to_process.reserve(smoothness + 1);
    std::string output;
    output.reserve(128);

    size_t valid_value_count = 0;
    for (size_t index = 0; index < fit_result->result.size(); ++index) {
      const auto& original_record = fit_result->result[index];

      const auto record_time_by_type = GetValueByType(original_record, DataType::kTypeTimeStamp);
      const int64_t record_timestamp = record_time_by_type.Valid() ? record_time_by_type.value : 0;

      // fit timestamp should not be 0, because it's milliseconds since UTC 00:00 Dec 31 1989
      if (0 == first_fit_timestamp) {
        first_fit_timestamp = record_timestamp;
        if (offset > 0) {
          first_fit_timestamp += offset;
        } else if (offset < 0) {
          first_video_timestamp = std::abs(offset);
          // subtitles.emplace_back(records_count++, 0, 0, "< .fit data is not available >");
        }
      }

      if (offset > 0) {
        // positive offset, 'offset' second of the data from .fit file will displayed at the first second of the video
        if (record_timestamp < first_fit_timestamp) {
          continue;
        }
      }

      // clear previous data
      records_to_process.clear();

      // smoothness
      if (valid_value_count > 0 && smoothness > 0) {
        Record start_from = fit_result->result[index - 1];

        Record diff = original_record - start_from;
        diff = diff / (smoothness + 1);
        for (int64_t cur_step = 0; cur_step < smoothness; ++cur_step) {
          start_from = start_from + diff;
          records_to_process.push_back(start_from);
        }
      }

      records_to_process.push_back(original_record);

      for (auto& record : records_to_process) {
        // we use it instead of index > 0
        ++valid_value_count;
        output.clear();


        const auto dst_by_type = GetValueByType(record, DataType::kTypeDistance);
        if (dst_by_type.Valid()) {
          const auto distance(NumberToStringPrecision(dst_by_type.value, imperial ? 160934.4 : 100000.0, 5, 2));
          output += FormatValue(DataType::kTypeDistance, distance, format);
        }

        const auto speed_by_type = GetValueByType(record, DataType::kTypeSpeed);
        if (speed_by_type.Valid()) {
          const auto speed(NumberToStringPrecision(speed_by_type.value, imperial ? 447.2136 : 277.77, 4, 1));
          output += FormatValue(DataType::kTypeSpeed, speed, format);
        }

        output += ProcessStraightValue(DataType::kTypeHeartRate, record, fit_result->header_flags, format);

        output += ProcessStraightValue(DataType::kTypeCadence, record, fit_result->header_flags, format);

        output += ProcessStraightValue(DataType::kTypePower, record, fit_result->header_flags, format);

        const auto altitude_by_type = GetValueByType(record, DataType::kTypeAltitude);
        if (altitude_by_type.Valid()) {
          const int64_t altitude_meters = (altitude_by_type.value / 5) - 500;
          const int64_t altitude_value = imperial ? int64_t(altitude_meters * 3.28084) : altitude_meters;
          output += FormatValue(DataType::kTypeAltitude, altitude_value, format);
        }

        const auto temp_by_type = GetValueByType(record, DataType::kTypeTemperature);
        if (temp_by_type.Valid()) {
          const int64_t temperature_value = imperial ? (temp_by_type.value * 9.0 / 5.0 + 32.0) : temp_by_type.value;
          output += FormatValue(DataType::kTypeTemperature, temperature_value, format);
        }

        const auto timestamp_by_type = GetValueByType(record, DataType::kTypeTimeStamp);
        const auto timestamp_by_type_next = GetValueByType(record, DataType::kTypeTimeStampNext);
        const int64_t current_record_from = timestamp_by_type.Valid() ? timestamp_by_type.value : 0;
        const int64_t current_record_to = timestamp_by_type_next.Valid() ? timestamp_by_type_next.value : 0;
        const int64_t milliseconds_from = (current_record_from - first_fit_timestamp) + first_video_timestamp;
        const int64_t milliseconds_to = (current_record_to - first_fit_timestamp) + first_video_timestamp;
        const Time time_from(GetTime(milliseconds_from));
        const Time time_to(GetTime(milliseconds_to));
        if (vtt_fomat) {
          const auto entry_data(fmt::format("{:0>2d}:{:0>2d}:{:0>2d}{}{:0>3d} --> {:0>2d}:{:0>2d}:{:0>2d}{}{:0>3d}\n{}\n\n",
                                            time_from.hours,
                                            time_from.minutes,
                                            time_from.seconds,
                                            milliseconds_delimiter,
                                            time_from.milliseconds,
                                            time_to.hours,
                                            time_to.minutes,
                                            time_to.seconds,
                                            milliseconds_delimiter,
                                            time_to.milliseconds,
                                            output));
          subtitles_data += entry_data;
        } else {
          const auto entry_data(fmt::format("{}\n{:0>2d}:{:0>2d}:{:0>2d}{}{:0>3d} --> {:0>2d}:{:0>2d}:{:0>2d}{}{:0>3d}\n{}\n\n",
                                            valid_value_count,
                                            time_from.hours,
                                            time_from.minutes,
                                            time_from.seconds,
                                            milliseconds_delimiter,
                                            time_from.milliseconds,
                                            time_to.hours,
                                            time_to.minutes,
                                            time_to.seconds,
                                            milliseconds_delimiter,
                                            time_to.milliseconds,
                                            output));
          subtitles_data += entry_data;
        }
      }
    }
    return subtitles_data;
  } else {
    throw std::runtime_error("unknown output format");
  }
}