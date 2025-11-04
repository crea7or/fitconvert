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

#include "datasource.h"

#include <fcntl.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

DataSource::Status DataSource::ReadDataInternal(std::istream& stream, Buffer& buffer) {
  try {
    stream.read(buffer.GetDataPtr(), buffer.GetBufferSize());
    buffer.SetDataSize(stream.gcount());
    if (stream.eof() || stream.gcount() == 0u) {
      return Status::kEndOfFile;
    } else if (stream.good()) {
      return Status::kContinueRead;
    }
  } catch (const std::exception& e) {
    SPDLOG_ERROR("input file reading error: {}", e.what());  //
  }
  return Status::kError;
}

DataSourceFile::DataSourceFile(const std::string source_name)
    : DataSource(DataSource::Type::kFile), source_name_(source_name) {
  stream_ = std::make_unique<std::ifstream>(source_name_, std::ios::in | std::ios::app | std::ios::binary);
  stream_->exceptions(std::ios_base::badbit);
}

DataSource::Status DataSourceFile::ReadData(Buffer& buffer) {
  return ReadDataInternal(*stream_.get(), buffer);
}

size_t DataSourceFile::GetSize() const {
  return std::filesystem::file_size(source_name_);
}

DataSourceStdin::DataSourceStdin() : DataSource(DataSource::Type::kStdin) {}

DataSource::Status DataSourceStdin::ReadData(Buffer& buffer) {
  return ReadDataInternal(std::cin, buffer);
}

size_t DataSourceStdin::GetSize() const {
  return 0u;
}

DataSourceMemory::DataSourceMemory(const uint8_t* buffer_ptr, const size_t count)
    : DataSource(DataSource::Type::kMemory), buffer_ptr_{buffer_ptr}, count_{count} {}

DataSource::Status DataSourceMemory::ReadData(Buffer& buffer) {
  const size_t remaining_data{count_ - position_};
  const uint8_t* next_data_ptr{buffer_ptr_ + position_};
  if (0u == remaining_data) {
    buffer.SetDataSize(0u);
    return Status::kError;
  }

  Status result{Status::kContinueRead};
  size_t data_to_copy{buffer.GetBufferSize()};
  if (buffer.GetBufferSize() >= remaining_data) {
    result = Status::kEndOfFile;
    data_to_copy = remaining_data;
  }

  std::memcpy(buffer.GetDataPtr(), next_data_ptr, data_to_copy);
  buffer.SetDataSize(data_to_copy);
  position_ += data_to_copy;
  return result;
}

size_t DataSourceMemory::GetSize() const {
  return count_;
}
