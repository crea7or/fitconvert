/*

 MIT License

 Copyright (c) 2025 pavel.sokolov@gmail.com / CEZEO software Ltd. All rights reserved.

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

#include <spdlog/spdlog.h>

#include <array>
#include <cstring>

#include "gtest/gtest.h"
#include "parser.cpp"


namespace {

FIT_RECORD_MESG MakeRecord() {
  FIT_RECORD_MESG record{};
  record.timestamp = 123;
  record.distance = FIT_UINT32_INVALID;
  record.heart_rate = FIT_BYTE_INVALID;
  record.cadence = FIT_BYTE_INVALID;
  record.power = FIT_UINT16_INVALID;
  record.altitude = FIT_UINT16_INVALID;
  record.enhanced_altitude = FIT_UINT32_INVALID;
  record.speed = FIT_UINT16_INVALID;
  record.enhanced_speed = FIT_UINT32_INVALID;
  record.temperature = FIT_SINT8_INVALID;
  record.position_lat = FIT_SINT32_INVALID;
  record.position_long = FIT_SINT32_INVALID;
  record.grade = FIT_SINT16_INVALID;
  return record;
}

TEST(ValuesFormatting, Positive) {
  std::array<char, 32> buffer;
  {
    const Time time(100);
    EXPECT_EQ(time.milliseconds, 100);
    EXPECT_EQ(time.seconds, 0);
    EXPECT_EQ(time.minutes, 0);
    EXPECT_EQ(time.hours, 0);
    const size_t result = format_timestamp(buffer.data(), buffer.size(), time);
    EXPECT_TRUE(result > 0);
    EXPECT_EQ(std::string_view("00:00:00.100"), std::string_view(buffer.data(), result));
  }
  {
    const Time time(1100);
    EXPECT_EQ(time.milliseconds, 100);
    EXPECT_EQ(time.seconds, 1);
    EXPECT_EQ(time.minutes, 0);
    EXPECT_EQ(time.hours, 0);
    const size_t result = format_timestamp(buffer.data(), buffer.size(), time);
    EXPECT_TRUE(result > 0);
    EXPECT_EQ(std::string_view("00:00:01.100"), std::string_view(buffer.data(), result));
  }
  {
    const Time time(11111);
    EXPECT_EQ(time.milliseconds, 111);
    EXPECT_EQ(time.seconds, 11);
    EXPECT_EQ(time.minutes, 0);
    EXPECT_EQ(time.hours, 0);
    const size_t result = format_timestamp(buffer.data(), buffer.size(), time);
    EXPECT_TRUE(result > 0);
    EXPECT_EQ(std::string_view("00:00:11.111"), std::string_view(buffer.data(), result));
  }
  {
    const Time time(123456);
    EXPECT_EQ(time.milliseconds, 456);
    EXPECT_EQ(time.seconds, 3);
    EXPECT_EQ(time.minutes, 2);
    EXPECT_EQ(time.hours, 0);
    const size_t result = format_timestamp(buffer.data(), buffer.size(), time);
    EXPECT_TRUE(result > 0);
    EXPECT_EQ(std::string_view("00:02:03.456"), std::string_view(buffer.data(), result));
  }
  {
    const Time time(123456789);
    EXPECT_EQ(time.milliseconds, 789);
    EXPECT_EQ(time.seconds, 36);
    EXPECT_EQ(time.minutes, 17);
    EXPECT_EQ(time.hours, 34);
    const size_t result = format_timestamp(buffer.data(), buffer.size(), time);
    EXPECT_TRUE(result > 0);
    EXPECT_EQ(std::string_view("34:17:36.789"), std::string_view(buffer.data(), result));
  }
}

TEST(ValuesFormatting, Negative) {
  { EXPECT_THROW(Time(1234567890), std::invalid_argument); }
  { EXPECT_THROW(Time(-1), std::invalid_argument); }
}

TEST(ValuesFormatting, EdgeCases) {
  std::array<char, 32> buffer;

  {
    const Time time(356400000);
    EXPECT_EQ(time.hours, 99);
    EXPECT_EQ(time.minutes, 0);
    EXPECT_EQ(time.seconds, 0);
    EXPECT_EQ(time.milliseconds, 0);
    const size_t result = format_timestamp(buffer.data(), buffer.size(), time);
    EXPECT_EQ(std::string_view("99:00:00.000"), std::string_view(buffer.data(), result));
  }

  {
    const Time time(0);
    EXPECT_EQ(format_timestamp(buffer.data(), 15, time), 0);
  }
}

TEST(ValuesFormattingWithSuffix, Positive) {
  std::array<char, 32> buffer;
  const FormatData kMetricFormat = {
      {{" km/h ", 5},  // kTypeSpeed km/h
       {" km ", 6},    // kTypeDistance km
       {" bpm ", 4},   // kTypeHeartRate bpm
       {" m ", 5},     // kTypeAltitude meters
       {" w ", 5},     // kTypePower watt
       {" rpm ", 4},   // kTypeCadence rotations rpm
       {" C ", 3},     // kTypeTemperature celsius
       {"", 0},        // kTypeTimeStamp
       {"", 0},        // kTypeLatitude
       {"", 0},        // kTypeLongitude
       {"", 0},        // kTypeTimeStampNext
       {"%", 5}}       // kTypeGradient
  };

  {
    const size_t size = format_value_suffix(0.123,
                                            buffer.data(),
                                            buffer.size(),
                                            kMetricFormat[DataType::kTypeDistance].second,
                                            kMetricFormat[DataType::kTypeDistance].first,
                                            2);
    EXPECT_EQ(std::string_view(buffer.data(), size), std::string_view("  0.12 km "));
  }

  {
    const size_t size = format_value_suffix(0.123, buffer.data(), buffer.size(), 5, kMetricFormat[DataType::kTypeDistance].first, 2);
    EXPECT_EQ(std::string_view(buffer.data(), size), std::string_view(" 0.12 km "));
  }

  {
    const size_t size = format_value_suffix(0.123, buffer.data(), buffer.size(), 4, kMetricFormat[DataType::kTypeDistance].first, 3);
    EXPECT_EQ(std::string_view(buffer.data(), size), std::string_view("0.123 km "));
  }

  {
    const size_t size = format_value_suffix(0.123, buffer.data(), buffer.size(), 4, kMetricFormat[DataType::kTypeDistance].first, 1);
    EXPECT_EQ(std::string_view(buffer.data(), size), std::string_view(" 0.1 km "));
  }

  {
    const size_t size = format_value_suffix(
        1.234, buffer.data(), buffer.size(), kMetricFormat[DataType::kTypeSpeed].second, kMetricFormat[DataType::kTypeSpeed].first, 1);
    EXPECT_EQ(std::string_view(buffer.data(), size), std::string_view("  1.2 km/h "));
  }

  {
    const size_t size = format_value_suffix(12345,
                                            buffer.data(),
                                            buffer.size(),
                                            kMetricFormat[DataType::kTypeAltitude].second,
                                            kMetricFormat[DataType::kTypeAltitude].first,
                                            0);
    EXPECT_EQ(std::string_view(buffer.data(), size), std::string_view("12345 m "));
  }

  {
    const size_t size = format_value_suffix(1234567,
                                            buffer.data(),
                                            buffer.size(),
                                            kMetricFormat[DataType::kTypeAltitude].second,
                                            kMetricFormat[DataType::kTypeAltitude].first,
                                            0);
    EXPECT_EQ(std::string_view(buffer.data(), size), std::string_view("1234567 m "));
  }
}

TEST(ValuesFormattingWithSuffix, BufferTooSmall) {
  std::array<char, 8> buffer;
  EXPECT_EQ(format_value_suffix(12.3, buffer.data(), buffer.size(), 4, " km ", 1), 0);
}

TEST(DataTypes, NamesToMaskAndIndexes) {
  const uint32_t mask = DataTypeNamesToMask("speed,invalid,distance,speed,timestampnext");
  EXPECT_EQ(mask,
            kDataTypeMasks[DataType::kTypeSpeed] | kDataTypeMasks[DataType::kTypeDistance] |
                kDataTypeMasks[DataType::kTypeTimeStampNext]);

  const auto indexes = DataTypeNamesToIndexes("temperature,invalid,power,speed");
  ASSERT_EQ(indexes.size(), 3);
  EXPECT_EQ(indexes[0], DataType::kTypeTemperature);
  EXPECT_EQ(indexes[1], DataType::kTypePower);
  EXPECT_EQ(indexes[2], DataType::kTypeSpeed);
}

TEST(DataTypes, NameToDataTypeMapsKnownAndUnknownNames) {
  EXPECT_EQ(NameToDataType("speed"), DataType::kTypeSpeed);
  EXPECT_EQ(NameToDataType("distance"), DataType::kTypeDistance);
  EXPECT_EQ(NameToDataType("timestampnext"), DataType::kTypeTimeStampNext);
  EXPECT_EQ(NameToDataType("unknown"), DataType::kTypeMax);
}

TEST(StringHelpers, EachSliceSkipsEmptySegments) {
  std::vector<std::string> segments;
  const std::string input = ",speed,,distance,";

  each_slice(input.begin(), input.end(), ',', [&segments](std::string_view segment) { segments.emplace_back(segment); });

  ASSERT_EQ(segments.size(), 2);
  EXPECT_EQ(segments[0], "speed");
  EXPECT_EQ(segments[1], "distance");
}

TEST(StringHelpers, EachSliceSupportsSingleSegmentWithoutDelimiter) {
  std::vector<std::string> segments;
  const std::string input = "temperature";

  each_slice(input.begin(), input.end(), ',', [&segments](std::string_view segment) { segments.emplace_back(segment); });

  ASSERT_EQ(segments.size(), 1);
  EXPECT_EQ(segments[0], "temperature");
}

TEST(GradientCalculatorTests, CalculatesSlopeAndHandlesShortWindow) {
  GradientCalculator gradient(5);
  // 10 m steps with 2 m (10 raw) climb = 20%
  EXPECT_EQ(gradient.AddRawPoint(0, 0), 0);
  EXPECT_EQ(gradient.AddRawPoint(1000, 10), 0);
  EXPECT_EQ(gradient.AddRawPoint(2000, 20), 2000);
  EXPECT_EQ(gradient.AddRawPoint(3000, 30), 2000);
}

TEST(GradientCalculatorTests, KeepsLastGradeWhenDistanceSpanIsTooShort) {
  GradientCalculator gradient(3);
  EXPECT_EQ(gradient.AddRawPoint(0, 0), 0);
  EXPECT_EQ(gradient.AddRawPoint(1000, 5), 0);
  EXPECT_EQ(gradient.AddRawPoint(2000, 10), 1000);
  // the window still spans 10 m here
  const int16_t before_stop = gradient.AddRawPoint(2001, 10);
  // nearly stopped: altitude noise over a few centimeters must not produce a spike
  EXPECT_EQ(gradient.AddRawPoint(2002, 15), before_stop);
  EXPECT_EQ(gradient.AddRawPoint(2003, 5), before_stop);
}

TEST(GradientCalculatorTests, ClampsUnrealisticGrade) {
  GradientCalculator gradient(3);
  EXPECT_EQ(gradient.AddRawPoint(0, 0), 0);
  EXPECT_EQ(gradient.AddRawPoint(500, 1000), 0);
  EXPECT_EQ(gradient.AddRawPoint(1000, 2000), GradientCalculator::kMaxGrade);
}

TEST(GradientCalculatorTests, ReturnsZeroWhenDistanceDoesNotChange) {
  GradientCalculator gradient(5);
  EXPECT_EQ(gradient.AddRawPoint(100, 10), 0);
  EXPECT_EQ(gradient.AddRawPoint(100, 20), 0);
  EXPECT_EQ(gradient.AddRawPoint(100, 30), 0);
}

TEST(CustomFormatterTests, UsesOrderWhenNoCustomFormatProvided) {
  const CustomFormater formatter("", "speed,distance,temperature", false);

  EXPECT_EQ(formatter.FieldsInitialized(), 0);
  EXPECT_FALSE(formatter.IsImperial());
  EXPECT_EQ(formatter.GetDataTypesMask(),
            kDataTypeMasks[DataType::kTypeSpeed] | kDataTypeMasks[DataType::kTypeDistance] |
                kDataTypeMasks[DataType::kTypeTemperature]);

  const auto& indexes = formatter.GetDataTypeIndexes();
  ASSERT_EQ(indexes.size(), 3);
  EXPECT_EQ(indexes[0], DataType::kTypeSpeed);
  EXPECT_EQ(indexes[1], DataType::kTypeDistance);
  EXPECT_EQ(indexes[2], DataType::kTypeTemperature);
}

TEST(CustomFormatterTests, ParsesCustomFormatAndCollectMaskSeparately) {
  const CustomFormater formatter("{speed:5.1f};{altitude:4};{temperature:3}", "speed,temperature", true);

  EXPECT_EQ(formatter.FieldsInitialized(), 3);
  EXPECT_TRUE(formatter.IsImperial());
  EXPECT_EQ(formatter.GetDataTypesMask(), kDataTypeMasks[DataType::kTypeSpeed] | kDataTypeMasks[DataType::kTypeTemperature]);

  const auto& indexes = formatter.GetDataTypeIndexes();
  ASSERT_EQ(indexes.size(), 3);
  EXPECT_EQ(indexes[0], DataType::kTypeSpeed);
  EXPECT_EQ(indexes[1], DataType::kTypeAltitude);
  EXPECT_EQ(indexes[2], DataType::kTypeTemperature);
  EXPECT_EQ(formatter.GetFormat(DataType::kTypeSpeed), "{:5.1f}");
  EXPECT_EQ(formatter.GetFormat(DataType::kTypeAltitude), "{:4}");
  EXPECT_EQ(formatter.GetFormat(DataType::kTypeTemperature), "{:3}");
}

TEST(CustomFormatterTests, IgnoresInvalidFormatFragments) {
  const CustomFormater formatter("speed:5.1f;{distance:4};;", "distance", false);

  EXPECT_EQ(formatter.FieldsInitialized(), 1);
  const auto& indexes = formatter.GetDataTypeIndexes();
  ASSERT_EQ(indexes.size(), 1);
  EXPECT_EQ(indexes[0], DataType::kTypeDistance);
  EXPECT_EQ(formatter.GetFormat(DataType::kTypeDistance), "{:4}");
}

TEST(FitDataTests, ApplyDataPrefersEnhancedFieldsAndSkipsInvalidValues) {
  auto record = MakeRecord();
  record.distance = 12345;
  record.speed = 1500;
  record.enhanced_speed = 2500;
  record.altitude = 750;
  record.enhanced_altitude = 1750;
  record.temperature = 24;
  record.grade = 321;

  FitData data(&record, std::numeric_limits<uint32_t>::max());

  EXPECT_EQ(data.GetValue(DataType::kTypeTimeStamp), 123000);
  EXPECT_EQ(data.GetValue(DataType::kTypeDistance), 12345);
  EXPECT_EQ(data.GetValue(DataType::kTypeSpeed), 2500);
  EXPECT_EQ(data.GetValue(DataType::kTypeAltitude), 1750);
  EXPECT_EQ(data.GetValue(DataType::kTypeTemperature), 24);
  EXPECT_EQ(data.GetValue(DataType::kTypeGradient), 321);
  EXPECT_FALSE(data.GetTypes() & kDataTypeMasks[DataType::kTypeHeartRate]);
}

TEST(FitDataTests, ExportToJsonUsesAliasesAndConversions) {
  auto record = MakeRecord();
  record.distance = 12345;
  record.enhanced_speed = 2500;
  record.enhanced_altitude = 1750;
  record.temperature = 25;
  record.heart_rate = 150;
  record.position_lat = 1073741824;
  record.position_long = -1073741824;

  FitData data(&record, std::numeric_limits<uint32_t>::max());
  const CustomFormater formatter("", "speed,distance,heartrate,altitude,temperature,latitude,longitude", false);
  rapidjson::StringBuffer json_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(json_buffer);

  data.ExportToJson(writer, formatter);

  rapidjson::Document doc;
  doc.Parse(json_buffer.GetString());
  ASSERT_FALSE(doc.HasParseError());
  EXPECT_DOUBLE_EQ(doc["s"].GetDouble(), 9.0);
  EXPECT_DOUBLE_EQ(doc["d"].GetDouble(), 0.12);
  EXPECT_EQ(doc["h"].GetInt(), 150);
  EXPECT_DOUBLE_EQ(doc["a"].GetDouble(), -150.0);
  EXPECT_EQ(doc["t"].GetInt(), 25);
  EXPECT_DOUBLE_EQ(doc["u"].GetDouble(), 90.0);
  EXPECT_DOUBLE_EQ(doc["o"].GetDouble(), -90.0);
  EXPECT_EQ(doc["f"].GetInt64(), 123000);
}

TEST(FitDataTests, ExportToVttWritesExpectedCue) {
  auto record = MakeRecord();
  record.distance = 12345;
  record.enhanced_speed = 2500;

  FitData data(&record, std::numeric_limits<uint32_t>::max());
  data.SetValue(DataType::kTypeTimeStamp, 1000);
  data.SetValue(DataType::kTypeTimeStampNext, 2500);

  const CustomFormater formatter("", "speed,distance", false);
  OutputBuffer out;
  data.ExportToVtt(out, formatter);

  EXPECT_EQ(std::string_view(out.GetString(), out.GetSize()),
            std::string_view("00:00:01.000 --> 00:00:02.500\n  9.0 ㎞/h   0.12 ㎞ \n\n"));
}

TEST(CustomFormatterTests, RejectsFormatNotMatchingValueType) {
  const CustomFormater formatter("{heartrate:3.1f} bpm", "heartrate", false);
  EXPECT_FALSE(formatter.IsValid());
}

TEST(CustomFormatterTests, RejectsUnterminatedAndUnknownFields) {
  EXPECT_FALSE(CustomFormater("{speed", "speed", false).IsValid());
  EXPECT_FALSE(CustomFormater("{grade:2.1f}", "gradient", false).IsValid());
}

TEST(CustomFormatterTests, DoesNotConfuseFieldsSharingPrefix) {
  const CustomFormater formatter("{timestampnext}", "timestampnext", false);
  ASSERT_TRUE(formatter.IsValid());
  const auto& indexes = formatter.GetDataTypeIndexes();
  ASSERT_EQ(indexes.size(), 1);
  EXPECT_EQ(indexes[0], DataType::kTypeTimeStampNext);
  EXPECT_EQ(formatter.GetFormat(DataType::kTypeTimeStampNext), "{}");
}

TEST(ConvertTests, InvalidFormatFailsWithoutCrash) {
  const uint8_t dummy[1] = {0};
  auto result = Convert(std::make_unique<DataSourceMemory>(dummy, sizeof(dummy)), "vtt", 0, 0, "heartrate", false, "{heartrate:.1f}");
  EXPECT_EQ(result->first, ParseResult::kError);
}

TEST(CustomFormatterTests, KeepsLabelBeforeField) {
  const CustomFormater formatter("HR {heartrate:3};{speed:4.1f} km/h", "heartrate,speed", false);
  ASSERT_TRUE(formatter.IsValid());
  ASSERT_EQ(formatter.GetDataTypeIndexes().size(), 2);
  EXPECT_EQ(formatter.GetFormat(DataType::kTypeHeartRate), "HR {:3}");
  EXPECT_EQ(formatter.GetFormat(DataType::kTypeSpeed), "{:4.1f} km/h");
}

TEST(FitDataTests, ExportToVttFmtDoesNotTruncateLongFormats) {
  auto record = MakeRecord();
  record.distance = 12345;
  FitData data(&record, std::numeric_limits<uint32_t>::max());
  data.SetValue(DataType::kTypeTimeStamp, 0);
  data.SetValue(DataType::kTypeTimeStampNext, 1000);

  const CustomFormater formatter("{distance:5.1f} kilometers ridden so far ❤️❤️", "distance", false);
  ASSERT_TRUE(formatter.IsValid());
  OutputBuffer out;
  data.ExportToVttFmt(out, formatter);

  EXPECT_EQ(std::string_view(out.GetString(), out.GetSize()),
            std::string_view("00:00:00.000 --> 00:00:01.000\n  0.1 kilometers ridden so far ❤️❤️\n\n"));
}

TEST(FitDataTests, InterpolationIgnoresValuesMissingInOneRecord) {
  auto record_from = MakeRecord();
  record_from.timestamp = 100;
  record_from.enhanced_altitude = 3000;
  record_from.power = 200;
  auto record_to = MakeRecord();
  record_to.timestamp = 101;
  record_to.heart_rate = 150;
  record_to.power = 300;

  const FitData from(&record_from, std::numeric_limits<uint32_t>::max());
  const FitData to(&record_to, std::numeric_limits<uint32_t>::max());
  const FitData step = from + (to - from) / 2;

  // heart rate is missing in the first record, altitude in the second one
  EXPECT_FALSE(step.GetTypes() & kDataTypeMasks[DataType::kTypeHeartRate]);
  EXPECT_TRUE(step.GetTypes() & kDataTypeMasks[DataType::kTypeAltitude]);
  EXPECT_EQ(step.GetValue(DataType::kTypeAltitude), 3000);
  EXPECT_EQ(step.GetValue(DataType::kTypePower), 250);
  EXPECT_EQ(step.GetValue(DataType::kTypeTimeStamp), 100500);
}

TEST(ProcessorTests, ConvertsUnits) {
  // 4470.4 mm/s = 10 mph = 16.09344 km/h
  EXPECT_NEAR(std::get<double>(kProcessors[kTypeSpeed].first(4470, true)), 9.9991, 1e-4);
  EXPECT_NEAR(std::get<double>(kProcessors[kTypeSpeed].first(10000, false)), 36.0, 1e-9);
  // 2503 raw = 0.6 m, rounded to 1 m
  EXPECT_DOUBLE_EQ(std::get<double>(kProcessors[kTypeAltitude].first(2503, false)), 1.0);
  EXPECT_DOUBLE_EQ(std::get<double>(kProcessors[kTypeAltitude].first(3000, true)), 328.0);
  EXPECT_EQ(std::get<int64_t>(kProcessors[kTypeTemperature].first(-1, true)), 30);
  EXPECT_EQ(std::get<int64_t>(kProcessors[kTypeTemperature].first(25, true)), 77);
}

}  // namespace

int main(int argc, char* argv[]) {
  spdlog::set_pattern("[%H:%M:%S.%e] %^[%l]%$ %v");

  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
