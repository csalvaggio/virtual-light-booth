#include "ColorScience.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vlb {
namespace {

constexpr double kEpsilon = 1e-12;

double sampleSpacing(const SpectralData& data) {
  if (data.wavelengths_nm.size() < 2) {
    throw std::runtime_error("Spectral wavelength grid is too small.");
  }
  return data.wavelengths_nm[1] - data.wavelengths_nm[0];
}

cv::Vec3d cameraWhiteResponse(const SpectralData& data,
                              const CameraSensitivity& camera,
                              const Illuminant& illuminant) {
  const std::vector<double> perfect_white(data.wavelengths_nm.size(), 1.0);
  return cameraRawResponse(data, camera, perfect_white, illuminant, 1.0);
}

double srgbEncode(double linear) {
  linear = std::clamp(linear, 0.0, 1.0);
  if (linear <= 0.0031308) return 12.92 * linear;
  return 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

double adobeEncode(double linear) {
  linear = std::clamp(linear, 0.0, 1.0);
  constexpr double gamma = 563.0 / 256.0;
  return std::pow(linear, 1.0 / gamma);
}

}  // namespace

double illuminantNormalization(const SpectralData& data,
                               const Illuminant& illuminant) {
  const double dl = sampleSpacing(data);
  double denominator = 0.0;
  for (std::size_t i = 0; i < data.wavelengths_nm.size(); ++i) {
    denominator += illuminant.spd[i] * data.xyz_cmf[1][i] * dl;
  }
  if (denominator <= kEpsilon) {
    throw std::runtime_error("Illuminant has zero photopic energy.");
  }
  return 1.0 / denominator;
}

cv::Vec3d illuminantWhiteXYZ(const SpectralData& data,
                             const Illuminant& illuminant) {
  const double k = illuminantNormalization(data, illuminant);
  const double dl = sampleSpacing(data);
  cv::Vec3d xyz(0.0, 0.0, 0.0);
  for (std::size_t i = 0; i < data.wavelengths_nm.size(); ++i) {
    for (int c = 0; c < 3; ++c) {
      xyz[c] += k * illuminant.spd[i] * data.xyz_cmf[c][i] * dl;
    }
  }
  return xyz;
}

cv::Vec3d reflectanceToXYZ(const SpectralData& data,
                           const std::vector<double>& reflectance,
                           const Illuminant& illuminant,
                           double nd_transmission) {
  if (reflectance.size() != data.wavelengths_nm.size()) {
    throw std::runtime_error("Reflectance spectrum size mismatch.");
  }
  nd_transmission = std::clamp(nd_transmission, 0.0, 1.0);

  // Normalize the unfiltered illuminant so a perfect reflecting diffuser has
  // Y=1. Apply ND after that normalization so it changes exposure as intended.
  const double k = illuminantNormalization(data, illuminant);
  const double dl = sampleSpacing(data);
  cv::Vec3d xyz(0.0, 0.0, 0.0);

  for (std::size_t i = 0; i < data.wavelengths_nm.size(); ++i) {
    const double leaving_energy =
        k * illuminant.spd[i] * nd_transmission * reflectance[i];
    for (int c = 0; c < 3; ++c) {
      xyz[c] += leaving_energy * data.xyz_cmf[c][i] * dl;
    }
  }
  return xyz;
}

cv::Vec3d bradfordAdapt(const cv::Vec3d& xyz,
                        const cv::Vec3d& source_white,
                        const cv::Vec3d& destination_white) {
  const cv::Matx33d M(0.8951, 0.2664, -0.1614,
                      -0.7502, 1.7135, 0.0367,
                      0.0389, -0.0685, 1.0296);
  const cv::Matx33d M_inv(0.9869929, -0.1470543, 0.1599627,
                          0.4323053, 0.5183603, 0.0492912,
                          -0.0085287, 0.0400428, 0.9684867);

  const cv::Vec3d src_lms = M * source_white;
  const cv::Vec3d dst_lms = M * destination_white;
  const cv::Vec3d xyz_lms = M * xyz;

  cv::Vec3d adapted_lms;
  for (int i = 0; i < 3; ++i) {
    const double scale = (std::abs(src_lms[i]) > kEpsilon)
                             ? dst_lms[i] / src_lms[i]
                             : 1.0;
    adapted_lms[i] = xyz_lms[i] * scale;
  }
  return M_inv * adapted_lms;
}

cv::Vec3d cameraRawResponse(const SpectralData& data,
                            const CameraSensitivity& camera,
                            const std::vector<double>& reflectance,
                            const Illuminant& illuminant,
                            double nd_transmission) {
  if (reflectance.size() != data.wavelengths_nm.size()) {
    throw std::runtime_error("Reflectance spectrum size mismatch.");
  }
  nd_transmission = std::clamp(nd_transmission, 0.0, 1.0);

  const double k = illuminantNormalization(data, illuminant);
  const double dl = sampleSpacing(data);
  cv::Vec3d raw(0.0, 0.0, 0.0);

  for (std::size_t i = 0; i < data.wavelengths_nm.size(); ++i) {
    const double leaving_energy =
        k * illuminant.spd[i] * nd_transmission * reflectance[i];
    for (int c = 0; c < 3; ++c) {
      raw[c] += leaving_energy * camera.rgb[c][i] * dl;
    }
  }
  return raw;
}

CameraModel buildCameraModel(const SpectralData& data,
                             const CameraSensitivity& camera,
                             const Illuminant& d65) {
  cv::Mat raw(static_cast<int>(data.patches.size()), 3, CV_64F);
  cv::Mat xyz(static_cast<int>(data.patches.size()), 3, CV_64F);

  for (std::size_t i = 0; i < data.patches.size(); ++i) {
    const cv::Vec3d r = cameraRawResponse(
        data, camera, data.patches[i].reflectance, d65, 1.0);
    const cv::Vec3d x = reflectanceToXYZ(
        data, data.patches[i].reflectance, d65, 1.0);

    for (int c = 0; c < 3; ++c) {
      raw.at<double>(static_cast<int>(i), c) = r[c];
      xyz.at<double>(static_cast<int>(i), c) = x[c];
    }
  }

  cv::Mat row_raw_to_row_xyz;
  if (!cv::solve(raw, xyz, row_raw_to_row_xyz, cv::DECOMP_SVD)) {
    throw std::runtime_error("Unable to solve camera color-correction matrix: " +
                             camera.name);
  }

  CameraModel model;
  // cv::solve gives B such that [raw row] B = [XYZ row]. Store B^T so that
  // column-vector use is XYZ = M * raw.
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      model.raw_to_xyz(r, c) = row_raw_to_row_xyz.at<double>(c, r);
    }
  }

  model.d65_white_raw = cameraWhiteResponse(data, camera, d65);
  return model;
}

cv::Vec3d cameraResponseToXYZ(const SpectralData& data,
                              const CameraSensitivity& camera,
                              const CameraModel& camera_model,
                              const std::vector<double>& reflectance,
                              const Illuminant& illuminant,
                              double nd_transmission,
                              bool white_balance) {
  cv::Vec3d raw = cameraRawResponse(data, camera, reflectance, illuminant,
                                    nd_transmission);

  if (white_balance) {
    const cv::Vec3d current_white =
        cameraWhiteResponse(data, camera, illuminant);
    for (int c = 0; c < 3; ++c) {
      if (std::abs(current_white[c]) > kEpsilon) {
        raw[c] *= camera_model.d65_white_raw[c] / current_white[c];
      }
    }
  }

  return camera_model.raw_to_xyz * raw;
}

cv::Vec3d xyzToEncodedRgb(const cv::Vec3d& xyz, OutputSpace output_space) {
  cv::Vec3d linear;

  if (output_space == OutputSpace::SRGB) {
    const cv::Matx33d M(3.2404542, -1.5371385, -0.4985314,
                        -0.9692660, 1.8760108, 0.0415560,
                        0.0556434, -0.2040259, 1.0572252);
    linear = M * xyz;
    return cv::Vec3d(srgbEncode(linear[0]), srgbEncode(linear[1]),
                     srgbEncode(linear[2]));
  }

  const cv::Matx33d M(2.0413690, -0.5649464, -0.3446944,
                      -0.9692660, 1.8760108, 0.0415560,
                      0.0134474, -0.1183897, 1.0154096);
  linear = M * xyz;
  return cv::Vec3d(adobeEncode(linear[0]), adobeEncode(linear[1]),
                   adobeEncode(linear[2]));
}

std::string outputSpaceName(OutputSpace output_space) {
  return output_space == OutputSpace::SRGB ? "sRGB" : "Adobe RGB (1998)";
}

}  // namespace vlb
