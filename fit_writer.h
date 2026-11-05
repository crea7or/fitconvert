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

// Minimal FIT file writer used by tests and benchmarks to build deterministic .fit files in memory.
// Only what fitconvert reads is supported: file_id and record messages with normal headers.

#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

#include "fitsdk/fit_crc.h"
#include "fitsdk/fit_example.h"

namespace fit_test {

// one record message, fields without a value are not written to the file
struct Record {
  uint32_t timestamp{0};               // seconds since UTC 00:00 Dec 31 1989
  std::optional<int32_t> position_lat;  // semicircles
  std::optional<int32_t> position_long;
  std::optional<uint16_t> altitude;  // 5 * m + 500
  std::optional<uint8_t> heart_rate;
  std::optional<uint8_t> cadence;
  std::optional<uint32_t> distance;  // 100 * m
  std::optional<uint16_t> speed;     // 1000 * m/s
  std::optional<uint16_t> power;
  std::optional<int16_t> grade;  // 100 * %
  std::optional<int8_t> temperature;
  std::optional<uint32_t> enhanced_speed;     // 1000 * m/s
  std::optional<uint32_t> enhanced_altitude;  // 5 * m + 500
};

class FitFileWriter {
 public:
  static constexpr uint8_t kRecordLocalMesg = 0;
  static constexpr uint8_t kFileIdLocalMesg = 1;

  FitFileWriter& AddFileId() {
    // definition: file_id.type (enum)
    data_.push_back(0x40 | kFileIdLocalMesg);
    PutDefinitionHeader(FIT_MESG_NUM_FILE_ID, 1);
    PutFieldDefinition(FIT_FILE_ID_FIELD_NUM_TYPE, 1, FIT_BASE_TYPE_ENUM);
    // data: activity file
    data_.push_back(kFileIdLocalMesg);
    data_.push_back(4);
    return *this;
  }

  FitFileWriter& AddRecord(const Record& record) {
    const uint32_t layout = Layout(record);
    if (!record_layout_ || *record_layout_ != layout) {
      WriteRecordDefinition(record);
      record_layout_ = layout;
    }
    data_.push_back(kRecordLocalMesg);
    PutValue(record.timestamp);
    ForEachField(record, [this](const uint8_t, const uint8_t, const uint8_t, const auto& value) {
      if (value) {
        PutValue(*value);
      }
    });
    return *this;
  }

  size_t GetDataSize() const { return data_.size(); }

  std::vector<uint8_t> Build(const uint8_t protocol_version = 0x20, const std::string_view data_type = ".FIT") const {
    std::vector<uint8_t> file;
    file.reserve(14 + data_.size() + 2);
    file.push_back(14);  // header size
    file.push_back(protocol_version);
    PutLittleEndian(file, static_cast<uint16_t>(2100));  // profile version
    PutLittleEndian(file, static_cast<uint32_t>(data_.size()));
    for (size_t i = 0; i < 4; ++i) {
      file.push_back(i < data_type.size() ? static_cast<uint8_t>(data_type[i]) : 0);
    }
    PutLittleEndian(file, FitCRC_Calc16(file.data(), static_cast<FIT_UINT32>(file.size())));
    file.insert(file.end(), data_.begin(), data_.end());
    PutLittleEndian(file, FitCRC_Calc16(file.data(), static_cast<FIT_UINT32>(file.size())));
    return file;
  }

 private:
  std::vector<uint8_t> data_;
  std::optional<uint32_t> record_layout_;

  // calls function(field_num, size, base_type, optional_value) for every optional record field
  template <typename Function>
  static void ForEachField(const Record& r, Function&& function) {
    function(FIT_RECORD_FIELD_NUM_POSITION_LAT, 4, FIT_BASE_TYPE_SINT32, r.position_lat);
    function(FIT_RECORD_FIELD_NUM_POSITION_LONG, 4, FIT_BASE_TYPE_SINT32, r.position_long);
    function(FIT_RECORD_FIELD_NUM_ALTITUDE, 2, FIT_BASE_TYPE_UINT16, r.altitude);
    function(FIT_RECORD_FIELD_NUM_HEART_RATE, 1, FIT_BASE_TYPE_UINT8, r.heart_rate);
    function(FIT_RECORD_FIELD_NUM_CADENCE, 1, FIT_BASE_TYPE_UINT8, r.cadence);
    function(FIT_RECORD_FIELD_NUM_DISTANCE, 4, FIT_BASE_TYPE_UINT32, r.distance);
    function(FIT_RECORD_FIELD_NUM_SPEED, 2, FIT_BASE_TYPE_UINT16, r.speed);
    function(FIT_RECORD_FIELD_NUM_POWER, 2, FIT_BASE_TYPE_UINT16, r.power);
    function(FIT_RECORD_FIELD_NUM_GRADE, 2, FIT_BASE_TYPE_SINT16, r.grade);
    function(FIT_RECORD_FIELD_NUM_TEMPERATURE, 1, FIT_BASE_TYPE_SINT8, r.temperature);
    function(FIT_RECORD_FIELD_NUM_ENHANCED_SPEED, 4, FIT_BASE_TYPE_UINT32, r.enhanced_speed);
    function(FIT_RECORD_FIELD_NUM_ENHANCED_ALTITUDE, 4, FIT_BASE_TYPE_UINT32, r.enhanced_altitude);
  }

  // bit mask of fields present in the record
  static uint32_t Layout(const Record& record) {
    uint32_t layout = 0;
    uint32_t bit = 1;
    ForEachField(record, [&](const uint8_t, const uint8_t, const uint8_t, const auto& value) {
      if (value) {
        layout |= bit;
      }
      bit <<= 1;
    });
    return layout;
  }

  void WriteRecordDefinition(const Record& record) {
    uint8_t fields = 1;  // timestamp
    ForEachField(record, [&](const uint8_t, const uint8_t, const uint8_t, const auto& value) { fields += value ? 1 : 0; });
    data_.push_back(0x40 | kRecordLocalMesg);
    PutDefinitionHeader(FIT_MESG_NUM_RECORD, fields);
    PutFieldDefinition(FIT_RECORD_FIELD_NUM_TIMESTAMP, 4, FIT_BASE_TYPE_UINT32);
    ForEachField(record, [this](const uint8_t field_num, const uint8_t size, const uint8_t base_type, const auto& value) {
      if (value) {
        PutFieldDefinition(field_num, size, base_type);
      }
    });
  }

  void PutDefinitionHeader(const uint16_t global_mesg_num, const uint8_t fields) {
    data_.push_back(0);  // reserved
    data_.push_back(0);  // little endian
    PutValue(global_mesg_num);
    data_.push_back(fields);
  }

  void PutFieldDefinition(const uint8_t field_num, const uint8_t size, const uint8_t base_type) {
    data_.push_back(field_num);
    data_.push_back(size);
    data_.push_back(base_type);
  }

  template <typename T>
  void PutValue(const T value) {
    PutLittleEndian(data_, value);
  }

  template <typename T>
  static void PutLittleEndian(std::vector<uint8_t>& out, const T value) {
    uint8_t bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));  // all supported platforms are little endian (fit_config.h)
    out.insert(out.end(), bytes, bytes + sizeof(T));
  }
};

// FIT timestamp of 2025-01-01 00:00:00 UTC
inline constexpr uint32_t kTestStartTimestamp = 1104537600u;

// Deterministic ride: 1 record per second, smooth speed/altitude/heart rate changes, a 5 minute pause every
// 'pause_every' records and a heart rate sensor dropout every 'dropout_every' records (0 disables them).
inline std::vector<uint8_t> MakeActivity(const size_t records, const size_t pause_every = 1800, const size_t dropout_every = 600) {
  FitFileWriter writer;
  writer.AddFileId();
  uint32_t timestamp = kTestStartTimestamp;
  double distance_m = 0.0;
  for (size_t i = 0; i < records; ++i) {
    const double t = static_cast<double>(i);
    const double speed_ms = 7.0 + 2.0 * std::sin(t / 60.0);
    distance_m += speed_ms;
    const double altitude_m = 200.0 + 50.0 * std::sin(t / 600.0);

    Record record;
    record.timestamp = timestamp;
    record.position_lat = static_cast<int32_t>(504000000 + static_cast<int64_t>(i) * 1000);
    record.position_long = static_cast<int32_t>(366000000 + static_cast<int64_t>(i) * 700);
    record.distance = static_cast<uint32_t>(distance_m * 100.0);
    record.enhanced_speed = static_cast<uint32_t>(speed_ms * 1000.0);
    record.enhanced_altitude = static_cast<uint32_t>((altitude_m + 500.0) * 5.0);
    record.power = static_cast<uint16_t>(180 + 60 * std::sin(t / 30.0));
    record.cadence = static_cast<uint8_t>(85 + 5 * std::sin(t / 20.0));
    record.temperature = static_cast<int8_t>(20 + static_cast<int>(i / 3600));
    if (dropout_every == 0 || (i % dropout_every) >= 5) {
      record.heart_rate = static_cast<uint8_t>(140 + 20 * std::sin(t / 300.0));
    }
    writer.AddRecord(record);

    timestamp += (pause_every != 0 && i > 0 && (i % pause_every) == 0) ? 300u : 1u;
  }
  return writer.Build();
}

}  // namespace fit_test
