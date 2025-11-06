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

#include "gtest/gtest.h"
#include "parser.cpp"


namespace {

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
}

TEST(ValuesFormattingWithSuffix, Positive) {
  std::array<char, 32> buffer;
  FormatData format = kMetricFormat;
  {
    const size_t size = format_value_suffix(
        0.123, buffer.data(), buffer.size(), format[DataType::kTypeDistance].second, format[DataType::kTypeDistance].first, 2);
    EXPECT_EQ(std::string_view(buffer.data(), size), "   0.12 km");
  }
  {
    const size_t size = format_value_suffix(
        1.234, buffer.data(), buffer.size(), format[DataType::kTypeSpeed].second, format[DataType::kTypeSpeed].first, 1);
    EXPECT_EQ(std::string_view(buffer.data(), size), "    1.2 km/h");
  }
  {
    const size_t size = format_value_suffix(
        12345, buffer.data(), buffer.size(), format[DataType::kTypeAltitude].second, format[DataType::kTypeAltitude].first, 0);
    EXPECT_EQ(std::string_view(buffer.data(), size), " 12345 m");
  }
  {
    const size_t size = format_value_suffix(
        1234567, buffer.data(), buffer.size(), format[DataType::kTypeAltitude].second, format[DataType::kTypeAltitude].first, 0);
    EXPECT_EQ(std::string_view(buffer.data(), size), "1234567 m");
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  spdlog::set_pattern("[%H:%M:%S.%e] %^[%l]%$ %v");

  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}