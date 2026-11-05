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

// End to end tests of Convert() on synthetic .fit files built with fit_writer.h

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "fit_writer.h"
#include "gtest/gtest.h"
#include "parser.h"

namespace {

using fit_test::FitFileWriter;
using fit_test::kTestStartTimestamp;
using fit_test::Record;

constexpr std::string_view kAllTypes("speed,distance,heartrate,power,cadence,temperature,gradient,altitude,latitude,longitude");

struct Output {
  ParseResult status{ParseResult::kError};
  std::string text;
};

Output RunConvert(const std::vector<uint8_t>& file,
           const std::string_view type = kOutputVttTag,
           const int64_t offset = 0,
           const uint8_t smoothness = 0,
           const std::string_view data_types = kAllTypes,
           const bool imperial = false,
           const std::string& format = {}) {
  auto result = Convert(std::make_unique<DataSourceMemory>(file.data(), file.size()), type, offset, smoothness, data_types, imperial, format);
  return {result->first, std::string(result->second.GetString(), result->second.GetSize())};
}

// record with speed and heart rate only, speed in km/h
Record SpeedRecord(const uint32_t second, const double speed_kmh, const std::optional<uint8_t> heart_rate) {
  Record record;
  record.timestamp = kTestStartTimestamp + second;
  record.enhanced_speed = static_cast<uint32_t>(speed_kmh / 3.6 * 1000.0 + 0.5);
  record.heart_rate = heart_rate;
  return record;
}

std::vector<uint8_t> MakeFile(const std::vector<Record>& records, const bool with_file_id = true) {
  FitFileWriter writer;
  if (with_file_id) {
    writer.AddFileId();
  }
  for (const auto& record : records) {
    writer.AddRecord(record);
  }
  return writer.Build();
}

struct Cue {
  std::string from;
  std::string to;
  std::string text;
};

// splits vtt output into cues, skipping the header
std::vector<Cue> ParseCues(const std::string& vtt) {
  std::vector<Cue> cues;
  size_t pos = vtt.find("\n\n");
  while (pos != std::string::npos && pos + 2 < vtt.size()) {
    const size_t start = pos + 2;
    const size_t end = vtt.find("\n\n", start);
    const std::string block = vtt.substr(start, end == std::string::npos ? std::string::npos : end - start);
    const size_t line_end = block.find('\n');
    const std::string times = block.substr(0, line_end);
    const size_t arrow = times.find(" --> ");
    if (arrow != std::string::npos) {
      cues.push_back({times.substr(0, arrow), times.substr(arrow + 5), line_end == std::string::npos ? "" : block.substr(line_end + 1)});
    }
    pos = end;
  }
  return cues;
}

rapidjson::Document ParseJson(const std::string& json) {
  rapidjson::Document document;
  document.Parse(json.c_str(), json.size());
  EXPECT_FALSE(document.HasParseError());
  return document;
}

// vtt

TEST(ConvertVtt, WritesCuesAndEndMessage) {
  const auto file = MakeFile({SpeedRecord(0, 18.0, 120), SpeedRecord(1, 36.0, 121)});
  const auto output = RunConvert(file, kOutputVttTag, 0, 0, "speed,heartrate");

  ASSERT_EQ(output.status, ParseResult::kSuccess);
  EXPECT_EQ(output.text,
            "WEBVTT\n\n"
            "00:00:00.000 --> 00:00:01.000\n 18.0 km/h  120❤️ \n\n"
            "00:00:01.000 --> 00:00:02.000\n 36.0 km/h  121❤️ \n\n"
            "00:00:02.000 --> 00:01:02.000\n< no more .fit data >\nmade with ❤️ by fitconvert\n\n");
}

TEST(ConvertVtt, SingleRecordUsesOneSecondCue) {
  const auto output = RunConvert(MakeFile({SpeedRecord(0, 18.0, 120)}), kOutputVttTag, 0, 0, "heartrate");
  ASSERT_EQ(output.status, ParseResult::kSuccess);
  const auto cues = ParseCues(output.text);
  ASSERT_EQ(cues.size(), 2u);
  EXPECT_EQ(cues[0].from, "00:00:00.000");
  EXPECT_EQ(cues[0].to, "00:00:01.000");
  EXPECT_EQ(cues[0].text, " 120❤️ ");
}

TEST(ConvertVtt, FileWithoutRecordsHasOnlyHeader) {
  FitFileWriter writer;
  writer.AddFileId().AddFileId();
  const auto output = RunConvert(writer.Build());
  ASSERT_EQ(output.status, ParseResult::kSuccess);
  EXPECT_EQ(output.text, "WEBVTT\n\n");
}

TEST(ConvertVtt, FileWithoutFileIdIsAccepted) {
  const auto output = RunConvert(MakeFile({SpeedRecord(0, 18.0, 120)}, false), kOutputVttTag, 0, 0, "heartrate");
  ASSERT_EQ(output.status, ParseResult::kSuccess);
  EXPECT_EQ(ParseCues(output.text).size(), 2u);
}

TEST(ConvertVtt, PauseStretchesCueUntilNextRecord) {
  const auto output = RunConvert(MakeFile({SpeedRecord(0, 18.0, 120), SpeedRecord(300, 0.0, 100)}), kOutputVttTag, 0, 0, "heartrate");
  ASSERT_EQ(output.status, ParseResult::kSuccess);
  const auto cues = ParseCues(output.text);
  ASSERT_EQ(cues.size(), 3u);
  EXPECT_EQ(cues[0].to, "00:05:00.000");
  EXPECT_EQ(cues[1].from, "00:05:00.000");
}

TEST(ConvertVtt, PositiveOffsetSkipsLeadingData) {
  std::vector<Record> records;
  for (uint32_t i = 0; i < 5; ++i) {
    records.push_back(SpeedRecord(i, 10.0, static_cast<uint8_t>(100 + i)));
  }
  const auto output = RunConvert(MakeFile(records), kOutputVttTag, 1500, 0, "heartrate");
  ASSERT_EQ(output.status, ParseResult::kSuccess);
  const auto cues = ParseCues(output.text);
  // records at 0 s and 1 s are before the offset, 2 s is displayed at 0.5 s of the video
  ASSERT_EQ(cues.size(), 4u);
  EXPECT_EQ(cues[0].from, "00:00:00.500");
  EXPECT_EQ(cues[0].text, " 102❤️ ");
  EXPECT_EQ(cues[2].text, " 104❤️ ");
}

TEST(ConvertVtt, NegativeOffsetWritesPlaceholderCue) {
  const auto output = RunConvert(MakeFile({SpeedRecord(0, 18.0, 120), SpeedRecord(1, 18.0, 121)}), kOutputVttTag, -5000, 0, "heartrate");
  ASSERT_EQ(output.status, ParseResult::kSuccess);
  EXPECT_TRUE(output.text.starts_with(
      "WEBVTT\n\n00:00:00.000 --> 00:00:05.000\n< .fit data is not yet available >\nmade with ❤️ by fitconvert\n\n00:00:05.000 --> "))
      << output.text;
  const auto cues = ParseCues(output.text);
  ASSERT_EQ(cues.size(), 4u);
  EXPECT_EQ(cues[1].from, "00:00:05.000");
  EXPECT_EQ(cues[1].to, "00:00:06.000");
}

TEST(ConvertVtt, SmoothingInterpolatesValuesAndKeepsCuesContiguous) {
  const auto output = RunConvert(MakeFile({SpeedRecord(0, 18.0, 120), SpeedRecord(1, 36.0, 140), SpeedRecord(4, 36.0, 140)}),
                          kOutputVttTag,
                          0,
                          2,
                          "speed,heartrate");
  ASSERT_EQ(output.status, ParseResult::kSuccess);
  const auto cues = ParseCues(output.text);
  // 2 intervals * 3 steps + last record + end message
  ASSERT_EQ(cues.size(), 8u);
  EXPECT_EQ(cues[0].text, " 18.0 km/h  120❤️ ");
  EXPECT_EQ(cues[1].text, " 24.0 km/h  126❤️ ");
  EXPECT_EQ(cues[2].text, " 30.0 km/h  133❤️ ");
  EXPECT_EQ(cues[3].text, " 36.0 km/h  140❤️ ");
  EXPECT_EQ(cues[0].to, "00:00:00.333");
  EXPECT_EQ(cues[2].to, "00:00:01.000");
  EXPECT_EQ(cues[4].from, "00:00:02.000");
  for (size_t i = 1; i + 1 < cues.size(); ++i) {
    EXPECT_EQ(cues[i - 1].to, cues[i].from) << "gap before cue " << i;
  }
}

TEST(ConvertVtt, SmoothingDoesNotInventMissingValues) {
  const auto output = RunConvert(MakeFile({SpeedRecord(0, 18.0, std::nullopt), SpeedRecord(1, 18.0, 150), SpeedRecord(2, 18.0, std::nullopt)}),
                          kOutputVttTag,
                          0,
                          1,
                          "heartrate,speed");
  ASSERT_EQ(output.status, ParseResult::kSuccess);
  const auto cues = ParseCues(output.text);
  ASSERT_EQ(cues.size(), 6u);
  // no heart rate in the first record: nothing to interpolate from
  EXPECT_EQ(cues[0].text, " 18.0 km/h ");
  EXPECT_EQ(cues[1].text, " 18.0 km/h ");
  // heart rate disappears in the next record: the value is kept
  EXPECT_EQ(cues[2].text, " 150❤️  18.0 km/h ");
  EXPECT_EQ(cues[3].text, " 150❤️  18.0 km/h ");
  EXPECT_EQ(cues[4].text, " 18.0 km/h ");
}

TEST(ConvertVtt, SkipsInvalidAndNonIncreasingTimestamps) {
  std::vector<Record> records = {SpeedRecord(0, 10.0, 100), SpeedRecord(2, 10.0, 102), SpeedRecord(1, 10.0, 101), SpeedRecord(2, 10.0, 103),
                                 SpeedRecord(3, 10.0, 104)};
  Record invalid = SpeedRecord(0, 10.0, 99);
  invalid.timestamp = FIT_DATE_TIME_INVALID;
  records.insert(records.begin() + 1, invalid);

  const auto output = RunConvert(MakeFile(records), kOutputVttTag, 0, 0, "heartrate");
  ASSERT_EQ(output.status, ParseResult::kSuccess);
  const auto cues = ParseCues(output.text);
  ASSERT_EQ(cues.size(), 4u);
  EXPECT_EQ(cues[0].text, " 100❤️ ");
  EXPECT_EQ(cues[0].to, "00:00:02.000");
  EXPECT_EQ(cues[1].text, " 102❤️ ");
  EXPECT_EQ(cues[2].text, " 104❤️ ");
}

TEST(ConvertVtt, CustomFormat) {
  const auto output = RunConvert(MakeFile({SpeedRecord(0, 18.0, 120)}), kOutputVttTag, 0, 0, kAllTypes, false, "{speed:4.1f} kmh;HR {heartrate}");
  ASSERT_EQ(output.status, ParseResult::kSuccess);
  const auto cues = ParseCues(output.text);
  ASSERT_FALSE(cues.empty());
  EXPECT_EQ(cues[0].text, "18.0 kmhHR 120");
}

TEST(ConvertVtt, CustomFormatSkipsMissingValues) {
  const auto output =
      RunConvert(MakeFile({SpeedRecord(0, 18.0, std::nullopt)}), kOutputVttTag, 0, 0, kAllTypes, false, "HR {heartrate};{speed:4.1f} kmh");
  ASSERT_EQ(output.status, ParseResult::kSuccess);
  EXPECT_EQ(ParseCues(output.text)[0].text, "18.0 kmh");
}

TEST(ConvertVtt, InvalidCustomFormatFails) {
  const auto output = RunConvert(MakeFile({SpeedRecord(0, 18.0, 120)}), kOutputVttTag, 0, 0, kAllTypes, false, "{speed:d}");
  EXPECT_EQ(output.status, ParseResult::kError);
  EXPECT_TRUE(output.text.empty());
}

TEST(ConvertVtt, DataTypesSelectOutputFieldsAndOrder) {
  const auto output = RunConvert(MakeFile({SpeedRecord(0, 18.0, 120)}), kOutputVttTag, 0, 0, "heartrate,speed");
  ASSERT_EQ(output.status, ParseResult::kSuccess);
  EXPECT_EQ(ParseCues(output.text)[0].text, " 120❤️  18.0 km/h ");
}

TEST(ConvertVtt, ImperialUnits) {
  Record record = SpeedRecord(0, 0.0, std::nullopt);
  record.enhanced_speed = 4470;   // 10 mph
  record.distance = 160934;       // 1 mile
  record.enhanced_altitude = 3000;  // 100 m
  record.temperature = 25;
  const auto output = RunConvert(MakeFile({record}), kOutputVttTag, 0, 0, "speed,distance,altitude,temperature", true);
  ASSERT_EQ(output.status, ParseResult::kSuccess);
  EXPECT_EQ(ParseCues(output.text)[0].text, " 10.0 mph   1.00 mi   328 ft↑  77°F ");
}

TEST(ConvertVtt, AllFieldsMetric) {
  Record record;
  record.timestamp = kTestStartTimestamp;
  record.enhanced_speed = 5000;
  record.distance = 123456;
  record.heart_rate = 150;
  record.power = 250;
  record.cadence = 90;
  record.temperature = -3;
  record.enhanced_altitude = 3000;
  record.position_lat = 536870912;    // 45 degrees
  record.position_long = -536870912;  // -45 degrees
  record.grade = 250;  // replaced by the calculated gradient, a single point gives 0
  const auto output = RunConvert(MakeFile({record}), kOutputVttTag, 0, 0, kAllTypes);
  ASSERT_EQ(output.status, ParseResult::kSuccess);
  EXPECT_EQ(ParseCues(output.text)[0].text,
            " 18.0 km/h   1.23 km  150❤️   250⚡   90 ↻  -3°C   0.0﹪  100 m↑  45.000000°  -45.000000° ");
}

TEST(ConvertVtt, TooLongActivityThrows) {
  const auto file = MakeFile({SpeedRecord(0, 18.0, 120), SpeedRecord(100 * 3600, 18.0, 120)});
  EXPECT_THROW(RunConvert(file), std::invalid_argument);
}

// gradient

TEST(ConvertGradient, CalculatedFromDistanceAndAltitude) {
  std::vector<Record> records;
  for (uint32_t i = 0; i < 6; ++i) {
    Record record;
    record.timestamp = kTestStartTimestamp + i;
    record.distance = i * 1000;                   // 10 m per second
    record.enhanced_altitude = 3000 + i * 5;      // 1 m per second = 10%
    record.grade = 9999;                          // device value is replaced
    records.push_back(record);
  }
  const auto document = ParseJson(RunConvert(MakeFile(records), kOutputJsonTag, 0, 0, "gradient").text);
  const auto& json_records = document["records"];
  ASSERT_EQ(json_records.Size(), 6u);
  EXPECT_DOUBLE_EQ(json_records[0]["g"].GetDouble(), 0.0);
  EXPECT_DOUBLE_EQ(json_records[2]["g"].GetDouble(), 10.0);
  EXPECT_DOUBLE_EQ(json_records[5]["g"].GetDouble(), 10.0);
}

TEST(ConvertGradient, FallsBackToAltitudeField) {
  std::vector<Record> records;
  for (uint32_t i = 0; i < 4; ++i) {
    Record record;
    record.timestamp = kTestStartTimestamp + i;
    record.distance = i * 1000;
    record.altitude = static_cast<uint16_t>(3000 - i * 10);  // -2 m per 10 m = -20%
    records.push_back(record);
  }
  const auto document = ParseJson(RunConvert(MakeFile(records), kOutputJsonTag, 0, 0, "gradient,altitude").text);
  EXPECT_DOUBLE_EQ(document["records"][3]["g"].GetDouble(), -20.0);
  EXPECT_DOUBLE_EQ(document["records"][3]["a"].GetDouble(), 94.0);
}

TEST(ConvertGradient, DeviceGradeIsUsedWithoutDistance) {
  Record record;
  record.timestamp = kTestStartTimestamp;
  record.grade = -350;
  const auto document = ParseJson(RunConvert(MakeFile({record}), kOutputJsonTag, 0, 0, "gradient").text);
  EXPECT_DOUBLE_EQ(document["records"][0]["g"].GetDouble(), -3.5);
}

TEST(ConvertGradient, NotWrittenWhenNotRequested) {
  Record record;
  record.timestamp = kTestStartTimestamp;
  record.distance = 0;
  record.enhanced_altitude = 3000;
  const auto document = ParseJson(RunConvert(MakeFile({record}), kOutputJsonTag, 0, 0, "altitude").text);
  EXPECT_FALSE(document["records"][0].HasMember("g"));
  EXPECT_TRUE(document["records"][0].HasMember("a"));
}

// json

TEST(ConvertJson, WritesRecordsAndLegend) {
  const auto file = MakeFile({SpeedRecord(0, 18.0, 120), SpeedRecord(5, 36.0, std::nullopt)});
  const auto document = ParseJson(RunConvert(file, kOutputJsonTag, 0, 0, "speed,heartrate").text);

  const auto& records = document["records"];
  ASSERT_EQ(records.Size(), 2u);
  EXPECT_EQ(records[0]["f"].GetInt64(), 0);
  EXPECT_DOUBLE_EQ(records[0]["s"].GetDouble(), 18.0);
  EXPECT_EQ(records[0]["h"].GetInt(), 120);
  EXPECT_EQ(records[1]["f"].GetInt64(), 5000);
  EXPECT_FALSE(records[1].HasMember("h"));

  EXPECT_EQ(document["types"]["speed"].GetUint(), 1u);
  EXPECT_EQ(document["types"]["gradient"].GetUint(), 2048u);
  EXPECT_STREQ(document["fields"]["heartrate"].GetString(), "h");
  EXPECT_STREQ(document["fields"]["timestamp"].GetString(), "f");
  EXPECT_EQ(document["usedTypes"].GetUint(), 1u | 4u);
  EXPECT_EQ(document["timestamp"].GetInt64(), static_cast<int64_t>(kTestStartTimestamp) * 1000);
  EXPECT_EQ(document["offset"].GetInt64(), 0);
  EXPECT_STREQ(document["units"].GetString(), "metric");
}

TEST(ConvertJson, OffsetsAndUnits) {
  const auto file = MakeFile({SpeedRecord(0, 18.0, 120), SpeedRecord(1, 18.0, 120), SpeedRecord(2, 18.0, 120)});
  {
    const auto document = ParseJson(RunConvert(file, kOutputJsonTag, -2000, 0, "speed", true).text);
    EXPECT_EQ(document["records"][0]["f"].GetInt64(), 2000);
    EXPECT_EQ(document["offset"].GetInt64(), -2000);
    EXPECT_STREQ(document["units"].GetString(), "imperial");
  }
  {
    const auto document = ParseJson(RunConvert(file, kOutputJsonTag, 1000, 0, "speed").text);
    ASSERT_EQ(document["records"].Size(), 2u);
    EXPECT_EQ(document["records"][0]["f"].GetInt64(), 0);
    EXPECT_EQ(document["timestamp"].GetInt64(), static_cast<int64_t>(kTestStartTimestamp) * 1000 + 1000);
  }
}

TEST(ConvertJson, SmoothingAddsRecords) {
  const auto file = MakeFile({SpeedRecord(0, 18.0, 120), SpeedRecord(1, 36.0, 140)});
  const auto document = ParseJson(RunConvert(file, kOutputJsonTag, 0, 3, "speed").text);
  const auto& records = document["records"];
  ASSERT_EQ(records.Size(), 5u);
  EXPECT_EQ(records[1]["f"].GetInt64(), 250);
  EXPECT_DOUBLE_EQ(records[2]["s"].GetDouble(), 27.0);
}

TEST(ConvertJson, LatitudeLongitudeHaveSixDecimals) {
  Record record;
  record.timestamp = kTestStartTimestamp;
  record.position_lat = 504000000;
  record.position_long = -366000000;
  const auto document = ParseJson(RunConvert(MakeFile({record}), kOutputJsonTag, 0, 0, "latitude,longitude").text);
  constexpr double kSemicircleToDegrees = 180.0 / 2147483648.0;
  // written rounded to 6 decimal places
  EXPECT_NEAR(document["records"][0]["u"].GetDouble(), 504000000 * kSemicircleToDegrees, 5e-7);
  EXPECT_NEAR(document["records"][0]["o"].GetDouble(), -366000000 * kSemicircleToDegrees, 5e-7);
  EXPECT_NE(document["records"][0]["u"].GetDouble(), 504000000 * kSemicircleToDegrees);
}

TEST(ConvertJson, EmptyActivity) {
  FitFileWriter writer;
  writer.AddFileId();
  const auto document = ParseJson(RunConvert(writer.Build(), kOutputJsonTag).text);
  EXPECT_EQ(document["records"].Size(), 0u);
  EXPECT_EQ(document["usedTypes"].GetUint(), 0u);
}

TEST(ConvertJson, LargeFileIsReadInChunks) {
  // much bigger than the 64 KB read buffer
  const auto file = fit_test::MakeActivity(6000);
  ASSERT_GT(file.size(), 2u * 65536u);
  const auto output = RunConvert(file, kOutputJsonTag);
  ASSERT_EQ(output.status, ParseResult::kSuccess);
  const auto document = ParseJson(output.text);
  const auto& records = document["records"];
  ASSERT_EQ(records.Size(), 6000u);
  // pauses of 5 minutes after every 1800 records
  EXPECT_EQ(records[5999]["f"].GetInt64(), (5999 + 3 * 299) * 1000);
  // heart rate dropouts
  EXPECT_FALSE(records[600].HasMember("h"));
  EXPECT_TRUE(records[605].HasMember("h"));
}

// other output types and errors

TEST(ConvertOther, UnknownOutputTypeProducesNothing) {
  const auto output = RunConvert(MakeFile({SpeedRecord(0, 18.0, 120)}), "none");
  EXPECT_EQ(output.status, ParseResult::kSuccess);
  EXPECT_TRUE(output.text.empty());
}

TEST(ConvertErrors, EmptyInput) {
  EXPECT_EQ(RunConvert({}).status, ParseResult::kError);
}

TEST(ConvertErrors, BadCrc) {
  auto file = MakeFile({SpeedRecord(0, 18.0, 120)});
  file.back() ^= 0xFF;
  EXPECT_EQ(RunConvert(file).status, ParseResult::kError);
}

TEST(ConvertErrors, TruncatedFile) {
  auto file = MakeFile({SpeedRecord(0, 18.0, 120), SpeedRecord(1, 18.0, 120)});
  file.resize(file.size() - 5);
  EXPECT_EQ(RunConvert(file).status, ParseResult::kError);
}

TEST(ConvertErrors, NotFitFile) {
  FitFileWriter writer;
  writer.AddRecord(SpeedRecord(0, 18.0, 120));
  EXPECT_EQ(RunConvert(writer.Build(0x20, ".TXT")).status, ParseResult::kError);
}

TEST(ConvertErrors, UnsupportedProtocolVersion) {
  FitFileWriter writer;
  writer.AddRecord(SpeedRecord(0, 18.0, 120));
  EXPECT_EQ(RunConvert(writer.Build(0x30)).status, ParseResult::kError);
}

TEST(ConvertErrors, RandomBytes) {
  std::vector<uint8_t> garbage(1000);
  for (size_t i = 0; i < garbage.size(); ++i) {
    garbage[i] = static_cast<uint8_t>((i * 131u + 7u) & 0xFF);
  }
  EXPECT_EQ(RunConvert(garbage).status, ParseResult::kError);
}

TEST(ConvertFile, ReadsFromDataSourceFile) {
  const auto path = std::filesystem::temp_directory_path() / "fitconvert_convert_test.fit";
  const auto file = fit_test::MakeActivity(100);
  {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
  }
  const auto result = Convert(std::make_unique<DataSourceFile>(path.string()), kOutputVttTag, 0, 0, kAllTypes, false);
  std::filesystem::remove(path);
  ASSERT_EQ(result->first, ParseResult::kSuccess);
  EXPECT_EQ(ParseCues(std::string(result->second.GetString(), result->second.GetSize())).size(), 101u);
}

}  // namespace
