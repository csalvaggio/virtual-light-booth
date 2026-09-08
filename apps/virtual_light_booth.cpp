#include "ColorScience.hpp"
#include "SpectralData.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <portable-file-dialogs.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr char kWindowName[] = "Virtual Light Booth - ColorChecker";
constexpr int kCanvasWidth = 1200;
constexpr int kCanvasHeight = 800;
constexpr int kPatchSize = 125;
constexpr int kGap = 20;
constexpr int kBorder = 60;
constexpr int kColumns = 6;
constexpr int kRows = 4;
constexpr double kNdStep = 0.05;
constexpr char kDefaultCameraName[] = "Canon 5DMarkII";

struct AppState {
  std::size_t illuminant_index = 0;
  std::size_t camera_index = 0;
  double nd_transmission = 1.0;
  bool camera_mode = false;
  bool chromatic_adaptation = false;
  bool camera_white_balance = false;
  vlb::OutputSpace output_space = vlb::OutputSpace::SRGB;
};

double parseSamplingNm(int argc, char** argv) {
  double sampling_nm = 5.0;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--sampling-nm") {
      sampling_nm = std::stod(argv[i + 1]);
    }
  }
  if (sampling_nm <= 0.0 || sampling_nm > 20.0) {
    throw std::runtime_error("--sampling-nm must be in the range (0, 20].");
  }
  return sampling_nm;
}

std::size_t findIlluminantIndex(const vlb::SpectralData& data,
                                const std::string& name) {
  for (std::size_t i = 0; i < data.illuminants.size(); ++i) {
    if (data.illuminants[i].name == name) return i;
  }
  throw std::runtime_error("Required illuminant not found: " + name);
}

std::size_t findCameraIndex(const vlb::SpectralData& data,
                            const std::string& name) {
  for (std::size_t i = 0; i < data.cameras.size(); ++i) {
    if (data.cameras[i].name == name) return i;
  }
  throw std::runtime_error("Required camera not found: " + name);
}

cv::Vec3b encodedRgbToBgr8(const cv::Vec3d& rgb) {
  auto to8 = [](double v) {
    return static_cast<unsigned char>(
        std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
  };
  return cv::Vec3b(to8(rgb[2]), to8(rgb[1]), to8(rgb[0]));
}

cv::Vec3d renderReflectance(const vlb::SpectralData& data,
                            const vlb::CameraSensitivity& camera,
                            const vlb::CameraModel& camera_model,
                            const vlb::Illuminant& illuminant,
                            const std::vector<double>& reflectance,
                            const AppState& state,
                            const cv::Vec3d& d65_white) {
  cv::Vec3d xyz;

  if (state.camera_mode) {
    xyz = vlb::cameraResponseToXYZ(data, camera, camera_model, reflectance,
                                   illuminant, state.nd_transmission,
                                   state.camera_white_balance);
  } else {
    xyz = vlb::reflectanceToXYZ(data, reflectance, illuminant,
                                state.nd_transmission);
    if (state.chromatic_adaptation) {
      const cv::Vec3d source_white = vlb::illuminantWhiteXYZ(data, illuminant);
      xyz = vlb::bradfordAdapt(xyz, source_white, d65_white);
    }
  }

  return vlb::xyzToEncodedRgb(xyz, state.output_space);
}

cv::Mat renderChart(const vlb::SpectralData& data,
                    const std::vector<vlb::CameraModel>& camera_models,
                    const AppState& state,
                    const cv::Vec3d& d65_white) {
  const auto& illuminant = data.illuminants.at(state.illuminant_index);
  const auto& camera = data.cameras.at(state.camera_index);
  const auto& camera_model = camera_models.at(state.camera_index);

  // Model the outer background as a spectrally flat 18% gray reflector so it
  // participates in the same illuminant, ND, observer/camera, and output-space
  // pipeline as the ColorChecker itself.
  const std::vector<double> background_reflectance(
      data.wavelengths_nm.size(), 0.18);
  const cv::Vec3b background_bgr = encodedRgbToBgr8(renderReflectance(
      data, camera, camera_model, illuminant, background_reflectance, state,
      d65_white));

  cv::Mat canvas(kCanvasHeight, kCanvasWidth, CV_8UC3,
                 cv::Scalar(background_bgr[0], background_bgr[1],
                            background_bgr[2]));

  const int board_width =
      kColumns * kPatchSize + (kColumns - 1) * kGap + 2 * kBorder;
  const int board_height =
      kRows * kPatchSize + (kRows - 1) * kGap + 2 * kBorder;
  const int board_x = (kCanvasWidth - board_width) / 2;
  const int board_y = (kCanvasHeight - board_height) / 2;

  // Model the checker surround as a spectrally flat 3% neutral reflector.
  const std::vector<double> surround_reflectance(data.wavelengths_nm.size(),
                                                  0.03);
  const cv::Vec3b surround_bgr = encodedRgbToBgr8(renderReflectance(
      data, camera, camera_model, illuminant, surround_reflectance, state,
      d65_white));

  cv::rectangle(canvas, cv::Rect(board_x, board_y, board_width, board_height),
                cv::Scalar(surround_bgr[0], surround_bgr[1], surround_bgr[2]),
                cv::FILLED);

  for (int row = 0; row < kRows; ++row) {
    for (int col = 0; col < kColumns; ++col) {
      const std::size_t index = static_cast<std::size_t>(row * kColumns + col);
      const auto& patch = data.patches.at(index);
      const cv::Vec3b bgr = encodedRgbToBgr8(renderReflectance(
          data, camera, camera_model, illuminant, patch.reflectance, state,
          d65_white));

      const int x = board_x + kBorder + col * (kPatchSize + kGap);
      const int y = board_y + kBorder + row * (kPatchSize + kGap);
      cv::rectangle(canvas, cv::Rect(x, y, kPatchSize, kPatchSize),
                    cv::Scalar(bgr[0], bgr[1], bgr[2]), cv::FILLED);
    }
  }

  return canvas;
}

std::string responseName(const vlb::SpectralData& data,
                         const AppState& state) {
  return state.camera_mode ? data.cameras.at(state.camera_index).name
                           : "CIE 1931 2-degree observer";
}

std::string sanitizeFilenameComponent(std::string value) {
  for (char& ch : value) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9')) {
      continue;
    }
    ch = '_';
  }

  while (!value.empty() && value.back() == '_') value.pop_back();
  while (!value.empty() && value.front() == '_') value.erase(value.begin());
  return value.empty() ? "render" : value;
}

std::string defaultSaveFilename(const vlb::SpectralData& data,
                                const AppState& state) {
  const auto& illuminant = data.illuminants.at(state.illuminant_index);

  std::ostringstream oss;
  oss << "virtual_light_booth_" << sanitizeFilenameComponent(illuminant.name)
      << "_nd_" << std::fixed << std::setprecision(2) << state.nd_transmission;

  if (state.camera_mode) {
    oss << "_camera_"
        << sanitizeFilenameComponent(data.cameras.at(state.camera_index).name)
        << "_wb_" << (state.camera_white_balance ? "on" : "off");
  } else {
    oss << "_cie_adapt_" << (state.chromatic_adaptation ? "on" : "off");
  }

  oss << '_'
      << ((state.output_space == vlb::OutputSpace::SRGB) ? "srgb"
                                                        : "adobe_rgb");

  return sanitizeFilenameComponent(oss.str()) + ".png";
}

std::filesystem::path ensureSupportedImageExtension(std::filesystem::path path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  if (ext.empty()) {
    path += ".png";
    return path;
  }

  if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tif" ||
      ext == ".tiff" || ext == ".bmp") {
    return path;
  }

  path.replace_extension(".png");
  return path;
}

std::filesystem::path chooseSavePath(const vlb::SpectralData& data,
                                     const AppState& state) {
  const std::filesystem::path default_name = defaultSaveFilename(data, state);
  const std::vector<std::string> filters = {
      "PNG image", "*.png", "JPEG image", "*.jpg *.jpeg",
      "TIFF image", "*.tif *.tiff", "Bitmap image", "*.bmp"};

  pfd::save_file dialog("Save rendered image", default_name.string(), filters,
                        pfd::opt::force_overwrite);
  const std::string selected = dialog.result();
  if (selected.empty()) return {};

  return ensureSupportedImageExtension(std::filesystem::path(selected));
}

void printState(const vlb::SpectralData& data, const AppState& state,
                double sampling_nm) {
  const auto& illuminant = data.illuminants.at(state.illuminant_index);

  std::cout << "\n";
  std::cout << "Illuminant: " << illuminant.name << "\n";
  std::cout << "ND transmission: " << std::fixed << std::setprecision(2)
            << state.nd_transmission << "\n";
  std::cout << "Response: " << responseName(data, state) << "\n";
  std::cout << "Output: " << vlb::outputSpaceName(state.output_space) << "\n";

  if (state.camera_mode) {
    std::cout << "Camera WB: "
              << (state.camera_white_balance ? "ON" : "OFF") << "\n";
  } else {
    std::cout << "Bradford adaptation: "
              << (state.chromatic_adaptation ? "ON" : "OFF") << "\n";
  }

  std::cout << "Grid spacing: " << sampling_nm << " nm" << "\n";
}

void printHelp() {
  std::cout
      << "\nVirtual Light Booth controls\n"
      << "  i / I     next / previous illuminant\n"
      << "  [ / ]     decrease / increase ND transmission by 0.05\n"
      << "  c         toggle CIE observer / camera spectral response\n"
      << "  m / M     next / previous Jiang camera model (camera mode only)\n"
      << "  o         toggle sRGB / Adobe RGB (1998) encoding\n"
      << "  a         toggle Bradford adaptation (CIE mode)\n"
      << "  w         toggle camera white balance (camera mode)\n"
      << "  Esc       reset to D65, ND=1, CIE observer (default camera), "
      << "sRGB\n"
      << "  s         save current rendered image using a file-save dialog\n"
      << "  ?         display this help\n"
      << "  q         quit\n\n";
}

void printCameraMatrix(const std::string& camera_name,
                       const vlb::CameraModel& model) {
  std::cout << camera_name << " D65-fitted raw-to-XYZ matrix:\n";
  for (int r = 0; r < 3; ++r) {
    std::cout << "  [ ";
    for (int c = 0; c < 3; ++c) {
      std::cout << std::setw(11) << std::fixed << std::setprecision(6)
                << model.raw_to_xyz(r, c) << (c == 2 ? " " : ", ");
    }
    std::cout << "]\n";
  }
}

AppState defaultState(std::size_t d65_index, std::size_t camera_index) {
  AppState state;
  state.illuminant_index = d65_index;
  state.camera_index = camera_index;
  return state;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const double sampling_nm = parseSamplingNm(argc, argv);
    const std::filesystem::path data_dir = vlb::locateDataDirectory(argc, argv);
    const vlb::SpectralData data = vlb::loadSpectralData(data_dir, sampling_nm);

    const std::size_t d65_index = findIlluminantIndex(data, "D65");
    const std::size_t default_camera_index =
        findCameraIndex(data, kDefaultCameraName);
    const vlb::Illuminant& d65 = data.illuminants.at(d65_index);
    const cv::Vec3d d65_white = vlb::illuminantWhiteXYZ(data, d65);

    std::vector<vlb::CameraModel> camera_models;
    camera_models.reserve(data.cameras.size());
    for (const auto& camera : data.cameras) {
      camera_models.push_back(vlb::buildCameraModel(data, camera, d65));
    }

    AppState state = defaultState(d65_index, default_camera_index);

    std::cout << "Loaded spectral data from: " << data_dir << "\n";
    std::cout << "ColorChecker patches: " << data.patches.size() << "\n";
    std::cout << "Illuminants: ";
    for (std::size_t i = 0; i < data.illuminants.size(); ++i) {
      if (i != 0) std::cout << ", ";
      std::cout << data.illuminants[i].name;
    }
    std::cout << "\nJiang cameras: " << data.cameras.size() << "\n";
    std::cout << "Default camera: "
              << data.cameras.at(default_camera_index).name << "\n";
    printCameraMatrix(data.cameras.at(default_camera_index).name,
                      camera_models.at(default_camera_index));
    printHelp();

    cv::namedWindow(kWindowName, cv::WINDOW_AUTOSIZE);

    bool quit = false;
    while (!quit) {
      const cv::Mat image = renderChart(data, camera_models, state, d65_white);
      cv::imshow(kWindowName, image);
      printState(data, state, sampling_nm);

      bool redraw = false;
      while (!redraw && !quit) {
        const int key = cv::waitKey(0);

        switch (key) {
          case 'q':
            quit = true;
            break;

          case 27:  // Esc
            state = defaultState(d65_index, default_camera_index);
            std::cout << "\n";
            std::cout << "RESET: default state restablished" << "\n";
            redraw = true;
            break;

          case 'i':
            state.illuminant_index =
                (state.illuminant_index + 1) % data.illuminants.size();
            redraw = true;
            break;

          case 'I':
            state.illuminant_index =
                (state.illuminant_index + data.illuminants.size() - 1) %
                data.illuminants.size();
            redraw = true;
            break;

          case ']':
            state.nd_transmission =
                std::clamp(state.nd_transmission + kNdStep, 0.0, 1.0);
            redraw = true;
            break;

          case '[':
            state.nd_transmission =
                std::clamp(state.nd_transmission - kNdStep, 0.0, 1.0);
            redraw = true;
            break;

          case 'c':
          case 'C':
            state.camera_mode = !state.camera_mode;
            redraw = true;
            break;

          case 'm':
            if (!state.camera_mode) break;
            state.camera_index =
                (state.camera_index + 1) % data.cameras.size();
            std::cout << "Selected camera: "
                      << data.cameras.at(state.camera_index).name << "\n";
            redraw = true;
            break;

          case 'M':
            if (!state.camera_mode) break;
            state.camera_index =
                (state.camera_index + data.cameras.size() - 1) %
                data.cameras.size();
            std::cout << "Selected camera: "
                      << data.cameras.at(state.camera_index).name << "\n";
            redraw = true;
            break;

          case 'o':
          case 'O':
            state.output_space =
                (state.output_space == vlb::OutputSpace::SRGB)
                    ? vlb::OutputSpace::AdobeRGB1998
                    : vlb::OutputSpace::SRGB;
            redraw = true;
            break;

          case 'a':
          case 'A':
            if (state.camera_mode) break;
            state.chromatic_adaptation = !state.chromatic_adaptation;
            redraw = true;
            break;

          case 'w':
          case 'W':
            if (!state.camera_mode) break;
            state.camera_white_balance = !state.camera_white_balance;
            redraw = true;
            break;

          case 's':
          case 'S': {
            const std::filesystem::path output = chooseSavePath(data, state);
            if (output.empty()) {
              std::cout << "Save canceled.\n";
            } else if (!cv::imwrite(output.string(), image)) {
              std::cerr << "Unable to save " << output << "\n";
            } else {
              std::cout << "Saved " << output << "\n";
            }
            break;
          }

          case '?':
            printHelp();
            break;

          default:
            break;
        }
      }
    }

    cv::destroyAllWindows();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}
