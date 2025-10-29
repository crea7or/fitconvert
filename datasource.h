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
#include <memory>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::string_view kStdinTag("stdin");
inline constexpr std::string_view kStdoutTag("stdout");

struct Buffer {
 public:
  Buffer(const size_t buffer_size) { buffer_.resize(buffer_size); }

  void SetDataSize(const size_t size) { data_size_ = size; }

  size_t GetDataSize() const { return data_size_; }

  size_t GetBufferSize() const { return buffer_.size(); }

  char* GetDataPtr() { return buffer_.data(); }

 private:
  std::vector<char> buffer_;
  size_t data_size_{0};
};

class DataSource {
 public:
  enum class Type {
    kFile,
    kMemory,
    kStdin,
    kStdout,
  };

  enum class Status {
    kContinueRead,
    kEndOfFile,
    kError,
  };

  DataSource(Type type) : type_(type) {}

  virtual ~DataSource() = default;

  virtual Status ReadData(Buffer& buffer) = 0;

  Type GetType() const noexcept { return type_; }

  virtual size_t GetSize() const = 0;

 protected:
  Status ReadDataInternal(std::istream& stream, Buffer& buffer);

 private:
  Type type_{Type::kFile};
};

class DataSourceFile final : public DataSource {
 public:
  DataSourceFile(const std::string source_name);
  virtual ~DataSourceFile() = default;

  Status ReadData(Buffer& buffer) override;

  size_t GetSize() const override;

 private:
  std::string source_name_;
  std::unique_ptr<std::istream> stream_;
};

class DataSourceStdin final : public DataSource {
 public:
  DataSourceStdin();

  Status ReadData(Buffer& buffer) override;

  size_t GetSize() const override;
};

class DataSourceMemory final : public DataSource {
 public:
  DataSourceMemory(const uint8_t* buffer_ptr, const size_t count);
  virtual ~DataSourceMemory() = default;

  Status ReadData(Buffer& buffer) override;

  size_t GetSize() const override;

 private:
  const uint8_t* buffer_ptr_{nullptr};
  const size_t count_{0};
  size_t position_{0};
};
