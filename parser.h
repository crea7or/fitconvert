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

#include <cstdint>
#include <string_view>

inline constexpr std::string_view kOutputJsonTag = "json";
inline constexpr std::string_view kOutputVttTag = "vtt";
inline constexpr std::string_view kOutputPreviewTag = "preview";
inline constexpr std::string_view kValuesISO = "iso";
inline constexpr std::string_view kValuesImperial = "imperial";

// convert datatypes divided by comma to datatypes mask, 0 - means error
uint32_t DataTypeNamesToMask(std::string_view name);

std::string convert(std::unique_ptr<DataSource> data_source_ptr,
                    const std::string_view output_type,
                    const int64_t offset,
                    const uint8_t smoothness,
                    const uint32_t datatypes,
                    const bool imperial);
