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

#include <fcntl.h>
#include <io.h>
#include <spdlog/spdlog.h>

#include <cxxopts.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "datasource.h"
#include "parser.h"

constexpr int kToolError{-1};

constexpr const char kBanner[] = R"%(

      .:+oooooooooooooooooooooooooooooooooooooo: `/ooooooooooo/` :ooooo+/-`
   `+dCEZEOCEZEOCEZEOCEZEOCEZEOCEZEOCEZEOCEZEOEZshCEZEOCEZEOEZ#doCEZEOEZEZNs.
  :CEZEON#ddddddddddddddddddddddddddddddNCEZEO#h.:hdddddddddddh/.yddddCEZEO#N+
 :CEZEO+.        .-----------.`       `+CEZEOd/   .-----------.        `:CEZEO/
 CEZEO/         :CEZEOCEZEOEZNd.    `/dCEZEO+`   sNCEZEOCEZEO#Ny         -CEZEO
 CEZEO/         :#NCEZEOCEZEONd.   :hCEZEOo`     oNCEZEOCEZEO#Ny         -CEZEO
 :CEZEOo.`       `-----------.`  -yNEZ#Ns.       `.-----------.`       `/CEZEO/
  :CEZEONCEZEOd/.ydCEZEOCEZEOdo.sNCEZEOCEZEOCEZEOCEZEOCEZEOCEZEOCEZEOEZNEZEZN+
   `+dCEZEOEZEZdoCEZEOCEZEOEZ#N+CEZEOCEZEOCEZEOCEZEOCEZEOCEZEOCEZEOCEZEOEZ#s.
      .:+ooooo/` :+oooooooooo+. .+ooooooooooooooooooooooooooooooooooooo+/.
 C E Z E O  S O F T W A R E (c) 2025   FIT telemetry converter to VTT or JSON

)%";

constexpr const char kHelp[] = R"%(

usage: fitconvert -i input_file -o output_file -t output_type -f offset -s N

-i - path to .fit file to read data from
-o - path to .vtt or .json file to write to
-t - export type: vtt or json
-f - offset in milliseconds to sync video and .fit data (optional)
* if the offset is positive - 'offset' second of the data from .fit file will be displayed at the first second of the video.
    it is for situations when you started video after starting recording your activity(that generated .fit file)
* if the offset is negative - the first second of .fit data will be displayed at abs('offset') second of the video
    it is for situations when you started your activity (that generated .fit file) after starting the video
-s - smooth values by inserting N (0-5) smoothed values between timestamps (optional)
-v - values format: iso or imperial (optional)
-d - data to process, enumerate delimited by comma (default all): speed,distance,heartrate,altitude,power,cadence,temperature
)%";

int main(int argc, char* argv[]) {
  spdlog::set_pattern("[%H:%M:%S.%e] %^[%l]%$ %v");
  try {
    cxxopts::Options cmd_options("FIT converter", "FIT telemetry converter to .VTT or .JSON");
    cmd_options.add_options()                                                               //
        ("i,input", "", cxxopts::value<std::string>())                                      //
        ("o,output", "", cxxopts::value<std::string>())                                     //
        ("h,help", "")                                                                      //
        ("d,data", "", cxxopts::value<std::string>()->default_value(""))                    //
        ("t,type", "", cxxopts::value<std::string>()->default_value(kOutputVttTag.data()))  //
        ("f,offset", "", cxxopts::value<int64_t>()->default_value("0"))                     //
        ("v,values", "", cxxopts::value<std::string>()->default_value("iso"))               //
        ("s,smooth", "", cxxopts::value<uint8_t>()->default_value("0"));                    //
    const auto cmd_result = cmd_options.parse(argc, argv);

    if (argc < 2 || cmd_result.count("help") > 0 || cmd_result.count("input") == 0 || cmd_result.count("output") == 0) {
      std::cout << kBanner << std::endl;
      std::cout << kHelp << std::endl;
    }

    const std::string input_fit_file(cmd_result["input"].as<std::string>());
    const std::string output_file(cmd_result["output"].as<std::string>());
    const std::string output_type(cmd_result["type"].as<std::string>());
    const int64_t offset(cmd_result["offset"].as<int64_t>());
    const uint8_t smoothness(cmd_result["smooth"].as<uint8_t>());
    const std::string datatypes(cmd_result["data"].as<std::string>());
    const std::string values(cmd_result["values"].as<std::string>());

    if (output_file == kStdoutTag) {
      // disable informative output for cou output
      spdlog::set_level(spdlog::level::err);
#ifdef _WIN32
      (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
    } else {
      spdlog::set_level(spdlog::level::info);
    }
#ifdef _WIN32
    if (input_fit_file == kStdinTag) {
      (void)_setmode(_fileno(stdin), _O_BINARY);
    }
#endif

    if (values != kValuesISO && values != kValuesImperial) {
      SPDLOG_ERROR("unknown values format specified: '{}, only 'iso' or 'imperial' is supported", values);
      return kToolError;
    }

    uint32_t datatypes_mask = DataTypeNamesToMask(datatypes);
    if (0 == datatypes_mask) {
      datatypes_mask = std::numeric_limits<uint32_t>::max();
    }

    if (output_type != kOutputJsonTag && output_type != kOutputVttTag) {
      SPDLOG_ERROR("unknown output specified: '{}', only vtt and .json supported", output_type);
      return kToolError;
    }

    if (smoothness > 5) {
      SPDLOG_ERROR("smoothness can not be more than 5");
      return kToolError;
    }

    std::unique_ptr<DataSource> data_source;
    size_t data_source_size{0};
    if (kStdinTag == input_fit_file) {
      data_source = std::make_unique<DataSourceStdin>();
    } else {
      data_source = std::make_unique<DataSourceFile>(input_fit_file);
    }

    const std::string result{
        convert(std::move(data_source), output_type, offset, smoothness, datatypes_mask, values == kValuesImperial)};
    if (kStdoutTag == output_file) {
      std::cout << result;
    } else {
      std::filesystem::remove(output_file);
      std::ofstream output_stream(output_file, std::ios::out | std::ios::app | std::ios::binary);
      output_stream.exceptions(std::ios_base::badbit);
      output_stream.write(result.data(), result.size());
    }
  } catch (const std::ios_base::failure& fail) {
    SPDLOG_ERROR("file problem during processing: {}", fail.what());
    return kToolError;
  } catch (const std::exception& e) {
    SPDLOG_ERROR("exception during processing: {}", e.what());
    return kToolError;
  }
  return 0;
}
