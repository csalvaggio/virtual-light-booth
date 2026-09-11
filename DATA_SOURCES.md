# Spectral Data Sources and Provenance

The source code and numerical data in this project have different provenance and may be governed by different licenses. Do not assume that the GNU GPL license applied to the project source code also applies to third-party numerical reference data.

## Working spectral range and resampling

The application currently performs its spectral calculations over **380-730 nm**. The default grid spacing is **5 nm** and can be changed at runtime with `--sampling-nm`.

Runtime resampling is linear:

- CIE observer data: source sampling 1 nm.
- CIE illuminant table: source sampling 5 nm.
- ColorChecker reflectances: source sampling 10 nm.
- Jiang camera sensitivities: source sampling 10 nm over 400-720 nm; sensitivity is treated as zero outside that measured range.

A finer computational grid does not create new measured spectral information in data that were originally sampled more coarsely.

## CIE standard-observer color-matching functions

Project file:

```text
data/cie_standard_observers_1nm.csv
```

The combined project file contains six color-matching-function columns at 1 nm spacing from 360-830 nm:

```text
x_bar_2deg, y_bar_2deg, z_bar_2deg
x_bar_10deg, y_bar_10deg, z_bar_10deg
```

These correspond to:

- the **CIE 1931 2-degree standard colorimetric observer**, and
- the **CIE 1964 10-degree standard colorimetric observer**.

The 1964 observer has historically also been called the *supplementary standard colorimetric observer*. Current ISO/CIE 11664-1 specifies both sets of color-matching functions for standard colorimetry.

The application uses only the 380-730 nm portion of these data because that is the current common scene-data range.

Authoritative CIE data sets:

### CIE 1931 2-degree observer

- Data set: `CIE_xyz_1931_2deg.csv`
- Sampling: 1 nm
- DOI: `10.25039/CIE.DS.xvudnb9b`
- https://cie.co.at/datatable/cie-1931-colour-matching-functions-2-degree-observer

### CIE 1964 10-degree observer

- Data set: `CIE_xyz_1964_10deg.csv`
- Sampling: 1 nm
- DOI: `10.25039/CIE.DS.sqksu2n5`
- https://cie.co.at/datatable/cie-1964-colour-matching-functions-10-degree-observer

Both observer sets are specified by ISO/CIE 11664-1:2019, *Colorimetry — Part 1: CIE standard colorimetric observers*.

CIE describes the 1931 functions as representative of normal-color-vision observers for visual fields of roughly 1-4 degrees, and the 1964 functions as representative of larger fields greater than about 4 degrees under appropriate photopic conditions.

Consult the CIE data pages for current copyright and redistribution terms before redistributing CIE numerical data.

## Standard illuminants

Project file:

```text
data/cie_illuminants_5nm.csv
```

The project table contains illuminants A, D50, D55, D65, D75, and C over 380-730 nm at 5 nm sampling.

Authoritative CIE data pages include:

- D65 — DOI `10.25039/CIE.DS.hjfjmt59`  
  https://www.cie.co.at/datatable/cie-standard-illuminant-d65
- A — DOI `10.25039/CIE.DS.8jsxjrsn`  
  https://cie.co.at/datatable/cie-standard-illuminant-1-nm
- D50 — DOI `10.25039/CIE.DS.etgmuqt5`  
  https://cie.co.at/datatable/cie-standard-illuminant-d50
- D55 — DOI `10.25039/CIE.DS.qewfb3kp`  
  https://cie.co.at/datatable/relative-spectral-power-distributions-cie-illuminant-d55
- D75 — DOI `10.25039/CIE.DS.9fvcmrk4`  
  https://cie.co.at/datatable/relative-spectral-power-distributions-cie-illuminant-d75
- C — DOI `10.25039/CIE.DS.mjdd2enu`  
  https://cie.co.at/datatable/relative-spectral-power-distributions-cie-illuminant-c

The application normalizes the unfiltered illuminant before applying ND transmission. In CIE mode the normalization uses the selected observer's `y_bar(lambda)`; in camera mode it uses the fixed CIE 1931 2-degree camera-reference observer. Consequently, the illuminant files are being used primarily for relative spectral shape rather than as absolute radiometric-power calibrations.

Consult the CIE data pages for current copyright and redistribution terms before redistributing CIE numerical data.

## ColorChecker spectral reflectance

Project file:

```text
data/colorchecker_babelcolor_avg30_10nm.csv
```

Source: BabelColor ColorChecker spectral data, `CC_Avg30_spectrum_CGATS.txt`, an average of 30 measured classic ColorChecker charts.

- Source sampling: 10 nm
- Source range: 380-730 nm
- Source page: https://babelcolor.com/colorchecker-2.htm

BabelColor notes that these data correspond to the classic ColorChecker formulation used before the production change in November 2014.

The project reorders the source samples into the familiar visual 6 x 4 row-major patch sequence and linearly resamples the spectra onto the selected computational grid.

Consult the BabelColor source page for current terms governing redistribution and reuse.

## Camera spectral sensitivities

Runtime file:

```text
camspec_database.txt
```

Primary scientific reference:

> Jun Jiang, Dengyu Liu, Jinwei Gu, and Sabine Süsstrunk, “What Is the Space of Spectral Sensitivity Functions for Digital Color Cameras?”, *2013 IEEE Workshop on Applications of Computer Vision (WACV)*, 2013. DOI: `10.1109/WACV.2013.6475015`.

Database references:

- Archived data record: https://doi.org/10.5281/zenodo.3245883
- Research/database page: https://www.gujinwei.org/research/camspec/db.html
- Original database file: https://www.gujinwei.org/research/camspec/camspec_database.txt

The database contains 28 measured cameras. For each camera, red, green, and blue spectral sensitivities are tabulated from **400-720 nm in 10 nm increments** (33 samples per channel). The published database normalizes each RGB channel separately to a maximum of 1.

The authors describe measurements using a Photo Research PR-655 spectroradiometer together with a monochromator/light-source and integrating-sphere arrangement.

The original database page states that the database is copyright Rochester Institute of Technology and licensed under **Creative Commons BY-NC-SA 4.0**. Verify the current source-page terms before redistributing the numerical database.

### CMake acquisition

If `data/camspec_database.txt` is already present, CMake copies it into the build data directory. Otherwise CMake tries, in order:

1. the Zenodo REST content endpoint, and
2. the original project-hosted database URL.

The downloaded file is verified against the published MD5 checksum:

```text
dcf7ce9ec9f2c87b87014f5cc46f8c15
```

The runtime copy is placed in:

```text
build/data/camspec_database.txt
```

## Camera color-correction model

The Jiang spectral sensitivities produce camera-raw RGB responses; they are not themselves display RGB values.

For each camera, the application fits a 3 x 3 raw-RGB-to-XYZ matrix directly from the measured spectral sensitivities. On the active wavelength grid it solves, in the least-squares sense,

```text
[s_R(lambda) s_G(lambda) s_B(lambda)] B
    ~= [x_bar(lambda) y_bar(lambda) z_bar(lambda)]
```

using the **CIE 1931 2-degree observer** CMFs and then stores `B^T` for column-vector use. This is a Luther-style best linear fit and does not use the ColorChecker patches or a particular illuminant as training data. Because the Jiang channels are independently normalized, their unknown constant relative gains are absorbed by the fitted 3 x 3 transform.

This 2-degree observer is therefore the fixed **camera colorimetric reference observer**. Changing the selected CIE observer in standard-observer mode does not refit or alter the camera matrices. Because measured cameras do not generally satisfy the Luther condition exactly, the resulting matrix is an approximation rather than an exact spectral equivalence.

Optional camera white balance is a diagonal raw-RGB scaling that maps the camera response of a perfect spectrally flat white reflector under the current illuminant to the same camera's perfect-white response under D65. It is a project modeling choice, not a manufacturer-specific auto-white-balance model. D65 is used for this white-balance reference, not for fitting the camera-to-XYZ matrix.

## Reference spectral display and RGB encodings

The project does not use a single fixed XYZ-to-RGB matrix for both CIE observers. Instead, it creates synthetic physical R, G, and B primary spectra and derives an observer-specific display matrix from those spectra.

### Conventional chromaticity targets

The target primary chromaticities are taken from the conventional RGB-space definitions.

#### sRGB

- R `(0.64, 0.33)`
- G `(0.30, 0.60)`
- B `(0.15, 0.06)`
- reference white D65
- standard piecewise sRGB transfer function

ICC registry:

https://registry.color.org/rgb-registry/srgb

#### Adobe RGB (1998)

- R `(0.64, 0.33)`
- G `(0.21, 0.71)`
- B `(0.15, 0.06)`
- reference white D65
- gamma `2.19921875` (`563/256`)

ICC registry:

https://registry.color.org/rgb-registry/adobergb

### Project-defined synthetic primary spectra

Chromaticity coordinates do not uniquely determine physical spectral primaries. The project therefore defines a reproducible synthetic display model.

Each R, G, and B primary is represented as a **nonnegative mixture of three Gaussian basis spectra with 20 nm FWHM**. The Gaussian center wavelengths are fixed in `src/ColorScience.cpp`. Mixture weights are solved so that each synthetic primary reproduces its target chromaticity under the **CIE 1931 2-degree observer** on the current computational grid.

These spectral primary shapes are a modeling choice made by this project. They are **not** measured spectra from an sRGB monitor, an Adobe RGB monitor, or any particular physical display.

After fitting the primary shapes, their relative physical powers are calibrated **once** under the CIE 1931 2-degree observer so equal linear R, G, and B drive reproduces the numerically integrated D65 white over the active project wavelength range. Those scaled primary spectra are then frozen.

For each supported CIE observer, the exact same frozen physical spectra are reintegrated with that observer's CMFs. No observer-dependent primary rescaling is performed. This yields observer-specific RGB-to-XYZ and XYZ-to-RGB matrices for one genuinely fixed assumed physical display. Equal RGB drive is therefore guaranteed to correspond to D65 in the 1931 2-degree reference characterization, but the 1964 10-degree observer may assign a different XYZ white to the same equal-drive physical emission.

In standard-observer mode, the display matrix follows the selected CIE observer. In camera mode, the display matrix is fixed to the CIE 1931 2-degree characterization because the camera color-correction matrices produce 1931-2-degree XYZ.

Linear RGB values are clipped to `[0, 1]` before the sRGB or Adobe RGB encoding curve is applied.

## Display and image-file interpretation

The terms **Reference sRGB spectral display** and **Reference Adobe RGB (1998) spectral display** refer to the project's synthetic spectral display models. They should not be interpreted as measured models of the physical monitor running the application.

OpenCV HighGUI is not an ICC-managed display pipeline, and the application does not currently embed an ICC profile when saving images with `cv::imwrite()`. The rendered files are therefore intended primarily as controlled simulation and classifier stimuli.

A future display-calibration extension could replace the synthetic primary spectra with measured red, green, and blue spectra from a particular monitor and derive its 2-degree and 10-degree matrices from those measurements.
