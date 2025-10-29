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

#include <spdlog/spdlog.h>

#include "gtest/gtest.h"
#include "parser.cpp"


namespace {

TEST(ValuesFormatting, Positive) {
  {
    const bool imperial = false;
    const auto distance = NumberToStringPrecision(123456, imperial ? 160934.4 : 100000.0, 5, 2);
    EXPECT_EQ(distance, "1.23");
  }
  {
    const bool imperial = false;
    const auto distance = NumberToStringPrecision(1234567, imperial ? 160934.4 : 100000.0, 5, 2);
    EXPECT_EQ(distance, "12.34");
  }
  {
    const bool imperial = false;
    const auto distance = NumberToStringPrecision(12345678, imperial ? 160934.4 : 100000.0, 5, 2);
    EXPECT_EQ(distance, "123.4");
  }
  {
    const bool imperial = false;
    const auto distance = NumberToStringPrecision(123456789, imperial ? 160934.4 : 100000.0, 5, 2);
    EXPECT_EQ(distance, "1234");
  }
  {
    const bool imperial = false;
    const auto distance = NumberToStringPrecision(1234567890, imperial ? 160934.4 : 100000.0, 5, 2);
    EXPECT_EQ(distance, "12345");
  }
  {
    const bool imperial = false;
    const auto distance = NumberToStringPrecision(12345678901, imperial ? 160934.4 : 100000.0, 5, 2);
    EXPECT_EQ(distance, "12345");
  }

  {
    const bool imperial = true;
    const auto distance = NumberToStringPrecision(123456, imperial ? 160934.4 : 100000.0, 5, 2);
    EXPECT_EQ(distance, "0.76");
  }
  {
    const bool imperial = true;
    const auto distance = NumberToStringPrecision(1234567, imperial ? 160934.4 : 100000.0, 5, 2);
    EXPECT_EQ(distance, "7.67");
  }
  {
    const bool imperial = true;
    const auto distance = NumberToStringPrecision(12345678, imperial ? 160934.4 : 100000.0, 5, 2);
    EXPECT_EQ(distance, "76.71");
  }
  {
    const bool imperial = true;
    const auto distance = NumberToStringPrecision(123456789, imperial ? 160934.4 : 100000.0, 5, 2);
    EXPECT_EQ(distance, "767.1");
  }
  {
    const bool imperial = true;
    const auto distance = NumberToStringPrecision(1234567890, imperial ? 160934.4 : 100000.0, 5, 2);
    EXPECT_EQ(distance, "7671");
  }
  {
    const bool imperial = true;
    const auto distance = NumberToStringPrecision(12345678901, imperial ? 160934.4 : 100000.0, 5, 2);
    EXPECT_EQ(distance, "76712");
  }

  {
    const bool imperial = false;
    const auto speed = NumberToStringPrecision(123, imperial ? 447.2136 : 277.77, 4, 1);
    EXPECT_EQ(speed, "0.4");
  }
  {
    const bool imperial = false;
    const auto speed = NumberToStringPrecision(1234, imperial ? 447.2136 : 277.77, 4, 1);
    EXPECT_EQ(speed, "4.4");
  }
  {
    const bool imperial = false;
    const auto speed = NumberToStringPrecision(12345, imperial ? 447.2136 : 277.77, 4, 1);
    EXPECT_EQ(speed, "44.4");
  }
  {
    const bool imperial = false;
    const auto speed = NumberToStringPrecision(123456, imperial ? 447.2136 : 277.77, 4, 1);
    EXPECT_EQ(speed, "444");
  }

  {
    const bool imperial = true;
    const auto speed = NumberToStringPrecision(123, imperial ? 447.2136 : 277.77, 4, 1);
    EXPECT_EQ(speed, "0.2");
  }
  {
    const bool imperial = true;
    const auto speed = NumberToStringPrecision(1234, imperial ? 447.2136 : 277.77, 4, 1);
    EXPECT_EQ(speed, "2.7");
  }
  {
    const bool imperial = true;
    const auto speed = NumberToStringPrecision(12345, imperial ? 447.2136 : 277.77, 4, 1);
    EXPECT_EQ(speed, "27.6");
  }
  {
    const bool imperial = true;
    const auto speed = NumberToStringPrecision(123456, imperial ? 447.2136 : 277.77, 4, 1);
    EXPECT_EQ(speed, "276");
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  spdlog::set_pattern("[%H:%M:%S.%e] %^[%l]%$ %v");

  ::testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}