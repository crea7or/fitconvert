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

// Benchmarks
//
// Convert benchmarks run on synthetic rides generated in memory (fit_writer.h), so they are deterministic and need no
// data files: 1 hour (3600 records) and 10 hours (36000 records).
// A real activity can be benchmarked too:
//   fitconvert-bench --fit_file=path/to/activity.fit
// or with the FITCONVERT_BENCH_FILE environment variable, otherwise ./300.fit is used when it exists.
// Micro benchmarks cover the per record hot paths.

#include <benchmark/benchmark.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "fit_writer.h"
#include "parser.cpp"

namespace {

const std::string kBenchFormat(
    "{speed:5.1f} km/h;{distance:6.1f} km;{heartrate:4} bpm;{altitude:5} m;{power:5} w;{temperature:4} C;{cadence:4} rpm;{gradient:3.1f} %");
constexpr std::string_view kBenchDataTypes("speed,distance,heartrate,power,cadence,temperature,gradient,altitude,latitude,longitude");

const std::vector<uint8_t>& SyntheticActivity(const size_t records) {
  static std::map<size_t, std::vector<uint8_t>> cache;
  auto it = cache.find(records);
  if (it == cache.end()) {
    it = cache.emplace(records, fit_test::MakeActivity(records)).first;
  }
  return it->second;
}

std::vector<uint8_t> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    throw std::runtime_error("failed to open file: " + path);
  }
  const std::streamsize size = file.tellg();
  std::vector<uint8_t> buffer(static_cast<size_t>(size));
  file.seekg(0, std::ios::beg);
  if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
    throw std::runtime_error("failed to read file: " + path);
  }
  return buffer;
}

void RunConvert(benchmark::State& state,
                const std::vector<uint8_t>& file,
                const std::string_view type,
                const uint8_t smoothness,
                const std::string& format = {}) {
  size_t output_size = 0;
  for (auto _ : state) {
    auto result = Convert(std::make_unique<DataSourceMemory>(file.data(), file.size()), type, 0, smoothness, kBenchDataTypes, false, format);
    if (result->first != ParseResult::kSuccess) {
      state.SkipWithError("conversion failed");
      return;
    }
    output_size = result->second.GetSize();
    benchmark::DoNotOptimize(result);
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * file.size()));
  state.counters["output_MB"] = static_cast<double>(output_size) / (1024.0 * 1024.0);
}

// args: records, smoothness
void BM_ConvertVtt(benchmark::State& state) {
  const auto& file = SyntheticActivity(static_cast<size_t>(state.range(0)));
  RunConvert(state, file, kOutputVttTag, static_cast<uint8_t>(state.range(1)));
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

void BM_ConvertVttCustomFormat(benchmark::State& state) {
  const auto& file = SyntheticActivity(static_cast<size_t>(state.range(0)));
  RunConvert(state, file, kOutputVttTag, static_cast<uint8_t>(state.range(1)), kBenchFormat);
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

void BM_ConvertJson(benchmark::State& state) {
  const auto& file = SyntheticActivity(static_cast<size_t>(state.range(0)));
  RunConvert(state, file, kOutputJsonTag, static_cast<uint8_t>(state.range(1)));
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

// decoding and record processing only, no output: the cost of the FIT SDK
void BM_DecodeOnly(benchmark::State& state) {
  const auto& file = SyntheticActivity(static_cast<size_t>(state.range(0)));
  RunConvert(state, file, "none", 0);
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_ConvertVtt)->ArgNames({"records", "smooth"})->ArgsProduct({{3600, 36000}, {0, 5}})->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ConvertVttCustomFormat)->ArgNames({"records", "smooth"})->ArgsProduct({{3600, 36000}, {0, 5}})->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ConvertJson)->ArgNames({"records", "smooth"})->ArgsProduct({{3600, 36000}, {0, 5}})->Unit(benchmark::kMillisecond);
BENCHMARK(BM_DecodeOnly)->ArgName("records")->Arg(3600)->Arg(36000)->Unit(benchmark::kMillisecond);

// micro benchmarks

FIT_RECORD_MESG MakeBenchRecord(const uint32_t second) {
  FIT_RECORD_MESG record{};
  record.timestamp = fit_test::kTestStartTimestamp + second;
  record.position_lat = 504000000 + static_cast<int32_t>(second) * 1000;
  record.position_long = 366000000 + static_cast<int32_t>(second) * 700;
  record.distance = second * 700;
  record.heart_rate = 140;
  record.cadence = 88;
  record.power = 210;
  record.altitude = FIT_UINT16_INVALID;
  record.enhanced_altitude = 3500;
  record.speed = FIT_UINT16_INVALID;
  record.enhanced_speed = 7000;
  record.temperature = 21;
  record.grade = 150;
  return record;
}

void BM_FormatTimestamp(benchmark::State& state) {
  std::array<char, 32> buffer;
  int64_t ms = 0;
  for (auto _ : state) {
    const Time time(ms);
    benchmark::DoNotOptimize(format_timestamp(buffer.data(), buffer.size(), time));
    ms = (ms + 1234567) % 356400000;
  }
}
BENCHMARK(BM_FormatTimestamp);

void BM_FormatValueSuffix(benchmark::State& state) {
  std::array<char, 32> buffer;
  double value = 0.0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(format_value_suffix(value, buffer.data(), buffer.size(), 5, " km/h ", 1));
    value += 0.37;
  }
}
BENCHMARK(BM_FormatValueSuffix);

void BM_GradientAddPoint(benchmark::State& state) {
  GradientCalculator gradient;
  uint32_t distance = 0;
  for (auto _ : state) {
    distance += 700;
    benchmark::DoNotOptimize(gradient.AddRawPoint(distance, 3000 + (distance / 1000) % 50));
  }
}
BENCHMARK(BM_GradientAddPoint);

void BM_FitDataApplyData(benchmark::State& state) {
  const FIT_RECORD_MESG record = MakeBenchRecord(10);
  FitData data;
  for (auto _ : state) {
    data.ApplyData(&record, std::numeric_limits<uint32_t>::max());
    benchmark::DoNotOptimize(data);
  }
}
BENCHMARK(BM_FitDataApplyData);

void BM_FitDataInterpolate(benchmark::State& state) {
  const FIT_RECORD_MESG record_from = MakeBenchRecord(10);
  const FIT_RECORD_MESG record_to = MakeBenchRecord(11);
  const FitData from(&record_from, std::numeric_limits<uint32_t>::max());
  const FitData to(&record_to, std::numeric_limits<uint32_t>::max());
  int64_t step = 0;
  for (auto _ : state) {
    const FitData diff = to - from;
    FitData step_data = from + (diff * step) / 6;
    benchmark::DoNotOptimize(step_data);
    step = (step + 1) % 6;
  }
}
BENCHMARK(BM_FitDataInterpolate);

// args: 0 - default vtt, 1 - custom format vtt, 2 - json
void BM_FitDataExport(benchmark::State& state) {
  const FIT_RECORD_MESG record = MakeBenchRecord(10);
  FitData data(&record, std::numeric_limits<uint32_t>::max());
  data.SetValue(DataType::kTypeTimeStamp, 10000);
  data.SetValue(DataType::kTypeTimeStampNext, 11000);
  const int mode = static_cast<int>(state.range(0));
  const CustomFormater formatter(mode == 1 ? kBenchFormat : std::string(), kBenchDataTypes, false);

  OutputBuffer out;
  rapidjson::Writer<rapidjson::StringBuffer> writer(out);
  size_t records = 0;
  for (auto _ : state) {
    if (mode == 0) {
      data.ExportToVtt(out, formatter);
    } else if (mode == 1) {
      data.ExportToVttFmt(out, formatter);
    } else {
      writer.Reset(out);
      data.ExportToJson(writer, formatter);
    }
    // keep the buffer small and warm
    if (++records == 1024) {
      out.Clear();
      records = 0;
    }
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_FitDataExport)->ArgName("mode")->DenseRange(0, 2);

void BM_CustomFormaterParse(benchmark::State& state) {
  for (auto _ : state) {
    const CustomFormater formatter(kBenchFormat, kBenchDataTypes, false);
    benchmark::DoNotOptimize(formatter.GetDataTypesMask());
  }
}
BENCHMARK(BM_CustomFormaterParse);

void RegisterRealFileBenchmarks(const std::string& path) {
  static std::vector<uint8_t> real_file;
  real_file = ReadFile(path);
  const std::string name = std::filesystem::path(path).filename().string();
  const struct {
    const char* label;
    std::string_view type;
    uint8_t smoothness;
    const std::string* format;
  } variants[] = {{"vtt", kOutputVttTag, 0, nullptr},
                  {"vtt_smooth5", kOutputVttTag, 5, nullptr},
                  {"vtt_format", kOutputVttTag, 0, &kBenchFormat},
                  {"json", kOutputJsonTag, 0, nullptr},
                  {"decode_only", "none", 0, nullptr}};
  for (const auto& variant : variants) {
    benchmark::RegisterBenchmark(("BM_RealFile/" + name + "/" + variant.label).c_str(),
                                 [variant](benchmark::State& state) {
                                   RunConvert(state, real_file, variant.type, variant.smoothness, variant.format ? *variant.format : std::string());
                                 })
        ->Unit(benchmark::kMillisecond);
  }
}

// takes --fit_file=<path> out of the arguments before google benchmark parses them
std::string ExtractFitFileArgument(int& argc, char** argv) {
  constexpr std::string_view kFlag("--fit_file=");
  std::string path;
  int out = 1;
  for (int in = 1; in < argc; ++in) {
    const std::string_view arg(argv[in]);
    if (arg.starts_with(kFlag)) {
      path = std::string(arg.substr(kFlag.size()));
    } else {
      argv[out++] = argv[in];
    }
  }
  argc = out;
  return path;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    spdlog::set_pattern("[%H:%M:%S.%e] %^[%l]%$ %v");
    spdlog::set_level(spdlog::level::err);

    std::string fit_file = ExtractFitFileArgument(argc, argv);
    if (fit_file.empty()) {
      if (const char* env = std::getenv("FITCONVERT_BENCH_FILE"); env != nullptr) {
        fit_file = env;
      } else if (std::filesystem::exists("300.fit")) {
        fit_file = "300.fit";
      }
    }
    if (!fit_file.empty()) {
      RegisterRealFileBenchmarks(fit_file);
    }

    ::benchmark::Initialize(&argc, argv);
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) {
      return 1;
    }
    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();
  } catch (const std::exception& e) {
    SPDLOG_ERROR("exception during processing: {}", e.what());
    return 1;
  }
  return 0;
}
