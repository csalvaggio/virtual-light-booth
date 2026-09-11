#include "ColorScience.hpp"
#include "SpectralData.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <portable-file-dialogs.h>

#include <algorithm>
#include <array>
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
constexpr int kCanvasWidth = 1280;
constexpr int kCanvasHeight = 720;
constexpr int kPatchSize = 125;
constexpr int kGap = 20;
constexpr int kBorder = 60;
constexpr int kColumns = 6;
constexpr int kRows = 4;
constexpr double kNdStep = 0.05;
constexpr char kDefaultObserverName[] = "CIE 1964 10-degree observer";
constexpr char kCameraReferenceObserverName[] =
    "CIE 1931 2-degree observer";
constexpr char kDefaultCameraName[] = "Canon 5DMarkII";

struct AppState {
  std::size_t illuminant_index = 0;
  std::size_t observer_index = 0;
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

std::size_t findObserverIndex(const vlb::SpectralData& data,
                              const std::string& name) {
  for (std::size_t i = 0; i < data.observers.size(); ++i) {
    if (data.observers[i].name == name) return i;
  }
  throw std::runtime_error("Required observer not found: " + name);
}

std::size_t findCameraIndex(const vlb::SpectralData& data,
                            const std::string& name) {
  for (std::size_t i = 0; i < data.cameras.size(); ++i) {
    if (data.cameras[i].name == name) return i;
  }
  throw std::runtime_error("Required camera not found: " + name);
}

std::size_t outputSpaceIndex(vlb::OutputSpace output_space) {
  return output_space == vlb::OutputSpace::SRGB ? 0U : 1U;
}

cv::Vec3b encodedRgbToBgr8(const cv::Vec3d& rgb) {
  auto to8 = [](double v) {
    return static_cast<unsigned char>(
        std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
  };
  return cv::Vec3b(to8(rgb[2]), to8(rgb[1]), to8(rgb[0]));
}

cv::Vec3d renderReflectance(
    const vlb::SpectralData& data,
    const vlb::StandardObserver& camera_reference_observer,
    const vlb::StandardObserver& active_observer,
    const vlb::CameraSensitivity& camera,
    const vlb::CameraModel& camera_model,
    const vlb::ReferenceDisplayModel& display_model,
    const vlb::Illuminant& illuminant,
    const std::vector<double>& reflectance,
    const AppState& state,
    const cv::Vec3d& active_observer_d65_white) {
  cv::Vec3d xyz;

  if (state.camera_mode) {
    xyz = vlb::cameraResponseToXYZ(
        data, camera_reference_observer, camera, camera_model, reflectance,
        illuminant, state.nd_transmission, state.camera_white_balance);
  } else {
    xyz = vlb::reflectanceToXYZ(data, active_observer, reflectance, illuminant,
                                state.nd_transmission);
    if (state.chromatic_adaptation) {
      const cv::Vec3d source_white =
          vlb::illuminantWhiteXYZ(data, active_observer, illuminant);
      xyz = vlb::bradfordAdapt(xyz, source_white,
                               active_observer_d65_white);
    }
  }

  return vlb::xyzToEncodedRgb(xyz, display_model);
}

cv::Mat renderChart(
    const vlb::SpectralData& data,
    const std::vector<vlb::CameraModel>& camera_models,
    const std::vector<std::array<vlb::ReferenceDisplayModel, 2>>&
        display_models,
    std::size_t camera_reference_observer_index,
    const std::vector<cv::Vec3d>& observer_d65_whites,
    const AppState& state) {
  const auto& illuminant = data.illuminants.at(state.illuminant_index);
  const auto& active_observer = data.observers.at(state.observer_index);
  const auto& active_observer_d65_white =
      observer_d65_whites.at(state.observer_index);
  const auto& camera_reference_observer =
      data.observers.at(camera_reference_observer_index);
  const auto& camera = data.cameras.at(state.camera_index);
  const auto& camera_model = camera_models.at(state.camera_index);

  // The camera RGB->XYZ matrices are fitted to the 1931 2-degree observer, so
  // camera mode must use the display model characterized for that same XYZ
  // system. In standard-observer mode, the display model follows the selected
  // observer.
  const std::size_t display_observer_index =
      state.camera_mode ? camera_reference_observer_index : state.observer_index;
  const auto& display_model =
      display_models.at(display_observer_index)
          .at(outputSpaceIndex(state.output_space));

  // Model the outer background as a spectrally flat 18% gray reflector so it
  // participates in the same illuminant, ND, observer/camera, and output-space
  // pipeline as the ColorChecker itself.
  const std::vector<double> background_reflectance(
      data.wavelengths_nm.size(), 0.18);
  const cv::Vec3b background_bgr = encodedRgbToBgr8(renderReflectance(
      data, camera_reference_observer, active_observer, camera, camera_model,
      display_model, illuminant, background_reflectance, state,
      active_observer_d65_white));

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
      data, camera_reference_observer, active_observer, camera, camera_model,
      display_model, illuminant, surround_reflectance, state,
      active_observer_d65_white));

  cv::rectangle(canvas, cv::Rect(board_x, board_y, board_width, board_height),
                cv::Scalar(surround_bgr[0], surround_bgr[1], surround_bgr[2]),
                cv::FILLED);

  for (int row = 0; row < kRows; ++row) {
    for (int col = 0; col < kColumns; ++col) {
      const std::size_t index = static_cast<std::size_t>(row * kColumns + col);
      const auto& patch = data.patches.at(index);
      const cv::Vec3b bgr = encodedRgbToBgr8(renderReflectance(
          data, camera_reference_observer, active_observer, camera,
          camera_model, display_model, illuminant, patch.reflectance, state,
          active_observer_d65_white));

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
                           : data.observers.at(state.observer_index).name;
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
    oss << "_observer_"
        << sanitizeFilenameComponent(data.observers.at(state.observer_index).name)
        << "_adapt_" << (state.chromatic_adaptation ? "on" : "off");
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
      << "  m / M     next / previous response model\n"
      << "            (standard observer in CIE mode; camera in camera mode)\n"
      << "  o         toggle sRGB / Adobe RGB (1998) encoding\n"
      << "  a         toggle Bradford adaptation (CIE mode)\n"
      << "  w         toggle camera white balance (camera mode)\n"
      << "  Esc       reset to D65, ND=1, 10-degree observer, default camera, "
      << "sRGB\n"
      << "  s         save current rendered image using a file-save dialog\n"
      << "  ?         display this help\n"
      << "  q         quit\n\n";
}

void printCameraMatrix(const std::string& camera_name,
                       const vlb::CameraModel& model) {
  std::cout << camera_name
            << " spectral raw-to-XYZ matrix (2-degree CIE reference):\n";
  for (int r = 0; r < 3; ++r) {
    std::cout << "  [ ";
    for (int c = 0; c < 3; ++c) {
      std::cout << std::setw(11) << std::fixed << std::setprecision(6)
                << model.raw_to_xyz(r, c) << (c == 2 ? " " : ", ");
    }
    std::cout << "]\n";
  }
}

void printDisplayMatrix(const vlb::ReferenceDisplayModel& model) {
  std::cout << model.name << " XYZ-to-linear-RGB matrix:\n";
  for (int r = 0; r < 3; ++r) {
    std::cout << "  [ ";
    for (int c = 0; c < 3; ++c) {
      std::cout << std::setw(11) << std::fixed << std::setprecision(6)
                << model.xyz_to_rgb(r, c) << (c == 2 ? " " : ", ");
    }
    std::cout << "]\n";
  }
}

AppState defaultState(std::size_t d65_index, std::size_t observer_index,
                      std::size_t camera_index) {
  AppState state;
  state.illuminant_index = d65_index;
  state.observer_index = observer_index;
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
    const std::size_t default_observer_index =
        findObserverIndex(data, kDefaultObserverName);
    const std::size_t camera_reference_observer_index =
        findObserverIndex(data, kCameraReferenceObserverName);
    const std::size_t default_camera_index =
        findCameraIndex(data, kDefaultCameraName);

    const vlb::Illuminant& d65 = data.illuminants.at(d65_index);
    const vlb::StandardObserver& camera_reference_observer =
        data.observers.at(camera_reference_observer_index);

    std::vector<cv::Vec3d> observer_d65_whites;
    observer_d65_whites.reserve(data.observers.size());
    for (const auto& observer : data.observers) {
      observer_d65_whites.push_back(
          vlb::illuminantWhiteXYZ(data, observer, d65));
    }

    // Camera color-correction matrices are fit directly in the spectral
    // domain to the CIE 1931 2-degree CMFs. They intentionally remain tied to
    // that fixed camera colorimetric reference, independently of the selected
    // CIE viewing observer.
    std::vector<vlb::CameraModel> camera_models;
    camera_models.reserve(data.cameras.size());
    for (const auto& camera : data.cameras) {
      camera_models.push_back(vlb::buildCameraModel(
          data, camera_reference_observer, camera, d65));
    }

    // Build one fixed physical primary set for each output RGB space. The
    // spectral shapes are fitted to the standard 1931 2-degree primary
    // chromaticities and their relative powers are calibrated once to D65.
    // Those exact scaled spectra are then characterized separately with every
    // supported observer; observer changes never alter the display itself.
    const std::array<vlb::SpectralDisplayPrimaries, 2> display_primaries = {
        vlb::buildReferenceDisplayPrimaries(
            data, camera_reference_observer, d65, vlb::OutputSpace::SRGB),
        vlb::buildReferenceDisplayPrimaries(
            data, camera_reference_observer, d65,
            vlb::OutputSpace::AdobeRGB1998)};

    std::vector<std::array<vlb::ReferenceDisplayModel, 2>> display_models;
    display_models.reserve(data.observers.size());
    for (const auto& observer : data.observers) {
      display_models.push_back(
          {vlb::buildReferenceDisplayModel(
               data, observer, d65, display_primaries[0],
               vlb::OutputSpace::SRGB),
           vlb::buildReferenceDisplayModel(
               data, observer, d65, display_primaries[1],
               vlb::OutputSpace::AdobeRGB1998)});
    }

    AppState state = defaultState(d65_index, default_observer_index,
                                  default_camera_index);

    std::cout << "Loaded spectral data from: " << data_dir << "\n";
    std::cout << "ColorChecker patches: " << data.patches.size() << "\n";
    std::cout << "Standard observers: ";
    for (std::size_t i = 0; i < data.observers.size(); ++i) {
      if (i != 0) std::cout << ", ";
      std::cout << data.observers[i].name;
    }
    std::cout << "\nIlluminants: ";
    for (std::size_t i = 0; i < data.illuminants.size(); ++i) {
      if (i != 0) std::cout << ", ";
      std::cout << data.illuminants[i].name;
    }
    std::cout << "\nJiang cameras: " << data.cameras.size() << "\n";
    std::cout << "\nDefault observer: "
              << data.observers.at(default_observer_index).name << "\n";
    std::cout << "Default camera: "
              << data.cameras.at(default_camera_index).name << "\n";
    printCameraMatrix(data.cameras.at(default_camera_index).name,
                      camera_models.at(default_camera_index));
    std::cout << "\nReference display primaries: Synthetic Gaussian-mixture "
                 "spectra fitted to standard 2-degree RGB chromaticities "
                 "and power-calibrated once to 2-degree D65\n";
    printDisplayMatrix(
        display_models.at(default_observer_index)
            .at(outputSpaceIndex(vlb::OutputSpace::SRGB)));
    printHelp();

    cv::namedWindow(kWindowName, cv::WINDOW_AUTOSIZE);

    bool quit = false;
    while (!quit) {
      const cv::Mat image =
          renderChart(data, camera_models, display_models,
                      camera_reference_observer_index, observer_d65_whites,
                      state);
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
            state = defaultState(d65_index, default_observer_index,
                                 default_camera_index);
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
            if (state.camera_mode) {
              state.camera_index =
                  (state.camera_index + 1) % data.cameras.size();
            } else {
              state.observer_index =
                  (state.observer_index + 1) % data.observers.size();
            }
            redraw = true;
            break;

          case 'M':
            if (state.camera_mode) {
              state.camera_index =
                  (state.camera_index + data.cameras.size() - 1) %
                  data.cameras.size();
            } else {
              state.observer_index =
                  (state.observer_index + data.observers.size() - 1) %
                  data.observers.size();
            }
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
