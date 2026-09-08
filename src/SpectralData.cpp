#include "SpectralData.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace vlb {
namespace {

constexpr std::size_t kJiangCameraCount = 28;
constexpr std::size_t kJiangSamplesPerChannel = 33;

std::vector<std::string> splitCsv(const std::string& line) {
  std::vector<std::string> fields;
  std::stringstream ss(line);
  std::string field;
  while (std::getline(ss, field, ',')) {
    fields.push_back(field);
  }
  return fields;
}

struct NumericTable {
  std::vector<std::string> headers;
  std::vector<std::vector<double>> columns;
};

NumericTable loadNumericCsv(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Unable to open data file: " + path.string());
  }

  std::string line;
  if (!std::getline(input, line)) {
    throw std::runtime_error("Empty data file: " + path.string());
  }

  NumericTable table;
  table.headers = splitCsv(line);
  table.columns.resize(table.headers.size());

  while (std::getline(input, line)) {
    if (line.empty()) continue;
    const auto fields = splitCsv(line);
    if (fields.size() != table.headers.size()) {
      throw std::runtime_error("Malformed CSV row in: " + path.string());
    }
    for (std::size_t i = 0; i < fields.size(); ++i) {
      table.columns[i].push_back(std::stod(fields[i]));
    }
  }
  return table;
}

std::vector<double> makeWavelengthGrid(double start_nm, double end_nm,
                                       double step_nm) {
  if (step_nm <= 0.0) {
    throw std::runtime_error("Spectral sampling interval must be positive.");
  }

  std::vector<double> grid;
  for (double w = start_nm; w <= end_nm + step_nm * 0.25; w += step_nm) {
    grid.push_back(w);
  }
  return grid;
}

std::vector<double> resampleLinear(const std::vector<double>& source_wavelengths,
                                   const std::vector<double>& source_values,
                                   const std::vector<double>& target_wavelengths,
                                   bool zero_outside) {
  if (source_wavelengths.size() != source_values.size() ||
      source_wavelengths.empty()) {
    throw std::runtime_error("Invalid spectrum passed to resampleLinear().");
  }

  std::vector<double> result;
  result.reserve(target_wavelengths.size());

  for (double w : target_wavelengths) {
    if (w < source_wavelengths.front()) {
      result.push_back(zero_outside ? 0.0 : source_values.front());
      continue;
    }
    if (w > source_wavelengths.back()) {
      result.push_back(zero_outside ? 0.0 : source_values.back());
      continue;
    }

    const auto hi_it = std::lower_bound(source_wavelengths.begin(),
                                        source_wavelengths.end(), w);
    if (hi_it == source_wavelengths.begin()) {
      result.push_back(source_values.front());
      continue;
    }
    if (hi_it == source_wavelengths.end()) {
      result.push_back(source_values.back());
      continue;
    }

    const std::size_t hi =
        static_cast<std::size_t>(hi_it - source_wavelengths.begin());
    if (*hi_it == w) {
      result.push_back(source_values[hi]);
      continue;
    }

    const std::size_t lo = hi - 1;
    const double t = (w - source_wavelengths[lo]) /
                     (source_wavelengths[hi] - source_wavelengths[lo]);
    result.push_back(source_values[lo] * (1.0 - t) + source_values[hi] * t);
  }
  return result;
}

std::vector<Patch> loadColorChecker(
    const std::filesystem::path& path,
    const std::vector<double>& target_wavelengths) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Unable to open ColorChecker file: " +
                             path.string());
  }

  std::string line;
  if (!std::getline(input, line)) {
    throw std::runtime_error("Empty ColorChecker file: " + path.string());
  }

  const auto header = splitCsv(line);
  if (header.size() < 4 || header[0] != "patch_id" ||
      header[1] != "patch_name") {
    throw std::runtime_error("Unexpected ColorChecker CSV header.");
  }

  std::vector<double> source_wavelengths;
  for (std::size_t i = 2; i < header.size(); ++i) {
    source_wavelengths.push_back(std::stod(header[i]));
  }

  std::vector<Patch> patches;
  while (std::getline(input, line)) {
    if (line.empty()) continue;

    const auto fields = splitCsv(line);
    if (fields.size() != header.size()) {
      throw std::runtime_error("Malformed ColorChecker row.");
    }

    std::vector<double> source_values;
    source_values.reserve(source_wavelengths.size());
    for (std::size_t i = 2; i < fields.size(); ++i) {
      source_values.push_back(std::stod(fields[i]));
    }

    Patch patch;
    patch.id = fields[0];
    patch.name = fields[1];
    patch.reflectance = resampleLinear(source_wavelengths, source_values,
                                       target_wavelengths, false);
    patches.push_back(std::move(patch));
  }

  if (patches.size() != 24) {
    throw std::runtime_error("Expected 24 ColorChecker patches.");
  }
  return patches;
}

std::vector<double> parseWhitespaceDoubles(const std::string& line) {
  std::istringstream input(line);
  std::vector<double> values;
  double value = 0.0;
  while (input >> value) {
    values.push_back(value);
  }
  return values;
}

bool readNextNonEmptyLine(std::istream& input, std::string& line) {
  while (std::getline(input, line)) {
    if (!line.empty() && line.find_first_not_of(" \t\r\n") !=
                             std::string::npos) {
      return true;
    }
  }
  return false;
}

std::vector<CameraSensitivity> loadJiangCameraDatabase(
    const std::filesystem::path& path,
    const std::vector<double>& target_wavelengths) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Unable to open Jiang camera database: " +
                             path.string());
  }

  const std::vector<double> source_wavelengths =
      makeWavelengthGrid(400.0, 720.0, 10.0);

  std::vector<CameraSensitivity> cameras;
  std::string camera_name;

  while (readNextNonEmptyLine(input, camera_name)) {
    CameraSensitivity camera;
    camera.name = camera_name;

    for (int channel = 0; channel < 3; ++channel) {
      std::string line;
      if (!readNextNonEmptyLine(input, line)) {
        throw std::runtime_error("Incomplete Jiang camera entry: " +
                                 camera.name);
      }

      const std::vector<double> source_values = parseWhitespaceDoubles(line);
      if (source_values.size() != kJiangSamplesPerChannel) {
        throw std::runtime_error(
            "Expected 33 sensitivity samples for camera '" + camera.name +
            "', channel " + std::to_string(channel) + ".");
      }

      camera.rgb[channel] =
          resampleLinear(source_wavelengths, source_values,
                         target_wavelengths, true);
    }

    cameras.push_back(std::move(camera));
  }

  if (cameras.size() != kJiangCameraCount) {
    throw std::runtime_error("Expected 28 cameras in Jiang database, found " +
                             std::to_string(cameras.size()) + ".");
  }

  return cameras;
}

std::size_t findColumn(const NumericTable& table, const std::string& name) {
  const auto it = std::find(table.headers.begin(), table.headers.end(), name);
  if (it == table.headers.end()) {
    throw std::runtime_error("Missing CSV column: " + name);
  }
  return static_cast<std::size_t>(it - table.headers.begin());
}

bool isSpectralDataDirectory(const std::filesystem::path& path) {
  return std::filesystem::is_directory(path) &&
         std::filesystem::is_regular_file(path / "cie_1931_2deg_5nm.csv") &&
         std::filesystem::is_regular_file(path / "cie_illuminants_5nm.csv") &&
         std::filesystem::is_regular_file(
             path / "colorchecker_babelcolor_avg30_10nm.csv") &&
         std::filesystem::is_regular_file(path / "camspec_database.txt");
}

}  // namespace

SpectralData loadSpectralData(const std::filesystem::path& data_dir,
                              double sampling_nm) {
  SpectralData data;
  data.wavelengths_nm = makeWavelengthGrid(380.0, 730.0, sampling_nm);

  const auto cmf = loadNumericCsv(data_dir / "cie_1931_2deg_5nm.csv");
  const std::size_t cmf_w = findColumn(cmf, "wavelength_nm");
  const auto& cmf_source_wavelengths = cmf.columns[cmf_w];
  for (int i = 0; i < 3; ++i) {
    const std::string name =
        (i == 0) ? "x_bar" : (i == 1) ? "y_bar" : "z_bar";
    const std::size_t col = findColumn(cmf, name);
    data.xyz_cmf[i] = resampleLinear(cmf_source_wavelengths, cmf.columns[col],
                                     data.wavelengths_nm, true);
  }

  const auto illum = loadNumericCsv(data_dir / "cie_illuminants_5nm.csv");
  const std::size_t illum_w = findColumn(illum, "wavelength_nm");
  const auto& illum_source_wavelengths = illum.columns[illum_w];
  for (const char* name : {"A", "D50", "D55", "D65", "D75", "C"}) {
    const std::size_t col = findColumn(illum, name);
    data.illuminants.push_back(Illuminant{
        name, resampleLinear(illum_source_wavelengths, illum.columns[col],
                             data.wavelengths_nm, true)});
  }

  data.patches = loadColorChecker(
      data_dir / "colorchecker_babelcolor_avg30_10nm.csv",
      data.wavelengths_nm);

  data.cameras = loadJiangCameraDatabase(data_dir / "camspec_database.txt",
                                         data.wavelengths_nm);

  return data;
}

std::filesystem::path locateDataDirectory(int argc, char** argv) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--data-dir") {
      const std::filesystem::path path(argv[i + 1]);
      if (!isSpectralDataDirectory(path)) {
        throw std::runtime_error(
            "--data-dir does not contain the required spectral data files: " +
            path.string());
      }
      return path;
    }
  }

  if (const char* env = std::getenv("VLB_DATA_DIR")) {
    const std::filesystem::path path(env);
    if (isSpectralDataDirectory(path)) return path;
  }

#ifdef VLB_BUILD_DATA_DIR
  const std::filesystem::path build_data(VLB_BUILD_DATA_DIR);
  if (isSpectralDataDirectory(build_data)) return build_data;
#endif

  for (const auto& path : {std::filesystem::path("data"),
                           std::filesystem::path("../data")}) {
    if (isSpectralDataDirectory(path)) return path;
  }

#ifdef VLB_SOURCE_DATA_DIR
  const std::filesystem::path source_data(VLB_SOURCE_DATA_DIR);
  if (isSpectralDataDirectory(source_data)) return source_data;
#endif

  throw std::runtime_error(
      "Unable to locate the complete spectral data directory. Use --data-dir "
      "PATH or set VLB_DATA_DIR.");
}

}  // namespace vlb
