#include "ColorScience.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace vlb {
namespace {

constexpr double kEpsilon = 1e-12;
constexpr double kReferencePrimaryFwhmNm = 20.0;

struct ReferencePrimaryDefinition {
  const char* name;
  std::array<std::array<double, 2>, 3> xy;
  std::array<std::array<double, 3>, 3> gaussian_centers_nm;
};

double sampleSpacing(const SpectralData& data) {
  if (data.wavelengths_nm.size() < 2) {
    throw std::runtime_error("Spectral wavelength grid is too small.");
  }
  return data.wavelengths_nm[1] - data.wavelengths_nm[0];
}

double determinant3x3(const cv::Matx33d& m) {
  return m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) -
         m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) +
         m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
}

cv::Matx33d inverse3x3(const cv::Matx33d& m) {
  const double det = determinant3x3(m);
  if (std::abs(det) <= kEpsilon) {
    throw std::runtime_error("Unable to invert singular 3x3 color matrix.");
  }

  return cv::Matx33d(
      (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) / det,
      (m(0, 2) * m(2, 1) - m(0, 1) * m(2, 2)) / det,
      (m(0, 1) * m(1, 2) - m(0, 2) * m(1, 1)) / det,
      (m(1, 2) * m(2, 0) - m(1, 0) * m(2, 2)) / det,
      (m(0, 0) * m(2, 2) - m(0, 2) * m(2, 0)) / det,
      (m(0, 2) * m(1, 0) - m(0, 0) * m(1, 2)) / det,
      (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0)) / det,
      (m(0, 1) * m(2, 0) - m(0, 0) * m(2, 1)) / det,
      (m(0, 0) * m(1, 1) - m(0, 1) * m(1, 0)) / det);
}

cv::Vec3d solve3x3(const cv::Matx33d& a, const cv::Vec3d& b) {
  return inverse3x3(a) * b;
}

cv::Vec3d cameraWhiteResponse(const SpectralData& data,
                              const StandardObserver& reference_observer,
                              const CameraSensitivity& camera,
                              const Illuminant& illuminant) {
  const std::vector<double> perfect_white(data.wavelengths_nm.size(), 1.0);
  return cameraRawResponse(data, reference_observer, camera, perfect_white,
                           illuminant, 1.0);
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

std::vector<double> gaussianSpectrum(const SpectralData& data,
                                     double center_nm,
                                     double fwhm_nm) {
  const double sigma_nm =
      fwhm_nm / (2.0 * std::sqrt(2.0 * std::log(2.0)));

  std::vector<double> spectrum;
  spectrum.reserve(data.wavelengths_nm.size());
  for (const double wavelength_nm : data.wavelengths_nm) {
    const double offset = (wavelength_nm - center_nm) / sigma_nm;
    spectrum.push_back(std::exp(-0.5 * offset * offset));
  }
  return spectrum;
}

cv::Vec3d emissionToXYZ(const SpectralData& data,
                        const StandardObserver& observer,
                        const std::vector<double>& spectrum) {
  if (spectrum.size() != data.wavelengths_nm.size()) {
    throw std::runtime_error("Emission spectrum size mismatch.");
  }

  const double dl = sampleSpacing(data);
  cv::Vec3d xyz(0.0, 0.0, 0.0);
  for (std::size_t i = 0; i < data.wavelengths_nm.size(); ++i) {
    for (int c = 0; c < 3; ++c) {
      xyz[c] += spectrum[i] * observer.xyz_cmf[c][i] * dl;
    }
  }
  return xyz;
}

cv::Vec3d targetXYZFromChromaticity(const std::array<double, 2>& xy) {
  const double x = xy[0];
  const double y = xy[1];
  const double z = 1.0 - x - y;
  if (y <= kEpsilon || z < 0.0) {
    throw std::runtime_error("Invalid reference-display chromaticity.");
  }

  // Chromaticity determines only direction in XYZ space. Set Y=1 to obtain a
  // convenient target vector; any positive scale would yield the same x,y.
  return cv::Vec3d(x / y, 1.0, z / y);
}

ReferencePrimaryDefinition primaryDefinition(OutputSpace output_space) {
  // Each physical primary is represented as a nonnegative mixture of three
  // smooth 20 nm FWHM Gaussian basis spectra. The basis wavelengths were
  // selected so the standard 1931 2-degree primary chromaticities can be
  // matched with positive weights while retaining a compact, reproducible
  // spectral model.
  if (output_space == OutputSpace::SRGB) {
    return ReferencePrimaryDefinition{
        "Reference sRGB spectral display",
        {{{0.64, 0.33}, {0.30, 0.60}, {0.15, 0.06}}},
        {{{460.0, 585.0, 625.0},
          {470.0, 550.0, 570.0},
          {455.0, 525.0, 580.0}}}};
  }

  return ReferencePrimaryDefinition{
      "Reference Adobe RGB (1998) spectral display",
      {{{0.64, 0.33}, {0.21, 0.71}, {0.15, 0.06}}},
      {{{460.0, 585.0, 625.0},
        {470.0, 535.0, 565.0},
        {455.0, 525.0, 580.0}}}};
}

std::vector<double> fitPrimarySpectrum(
    const SpectralData& data,
    const StandardObserver& cie_1931_2deg_observer,
    const std::array<double, 2>& target_xy,
    const std::array<double, 3>& gaussian_centers_nm) {
  std::array<std::vector<double>, 3> basis;
  cv::Matx33d basis_xyz;

  for (int component = 0; component < 3; ++component) {
    basis[component] = gaussianSpectrum(data, gaussian_centers_nm[component],
                                        kReferencePrimaryFwhmNm);
    const cv::Vec3d xyz =
        emissionToXYZ(data, cie_1931_2deg_observer, basis[component]);
    for (int row = 0; row < 3; ++row) {
      basis_xyz(row, component) = xyz[row];
    }
  }

  const cv::Vec3d target_xyz = targetXYZFromChromaticity(target_xy);
  const cv::Vec3d weights = solve3x3(basis_xyz, target_xyz);

  std::vector<double> primary(data.wavelengths_nm.size(), 0.0);
  for (int component = 0; component < 3; ++component) {
    if (weights[component] < -1e-8) {
      throw std::runtime_error(
          "Reference-display primary fit produced a negative spectral weight.");
    }
    const double weight = std::max(0.0, weights[component]);
    for (std::size_t i = 0; i < primary.size(); ++i) {
      primary[i] += weight * basis[component][i];
    }
  }

  const auto peak = std::max_element(primary.begin(), primary.end());
  if (peak == primary.end() || *peak <= kEpsilon) {
    throw std::runtime_error("Reference-display primary has zero energy.");
  }
  for (double& value : primary) value /= *peak;

  return primary;
}

}  // namespace

double illuminantNormalization(const SpectralData& data,
                               const StandardObserver& observer,
                               const Illuminant& illuminant) {
  const double dl = sampleSpacing(data);
  double denominator = 0.0;
  for (std::size_t i = 0; i < data.wavelengths_nm.size(); ++i) {
    denominator += illuminant.spd[i] * observer.xyz_cmf[1][i] * dl;
  }
  if (denominator <= kEpsilon) {
    throw std::runtime_error("Illuminant has zero photopic energy.");
  }
  return 1.0 / denominator;
}

cv::Vec3d illuminantWhiteXYZ(const SpectralData& data,
                             const StandardObserver& observer,
                             const Illuminant& illuminant) {
  const double k = illuminantNormalization(data, observer, illuminant);
  const double dl = sampleSpacing(data);
  cv::Vec3d xyz(0.0, 0.0, 0.0);
  for (std::size_t i = 0; i < data.wavelengths_nm.size(); ++i) {
    for (int c = 0; c < 3; ++c) {
      xyz[c] += k * illuminant.spd[i] * observer.xyz_cmf[c][i] * dl;
    }
  }
  return xyz;
}

cv::Vec3d reflectanceToXYZ(const SpectralData& data,
                           const StandardObserver& observer,
                           const std::vector<double>& reflectance,
                           const Illuminant& illuminant,
                           double nd_transmission) {
  if (reflectance.size() != data.wavelengths_nm.size()) {
    throw std::runtime_error("Reflectance spectrum size mismatch.");
  }
  nd_transmission = std::clamp(nd_transmission, 0.0, 1.0);

  // Normalize the unfiltered illuminant so a perfect reflecting diffuser has
  // Y=1. Apply ND after that normalization so it changes exposure as intended.
  const double k = illuminantNormalization(data, observer, illuminant);
  const double dl = sampleSpacing(data);
  cv::Vec3d xyz(0.0, 0.0, 0.0);

  for (std::size_t i = 0; i < data.wavelengths_nm.size(); ++i) {
    const double leaving_energy =
        k * illuminant.spd[i] * nd_transmission * reflectance[i];
    for (int c = 0; c < 3; ++c) {
      xyz[c] += leaving_energy * observer.xyz_cmf[c][i] * dl;
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
                            const StandardObserver& reference_observer,
                            const CameraSensitivity& camera,
                            const std::vector<double>& reflectance,
                            const Illuminant& illuminant,
                            double nd_transmission) {
  if (reflectance.size() != data.wavelengths_nm.size()) {
    throw std::runtime_error("Reflectance spectrum size mismatch.");
  }
  nd_transmission = std::clamp(nd_transmission, 0.0, 1.0);

  const double k = illuminantNormalization(data, reference_observer, illuminant);
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
                             const StandardObserver& reference_observer,
                             const CameraSensitivity& camera,
                             const Illuminant& d65) {
  cv::Mat raw(static_cast<int>(data.patches.size()), 3, CV_64F);
  cv::Mat xyz(static_cast<int>(data.patches.size()), 3, CV_64F);

  for (std::size_t i = 0; i < data.patches.size(); ++i) {
    const cv::Vec3d r = cameraRawResponse(
        data, reference_observer, camera, data.patches[i].reflectance, d65, 1.0);
    const cv::Vec3d x = reflectanceToXYZ(
        data, reference_observer, data.patches[i].reflectance, d65, 1.0);

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

  model.d65_white_raw =
      cameraWhiteResponse(data, reference_observer, camera, d65);
  return model;
}

cv::Vec3d cameraResponseToXYZ(const SpectralData& data,
                              const StandardObserver& reference_observer,
                              const CameraSensitivity& camera,
                              const CameraModel& camera_model,
                              const std::vector<double>& reflectance,
                              const Illuminant& illuminant,
                              double nd_transmission,
                              bool white_balance) {
  cv::Vec3d raw = cameraRawResponse(data, reference_observer, camera,
                                    reflectance, illuminant, nd_transmission);

  if (white_balance) {
    const cv::Vec3d current_white =
        cameraWhiteResponse(data, reference_observer, camera, illuminant);
    for (int c = 0; c < 3; ++c) {
      if (std::abs(current_white[c]) > kEpsilon) {
        raw[c] *= camera_model.d65_white_raw[c] / current_white[c];
      }
    }
  }

  return camera_model.raw_to_xyz * raw;
}

SpectralDisplayPrimaries buildReferenceDisplayPrimaries(
    const SpectralData& data,
    const StandardObserver& cie_1931_2deg_observer,
    OutputSpace output_space) {
  const ReferencePrimaryDefinition definition = primaryDefinition(output_space);

  SpectralDisplayPrimaries primaries;
  primaries.name = definition.name;
  for (int channel = 0; channel < 3; ++channel) {
    primaries.rgb[channel] = fitPrimarySpectrum(
        data, cie_1931_2deg_observer, definition.xy[channel],
        definition.gaussian_centers_nm[channel]);
  }
  return primaries;
}

ReferenceDisplayModel buildReferenceDisplayModel(
    const SpectralData& data,
    const StandardObserver& observer,
    const Illuminant& d65,
    const SpectralDisplayPrimaries& primaries,
    OutputSpace output_space) {
  cv::Matx33d primary_xyz;
  for (int channel = 0; channel < 3; ++channel) {
    const cv::Vec3d xyz = emissionToXYZ(data, observer, primaries.rgb[channel]);
    for (int row = 0; row < 3; ++row) {
      primary_xyz(row, channel) = xyz[row];
    }
  }

  const cv::Vec3d d65_white = illuminantWhiteXYZ(data, observer, d65);
  const cv::Vec3d scales = solve3x3(primary_xyz, d65_white);

  ReferenceDisplayModel model;
  model.name = primaries.name + " / " + observer.name;
  model.output_space = output_space;
  model.d65_white_xyz = d65_white;

  for (int row = 0; row < 3; ++row) {
    for (int channel = 0; channel < 3; ++channel) {
      if (scales[channel] <= kEpsilon) {
        throw std::runtime_error(
            "Reference-display D65 scaling produced a nonpositive primary.");
      }
      model.rgb_to_xyz(row, channel) =
          primary_xyz(row, channel) * scales[channel];
    }
  }

  model.xyz_to_rgb = inverse3x3(model.rgb_to_xyz);
  return model;
}

cv::Vec3d xyzToEncodedRgb(const cv::Vec3d& xyz,
                          const ReferenceDisplayModel& display_model) {
  const cv::Vec3d linear = display_model.xyz_to_rgb * xyz;

  if (display_model.output_space == OutputSpace::SRGB) {
    return cv::Vec3d(srgbEncode(linear[0]), srgbEncode(linear[1]),
                     srgbEncode(linear[2]));
  }

  return cv::Vec3d(adobeEncode(linear[0]), adobeEncode(linear[1]),
                   adobeEncode(linear[2]));
}

std::string outputSpaceName(OutputSpace output_space) {
  return output_space == OutputSpace::SRGB
             ? "Reference sRGB spectral display"
             : "Reference Adobe RGB (1998) spectral display";
}

}  // namespace vlb
