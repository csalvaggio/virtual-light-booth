#pragma once

#include "SpectralData.hpp"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace vlb {

enum class OutputSpace { SRGB, AdobeRGB1998 };

struct CameraModel {
  cv::Matx33d raw_to_xyz = cv::Matx33d::eye();
  cv::Vec3d d65_white_raw{1.0, 1.0, 1.0};
};

double illuminantNormalization(const SpectralData& data,
                               const Illuminant& illuminant);

cv::Vec3d illuminantWhiteXYZ(const SpectralData& data,
                             const Illuminant& illuminant);

cv::Vec3d reflectanceToXYZ(const SpectralData& data,
                           const std::vector<double>& reflectance,
                           const Illuminant& illuminant,
                           double nd_transmission);

cv::Vec3d bradfordAdapt(const cv::Vec3d& xyz,
                        const cv::Vec3d& source_white,
                        const cv::Vec3d& destination_white);

CameraModel buildCameraModel(const SpectralData& data,
                             const CameraSensitivity& camera,
                             const Illuminant& d65);

cv::Vec3d cameraRawResponse(const SpectralData& data,
                            const CameraSensitivity& camera,
                            const std::vector<double>& reflectance,
                            const Illuminant& illuminant,
                            double nd_transmission);

cv::Vec3d cameraResponseToXYZ(const SpectralData& data,
                              const CameraSensitivity& camera,
                              const CameraModel& camera_model,
                              const std::vector<double>& reflectance,
                              const Illuminant& illuminant,
                              double nd_transmission,
                              bool white_balance);

cv::Vec3d xyzToEncodedRgb(const cv::Vec3d& xyz, OutputSpace output_space);

std::string outputSpaceName(OutputSpace output_space);

}  // namespace vlb
