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

#pragma once

// defines for rapidjson
#define RAPIDJSON_HAS_CXX11_RVALUE_REFS 1
#define RAPIDJSON_HAS_CXX11_NOEXCEPT 1
#define RAPIDJSON_HAS_CXX11_TYPETRAITS 1
#define RAPIDJSON_HAS_CXX11_STATIC_ASSERT 1
#define RAPIDJSON_HAS_STDSTRING 1

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

#include <cstdint>
#include <memory>
#include <string_view>

#include "datasource.h"

enum class ParseResult {
  kSuccess,
  kError,
};

using FitResult = std::pair<ParseResult, rapidjson::StringBuffer>;

inline constexpr std::string_view kOutputJsonTag = "json";
inline constexpr std::string_view kOutputVttTag = "vtt";
inline constexpr std::string_view kValuesMetric = "metric";
inline constexpr std::string_view kValuesImperial = "imperial";

// convert datatypes divided by comma to datatypes mask, 0 - means error
uint32_t DataTypeNamesToMask(std::string_view name);

std::unique_ptr<FitResult> Convert(std::unique_ptr<DataSource> data_source_ptr,
                                   const std::string_view output_type,
                                   const int64_t offset,
                                   const uint8_t smoothness,
                                   const uint32_t datatypes,
                                   const bool imperial);
