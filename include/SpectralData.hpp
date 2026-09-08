#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace vlb {

struct Patch {
  std::string id;
  std::string name;
  std::vector<double> reflectance;
};

struct Illuminant {
  std::string name;
  std::vector<double> spd;
};

struct CameraSensitivity {
  std::string name;
  std::array<std::vector<double>, 3> rgb;
};

struct SpectralData {
  std::vector<double> wavelengths_nm;
  std::array<std::vector<double>, 3> xyz_cmf;
  std::vector<Illuminant> illuminants;
  std::vector<Patch> patches;
  std::vector<CameraSensitivity> cameras;
};

SpectralData loadSpectralData(const std::filesystem::path& data_dir,
                              double sampling_nm = 5.0);

std::filesystem::path locateDataDirectory(int argc, char** argv);

}  // namespace vlb
