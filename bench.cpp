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

#include <benchmark/benchmark.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "datasource.h"
#include "parser.h"


std::vector<uint8_t> readFileToBuffer(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    throw std::runtime_error("Failed to open file: " + path);

  std::streamsize size = file.tellg();
  if (size < 0)
    throw std::runtime_error("Failed to determine file size: " + path);

  std::vector<uint8_t> buffer(static_cast<size_t>(size));
  file.seekg(0, std::ios::beg);

  if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
    throw std::runtime_error("Failed to read file: " + path);

  return buffer;
}

std::vector<uint8_t> fit_file;

static void BM_VttExpor(benchmark::State& state) {
  for (auto _ : state) {
    auto data_source = std::make_unique<DataSourceMemory>(fit_file.data(), fit_file.size());
    const std::unique_ptr<FitResult> result{Convert(std::move(data_source), "vtt", 0, 0, 0xFFFFFF, false)};
    benchmark::DoNotOptimize(result);
  }
}

static void BM_JsonExport(benchmark::State& state) {
  for (auto _ : state) {
    auto data_source = std::make_unique<DataSourceMemory>(fit_file.data(), fit_file.size());
    const std::unique_ptr<FitResult> result{Convert(std::move(data_source), "json", 0, 0, 0xFFFFFF, false)};
    benchmark::DoNotOptimize(result);
  }
}

static void BM_FitOnlyExport(benchmark::State& state) {
  for (auto _ : state) {
    auto data_source = std::make_unique<DataSourceMemory>(fit_file.data(), fit_file.size());
    const std::unique_ptr<FitResult> result{Convert(std::move(data_source), "none", 0, 0, 0xFFFFFF, false)};
    benchmark::DoNotOptimize(result);
  }
}

BENCHMARK(BM_JsonExport);
BENCHMARK(BM_VttExpor);
BENCHMARK(BM_FitOnlyExport);

// Run the benchmark
int main(int argc, char** argv) {
  try {
    spdlog::set_pattern("[%H:%M:%S.%e] %^[%l]%$ %v");
    spdlog::set_level(spdlog::level::err);

    fit_file = readFileToBuffer("C:\\Sources\\fit2srt\\build\\Release\\300.fit");

    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
  } catch (const std::exception& e) {
    SPDLOG_ERROR("exception during processing: {}", e.what());
  }
}
