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

#include <cstdint>

#include "datasource.h"
#include "fitsdk/fit_convert.h"
#include "parser.h"

namespace {

constexpr std::string_view kSpeedTag("speed");
constexpr std::string_view kSpeedUnitsTag("mm/sec");

constexpr std::string_view kDistanceTag("distance");
constexpr std::string_view kDistanceUnitsTag("cm");

constexpr std::string_view kHeartRateTag("heartrate");
constexpr std::string_view kHeartRateUnitsTag("bpm");

constexpr std::string_view kAltitudeTag("altitude");
constexpr std::string_view kAltitudeUnitsTag("cm");

constexpr std::string_view kPowerTag("power");
constexpr std::string_view kPowerUnitsTag("w");

constexpr std::string_view kCadenceTag("cadence");
constexpr std::string_view kCadenceUnitsTag("rpm");

constexpr std::string_view kTemperatureTag("temperature");
constexpr std::string_view kTemperatureUnitsTag("c");

constexpr std::string_view kTimeStampTag("timestamp");
constexpr std::string_view kTimeStampUnitsTag("millisec");  // we convert it from seconds here

constexpr std::string_view kTimeStampNextTag("timestamp_next");
constexpr std::string_view kTimeStampNextUnitsTag("millisec");  // we convert it from seconds here

constexpr std::string_view kLatitudeTag("latitude");
constexpr std::string_view kLatitudeUnitsTag("semicircles");

constexpr std::string_view kLongitudeTag("longitude");
constexpr std::string_view kLongitudeUnitsTag("semicircles");

constexpr std::string_view kVttHeaderTag("WEBVTT\n\n");

enum class DataType : uint32_t {
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

constexpr uint32_t kDataTypeFirst = static_cast<uint32_t>(DataType::kTypeFirst);
constexpr uint32_t kDataTypeMax = static_cast<uint32_t>(DataType::kTypeMax);

uint32_t DataTypeToMask(const DataType type) {
  return 0x01 << static_cast<uint32_t>(type);
};

struct DataTagUnit {
  DataTagUnit() = default;
  DataTagUnit(std::string_view tag, std::string_view units) : data_tag(std::move(tag)), data_units(std::move(units)) {}

  bool IsValid() const { return false == data_tag.empty() && false == data_units.empty(); }

  std::string_view data_tag;
  std::string_view data_units;
};

struct Record {
  int64_t values[static_cast<uint32_t>(DataType::kTypeMax)]{};
  uint32_t Valid{0};  // mask of values DataType values: 0x01 << DataType
};

constexpr Record operator-(const Record left_value, const Record right_value) {
  Record diff_record;
  diff_record.Valid = left_value.Valid & right_value.Valid;
  for (uint32_t index = kDataTypeFirst; index < kDataTypeMax; ++index) {
    diff_record.values[index] = left_value.values[index] - right_value.values[index];
  }
  return diff_record;
};

constexpr Record operator+(const Record left_value, const Record right_value) {
  Record summ_record;
  summ_record.Valid = left_value.Valid & right_value.Valid;
  for (uint32_t index = kDataTypeFirst; index < kDataTypeMax; ++index) {
    summ_record.values[index] = left_value.values[index] + right_value.values[index];
  }
  return summ_record;
};

constexpr Record operator/(const Record left_value, const int64_t divider) {
  Record divided_record;
  divided_record.Valid = left_value.Valid;
  for (uint32_t index = kDataTypeFirst; index < kDataTypeMax; ++index) {
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

std::string NumberToStringPrecision(const int64_t number,
                                    const double divider,
                                    const size_t total_symbols,
                                    const size_t dot_limit) {
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
  switch (type) {
    case DataType::kTypeAltitude:
      return kAltitudeTag;
    case DataType::kTypeLatitude:
      return kLatitudeTag;
    case DataType::kTypeLongitude:
      return kLongitudeTag;
    case DataType::kTypeSpeed:
      return kSpeedTag;
    case DataType::kTypeDistance:
      return kDistanceTag;
    case DataType::kTypeHeartRate:
      return kHeartRateTag;
    case DataType::kTypePower:
      return kPowerTag;
    case DataType::kTypeCadence:
      return kCadenceTag;
    case DataType::kTypeTemperature:
      return kTemperatureTag;
    case DataType::kTypeTimeStamp:
      return kTimeStampTag;
    case DataType::kTypeTimeStampNext:
      return kTimeStampNextTag;
  }
  return "";
};

DataType NameToDataType(std::string_view name) {
  if (name == kAltitudeTag) {
    return DataType::kTypeAltitude;
  } else if (name == kLatitudeTag) {
    return DataType::kTypeLatitude;
  } else if (name == kLongitudeTag) {
    return DataType::kTypeLongitude;
  } else if (name == kSpeedTag) {
    return DataType::kTypeSpeed;
  } else if (name == kDistanceTag) {
    return DataType::kTypeDistance;
  } else if (name == kHeartRateTag) {
    return DataType::kTypeHeartRate;
  } else if (name == kPowerTag) {
    return DataType::kTypePower;
  } else if (name == kCadenceTag) {
    return DataType::kTypeCadence;
  } else if (name == kTemperatureTag) {
    return DataType::kTypeTemperature;
  } else if (name == kTimeStampTag) {
    return DataType::kTypeTimeStamp;
  } else if (name == kTimeStampNextTag) {
    return DataType::kTypeTimeStampNext;
  }
  return DataType::kTypeMax;
};

std::string_view DataTypeToUnit(const DataType type) {
  switch (type) {
    case DataType::kTypeAltitude:
      return kAltitudeUnitsTag;
    case DataType::kTypeLatitude:
      return kLatitudeUnitsTag;
    case DataType::kTypeLongitude:
      return kLongitudeUnitsTag;
    case DataType::kTypeSpeed:
      return kSpeedUnitsTag;
    case DataType::kTypeDistance:
      return kDistanceUnitsTag;
    case DataType::kTypeHeartRate:
      return kHeartRateUnitsTag;
    case DataType::kTypePower:
      return kPowerUnitsTag;
    case DataType::kTypeCadence:
      return kCadenceUnitsTag;
    case DataType::kTypeTemperature:
      return kTemperatureUnitsTag;
    case DataType::kTypeTimeStamp:
      return kTimeStampUnitsTag;
    case DataType::kTypeTimeStampNext:
      return kTimeStampNextUnitsTag;
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

std::unique_ptr<FitResult> FitParser(std::unique_ptr<DataSource> data_source_ptr, const uint32_t collect_data_types) {
  auto fit_result = std::make_unique<FitResult>();
  uint32_t used_data_types{0};  // mask of values DataType values: 0x01 << DataType

  const size_t data_source_size = data_source_ptr->GetSize();
  if (data_source_size > 0) {
    fit_result->result.reserve(data_source_size / 60);
  } else {
    fit_result->result.reserve(8192);  // average for 2 hours ride
  }

  FIT_CONVERT_RETURN fit_status = FIT_CONVERT_CONTINUE;
  FitConvert_Init(FIT_TRUE);
  Buffer data_buffer(4096);
  size_t errors_counter{0};
  while ((DataSource::Status::kError != data_source_ptr->ReadData(data_buffer)) &&
         (fit_status == FIT_CONVERT_CONTINUE)) {
    while (fit_status = FitConvert_Read(data_buffer.GetDataPtr(), static_cast<FIT_UINT32>(data_buffer.GetDataSize())),
           fit_status == FIT_CONVERT_MESSAGE_AVAILABLE) {
      if (FitConvert_GetMessageNumber() != FIT_MESG_NUM_RECORD) {
        if (errors_counter > data_buffer.GetBufferSize()) {
          // avoid stupid hang, not sure if it's even possible but why not
          break;
        }
        continue;
      }
      errors_counter = 0;

      const FIT_UINT8* fit_message_ptr = FitConvert_GetMessageData();
      const FIT_RECORD_MESG* fit_record_ptr = reinterpret_cast<const FIT_RECORD_MESG*>(fit_message_ptr);

      // allocate struct
      fit_result->result.emplace_back();
      // convert timestamp to milliseconds
      const int64_t type_msec = static_cast<int64_t>(fit_record_ptr->timestamp) * 1000;
      ApplyValue(fit_result->result.back(), DataType::kTypeTimeStamp, type_msec);
      ApplyValue(
          fit_result->result.back(), DataType::kTypeTimeStampNext, type_msec + 1000);  // default 1sec to next entry
      if (fit_result->result.size() > 1) {
        // fix kTypeTimeStampNext in previous entry
        ApplyValue(fit_result->result[fit_result->result.size() - 2], DataType::kTypeTimeStampNext, type_msec);
      }

      std::string output;
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

      if (fit_record_ptr->enhanced_altitude != FIT_UINT32_INVALID &&
          CheckType(collect_data_types, DataType::kTypeAltitude)) {
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

      if (fit_record_ptr->temperature != FIT_SINT8_INVALID &&
          CheckType(collect_data_types, DataType::kTypeTemperature)) {
        // FIT_SINT8 temperature = C
        ApplyValue(fit_result->result.back(), DataType::kTypeTemperature, fit_record_ptr->temperature);
      }

      if (fit_record_ptr->position_lat != FIT_SINT32_INVALID && fit_record_ptr->position_long != FIT_SINT32_INVALID &&
          CheckType(collect_data_types, DataType::kTypeLatitude) &&
          CheckType(collect_data_types, DataType::kTypeLongitude)) {
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

  // not a good idea for stream output
  SPDLOG_INFO("fit records processed: {}, source size: {}", fit_result->result.size(), data_source_size);
  return fit_result;
}

std::string convert(std::unique_ptr<DataSource> data_source_ptr,
                    const std::string_view output_type,
                    const int64_t offset,
                    const uint8_t smoothness,
                    const uint32_t datatypes) {
  std::unique_ptr<FitResult> fit_result = FitParser(std::move(data_source_ptr), datatypes);
  if (fit_result->status != ParseResult::kSuccess) {
    // error reported in parser
    throw std::runtime_error("can not parse .fit file");
  }

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

      for (uint32_t index = kDataTypeFirst; index < kDataTypeMax; ++index) {
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

  } else if (kOutputSrtTag == output_type || kOutputVttTag == output_type) {
    int64_t records_count = 0;
    int64_t first_video_timestamp = 0;
    int64_t first_fit_timestamp = 0;
    int64_t ascent = 500 * 5;   // default for altitude, because altitude: meters = (value / 5 ) - 500
    int64_t descent = 500 * 5;  // default for altitude, because altitude: meters = (value / 5 ) - 500
    int64_t previous_altitude = 0;
    bool initial_altitude_set = false;

    const size_t items_total = (smoothness + 1) * fit_result->result.size();
    std::string subtitles_data;
    subtitles_data.reserve(items_total * 128);  // 128 near to maxtimum size of ,vtt or .srt sntry
    char milliseconds_delimiter = ',';
    if (kOutputVttTag == output_type) {
      milliseconds_delimiter = '.';
      subtitles_data += kVttHeaderTag;
    }

    std::vector<Record> records_to_process;
    records_to_process.reserve(smoothness + 1);

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

      // set initial altitude
      if (false == initial_altitude_set) {
        const auto altitude_by_type = GetValueByType(original_record, DataType::kTypeAltitude);
        if (altitude_by_type.Valid()) {
          initial_altitude_set = true;
          previous_altitude = altitude_by_type.value;
        }
      }

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

        std::string output;
        const auto dst_by_type = GetValueByType(record, DataType::kTypeDistance);
        if (dst_by_type.Valid()) {
          const auto distance(NumberToStringPrecision(dst_by_type.value, 100000.0, 5, 2));
          output += fmt::format("{:>5} km", distance);
        }

        const auto hr_by_type = GetValueByType(record, DataType::kTypeHeartRate);
        if (hr_by_type.Valid()) {
          output += fmt::format("{:>5} bpm", hr_by_type.value);
        }

        const auto cadence_by_type = GetValueByType(record, DataType::kTypeCadence);
        if (cadence_by_type.Valid()) {
          output += fmt::format("{:>5} rpm", cadence_by_type.value);
        }

        const auto power_by_type = GetValueByType(record, DataType::kTypePower);
        if (power_by_type.Valid()) {
          output += fmt::format("{:>6} w", power_by_type.value);
        }

        const auto altitude_by_type = GetValueByType(record, DataType::kTypeAltitude);
        if (altitude_by_type.Valid()) {
          const int64_t altitude_diff = altitude_by_type.value - previous_altitude;
          if (altitude_diff > 0) {
            ascent += altitude_diff;
          } else {
            descent += altitude_diff;
          }
          previous_altitude = altitude_by_type.value;
          output += fmt::format("{:>5} m", (ascent / 5) - 500);
        }

        const auto speed_by_type = GetValueByType(record, DataType::kTypeSpeed);
        if (speed_by_type.Valid()) {
          const auto speed(NumberToStringPrecision(speed_by_type.value, 277.77, 5, 1));
          output += fmt::format("{:>6} km/h", speed);
        }

        const auto temp_by_type = GetValueByType(record, DataType::kTypeTemperature);
        if (temp_by_type.Valid()) {
          output += fmt::format("{:>4} C", temp_by_type.value);
        }

        const auto timestamp_by_type = GetValueByType(record, DataType::kTypeTimeStamp);
        const auto timestamp_by_type_next = GetValueByType(record, DataType::kTypeTimeStampNext);

        const int64_t current_record_from = timestamp_by_type.Valid() ? timestamp_by_type.value : 0;
        const int64_t current_record_to = timestamp_by_type_next.Valid() ? timestamp_by_type_next.value : 0;        
        const int64_t milliseconds_from = (current_record_from - first_fit_timestamp) + first_video_timestamp;
        const int64_t milliseconds_to = (current_record_to - first_fit_timestamp) + first_video_timestamp;
        const Time time_from(GetTime(milliseconds_from));
        const Time time_to(GetTime(milliseconds_to));
        const auto time_data(
            fmt::format("{}\n{:0>2d}:{:0>2d}:{:0>2d}{}{:0>3d} --> {:0>2d}:{:0>2d}:{:0>2d}{}{:0>3d}\n{}\n\n",
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
        subtitles_data += time_data;
      }
    }
    return subtitles_data;
  } else {
    throw std::runtime_error("unknown output format");
  }
}