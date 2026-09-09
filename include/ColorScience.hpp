#pragma once

#include "SpectralData.hpp"

#include <opencv2/core.hpp>

#include <array>
#include <string>
#include <vector>

namespace vlb {

enum class OutputSpace { SRGB, AdobeRGB1998 };

struct CameraModel {
  cv::Matx33d raw_to_xyz = cv::Matx33d::eye();
  cv::Vec3d d65_white_raw{1.0, 1.0, 1.0};
};

// Fixed physical spectra for a synthetic reference display. The spectra are
// generated once by fitting smooth Gaussian-mixture primaries to the standard
// 1931 2-degree chromaticities of the requested RGB space. The same spectra
// are then reused for every observer.
struct SpectralDisplayPrimaries {
  std::string name;
  std::array<std::vector<double>, 3> rgb;
};

// Observer-specific colorimetric characterization of one fixed spectral
// display. RGB->XYZ is formed by integrating the physical primary spectra with
// the selected observer and scaling the three primaries to that observer's D65
// white. XYZ->RGB is its inverse.
struct ReferenceDisplayModel {
  std::string name;
  OutputSpace output_space = OutputSpace::SRGB;
  cv::Matx33d rgb_to_xyz = cv::Matx33d::eye();
  cv::Matx33d xyz_to_rgb = cv::Matx33d::eye();
  cv::Vec3d d65_white_xyz{1.0, 1.0, 1.0};
};

double illuminantNormalization(const SpectralData& data,
                               const StandardObserver& observer,
                               const Illuminant& illuminant);

cv::Vec3d illuminantWhiteXYZ(const SpectralData& data,
                             const StandardObserver& observer,
                             const Illuminant& illuminant);

cv::Vec3d reflectanceToXYZ(const SpectralData& data,
                           const StandardObserver& observer,
                           const std::vector<double>& reflectance,
                           const Illuminant& illuminant,
                           double nd_transmission);

cv::Vec3d bradfordAdapt(const cv::Vec3d& xyz,
                        const cv::Vec3d& source_white,
                        const cv::Vec3d& destination_white);

CameraModel buildCameraModel(const SpectralData& data,
                             const StandardObserver& reference_observer,
                             const CameraSensitivity& camera,
                             const Illuminant& d65);

cv::Vec3d cameraRawResponse(const SpectralData& data,
                            const StandardObserver& reference_observer,
                            const CameraSensitivity& camera,
                            const std::vector<double>& reflectance,
                            const Illuminant& illuminant,
                            double nd_transmission);

cv::Vec3d cameraResponseToXYZ(const SpectralData& data,
                              const StandardObserver& reference_observer,
                              const CameraSensitivity& camera,
                              const CameraModel& camera_model,
                              const std::vector<double>& reflectance,
                              const Illuminant& illuminant,
                              double nd_transmission,
                              bool white_balance);

SpectralDisplayPrimaries buildReferenceDisplayPrimaries(
    const SpectralData& data,
    const StandardObserver& cie_1931_2deg_observer,
    OutputSpace output_space);

ReferenceDisplayModel buildReferenceDisplayModel(
    const SpectralData& data,
    const StandardObserver& observer,
    const Illuminant& d65,
    const SpectralDisplayPrimaries& primaries,
    OutputSpace output_space);

cv::Vec3d xyzToEncodedRgb(const cv::Vec3d& xyz,
                          const ReferenceDisplayModel& display_model);

std::string outputSpaceName(OutputSpace output_space);

}  // namespace vlb
