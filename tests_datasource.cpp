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

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "datasource.h"
#include "gtest/gtest.h"

namespace {

std::string ReadAll(DataSource& source, Buffer& buffer, std::vector<DataSource::Status>* statuses = nullptr) {
  std::string data;
  for (int guard = 0; guard < 1000; ++guard) {
    const auto status = source.ReadData(buffer);
    if (statuses != nullptr) {
      statuses->push_back(status);
    }
    data.append(buffer.GetDataPtr(), buffer.GetDataSize());
    if (status != DataSource::Status::kContinueRead) {
      break;
    }
  }
  return data;
}

// replaces std::cin's buffer for the lifetime of the object
class CinRedirect final {
 public:
  explicit CinRedirect(std::streambuf* buffer) : previous_(std::cin.rdbuf(buffer)), previous_exceptions_(std::cin.exceptions()) {}
  ~CinRedirect() {
    std::cin.rdbuf(previous_);
    std::cin.clear();
    std::cin.exceptions(previous_exceptions_);
  }

 private:
  std::streambuf* previous_;
  std::ios_base::iostate previous_exceptions_;
};

class ThrowingStreamBuffer final : public std::streambuf {
 protected:
  int_type underflow() override { throw std::runtime_error("device is gone"); }
};

class TempFile final {
 public:
  explicit TempFile(const std::string& content)
      : path_(std::filesystem::temp_directory_path() / ("fitconvert_datasource_test_" + std::to_string(content.size()) + ".bin")) {
    std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
  }
  ~TempFile() { std::filesystem::remove(path_); }
  std::string Path() const { return path_.string(); }

 private:
  std::filesystem::path path_;
};

TEST(BufferTests, KeepsSizes) {
  Buffer buffer(16);
  EXPECT_EQ(buffer.GetBufferSize(), 16u);
  EXPECT_EQ(buffer.GetDataSize(), 0u);
  buffer.SetDataSize(5);
  EXPECT_EQ(buffer.GetDataSize(), 5u);
  EXPECT_NE(buffer.GetDataPtr(), nullptr);
}

TEST(DataSourceMemoryTests, ReadsInChunks) {
  const std::string content = "0123456789";
  DataSourceMemory source(reinterpret_cast<const uint8_t*>(content.data()), content.size());
  EXPECT_EQ(source.GetType(), DataSource::Type::kMemory);
  EXPECT_EQ(source.GetSize(), content.size());

  Buffer buffer(4);
  std::vector<DataSource::Status> statuses;
  EXPECT_EQ(ReadAll(source, buffer, &statuses), content);
  EXPECT_EQ(statuses,
            (std::vector<DataSource::Status>{DataSource::Status::kContinueRead, DataSource::Status::kContinueRead, DataSource::Status::kEndOfFile}));
  // reading after the end is an error and returns no data
  EXPECT_EQ(source.ReadData(buffer), DataSource::Status::kError);
  EXPECT_EQ(buffer.GetDataSize(), 0u);
}

TEST(DataSourceMemoryTests, ExactBufferMultipleEndsWithEndOfFile) {
  const std::string content = "01234567";
  DataSourceMemory source(reinterpret_cast<const uint8_t*>(content.data()), content.size());
  Buffer buffer(4);
  std::vector<DataSource::Status> statuses;
  EXPECT_EQ(ReadAll(source, buffer, &statuses), content);
  EXPECT_EQ(statuses, (std::vector<DataSource::Status>{DataSource::Status::kContinueRead, DataSource::Status::kEndOfFile}));
}

TEST(DataSourceMemoryTests, EmptyIsError) {
  DataSourceMemory source(nullptr, 0);
  Buffer buffer(4);
  EXPECT_EQ(source.ReadData(buffer), DataSource::Status::kError);
  EXPECT_EQ(source.GetSize(), 0u);
}

TEST(DataSourceFileTests, ReadsWholeFile) {
  std::string content(10000, '\0');
  for (size_t i = 0; i < content.size(); ++i) {
    content[i] = static_cast<char>(i * 7);
  }
  const TempFile file(content);
  DataSourceFile source(file.Path());
  EXPECT_EQ(source.GetType(), DataSource::Type::kFile);
  EXPECT_EQ(source.GetSize(), content.size());

  Buffer buffer(4096);
  std::vector<DataSource::Status> statuses;
  EXPECT_EQ(ReadAll(source, buffer, &statuses), content);
  EXPECT_EQ(statuses.back(), DataSource::Status::kEndOfFile);
  EXPECT_EQ(statuses.size(), 3u);
}

TEST(DataSourceFileTests, MissingFileThrowsAndIsNotCreated) {
  const auto path = std::filesystem::temp_directory_path() / "fitconvert_datasource_missing.fit";
  std::filesystem::remove(path);
  EXPECT_THROW(DataSourceFile source(path.string()), std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(DataSourceFileTests, ReadOnlyFileCanBeRead) {
  const TempFile file("read only content");
  std::filesystem::permissions(file.Path(), std::filesystem::perms::owner_write, std::filesystem::perm_options::remove);
  {
    DataSourceFile source(file.Path());
    Buffer buffer(64);
    EXPECT_EQ(ReadAll(source, buffer), "read only content");
  }
  std::filesystem::permissions(file.Path(), std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
}

TEST(DataSourceStdinTests, ReadsFromStdin) {
  std::istringstream input("stdin data");
  const CinRedirect redirect(input.rdbuf());
  DataSourceStdin source;
  EXPECT_EQ(source.GetType(), DataSource::Type::kStdin);
  EXPECT_EQ(source.GetSize(), 0u);

  Buffer buffer(4);
  EXPECT_EQ(ReadAll(source, buffer), "stdin data");
}

TEST(DataSourceStdinTests, EmptyStdinIsEndOfFile) {
  std::istringstream input("");
  const CinRedirect redirect(input.rdbuf());
  DataSourceStdin source;
  Buffer buffer(4);
  EXPECT_EQ(source.ReadData(buffer), DataSource::Status::kEndOfFile);
  EXPECT_EQ(buffer.GetDataSize(), 0u);
}

TEST(DataSourceStdinTests, ReadErrorIsReported) {
  ThrowingStreamBuffer throwing;
  const CinRedirect redirect(&throwing);
  std::cin.exceptions(std::ios_base::badbit);
  DataSourceStdin source;
  Buffer buffer(4);
  EXPECT_EQ(source.ReadData(buffer), DataSource::Status::kError);
}

}  // namespace
