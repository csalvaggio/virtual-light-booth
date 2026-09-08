# Spectral Data Sources and Provenance

The source code and numerical data have different provenance. Do not assume the source-code license applies to third-party reference data.

## CIE 1931 2-degree color-matching functions

Project file: `data/cie_1931_2deg_5nm.csv`

The values correspond to the CIE 1931 2-degree standard colorimetric observer over 380-730 nm. The CIE publishes the authoritative 1 nm data set as:

- CIE 2019, *Colour-matching functions of CIE 1931 standard colorimetric observer*.
- DOI: 10.25039/CIE.DS.xvudnb9b
- Data set name: `CIE_xyz_1931_2deg.csv`
- CIE page: https://cie.co.at/datatable/cie-1931-colour-matching-functions-2-degree-observer

## Standard illuminants

Project file: `data/cie_illuminants_5nm.csv`

The project contains A, D50, D55, D65, D75, and C over 380-730 nm on a 5 nm table. Authoritative CIE data pages include:

- D65: https://www.cie.co.at/datatable/cie-standard-illuminant-d65 — DOI 10.25039/CIE.DS.hjfjmt59
- A: https://cie.co.at/datatable/cie-standard-illuminant-1-nm — DOI 10.25039/CIE.DS.8jsxjrsn
- D50: https://cie.co.at/datatable/cie-standard-illuminant-d50 — DOI 10.25039/CIE.DS.etgmuqt5
- D55: https://cie.co.at/datatable/relative-spectral-power-distributions-cie-illuminant-d55 — DOI 10.25039/CIE.DS.qewfb3kp
- D75: https://cie.co.at/datatable/relative-spectral-power-distributions-cie-illuminant-d75 — DOI 10.25039/CIE.DS.9fvcmrk4
- C: https://cie.co.at/datatable/relative-spectral-power-distributions-cie-illuminant-c — DOI 10.25039/CIE.DS.mjdd2enu

Consult the CIE data pages for their current copyright/licensing terms before redistributing CIE numerical data.

## ColorChecker spectral reflectance

Project file: `data/colorchecker_babelcolor_avg30_10nm.csv`

Source: BabelColor ColorChecker spectral data, `CC_Avg30_spectrum_CGATS.txt`, an average of 30 measured classic ColorChecker charts. The source data cover 380-730 nm in 10 nm increments. BabelColor notes that these data correspond to the formulation used before the ColorChecker production change in November 2014.

Source page:
https://babelcolor.com/colorchecker-2.htm

The project reorders the CGATS samples into the familiar visual 6 x 4 row-major sequence and linearly resamples them at runtime when the computational grid is finer than 10 nm.

## Camera spectral sensitivities

Runtime file: `camspec_database.txt`

Source: Jun Jiang, Dengyu Liu, Jinwei Gu, and Sabine Süsstrunk, *What is the Space of Spectral Sensitivity Functions for Digital Color Cameras?*, IEEE Workshop on Applications of Computer Vision, 2013.

- Paper DOI: 10.1109/WACV.2013.6475015
- Dataset DOI: https://doi.org/10.5281/zenodo.3245883
- Research page: https://www.gujinwei.org/research/camspec/
- Original database URL: https://www.gujinwei.org/research/camspec/camspec_database.txt

The database contains 28 measured cameras. For every camera, the red, green, and blue spectral sensitivities are tabulated from 400-720 nm in 10 nm increments (33 samples per channel), with each channel normalized to a maximum of 1.

By default CMake downloads the archived Zenodo copy into `build/data/camspec_database.txt` and verifies the file using the published MD5 checksum:

```text
dcf7ce9ec9f2c87b87014f5cc46f8c15
```

If `data/camspec_database.txt` is already present, it is copied into the build data directory instead and no database download is required.

Consult the source/archival record for the current terms governing redistribution and reuse of the numerical database.

## Color encodings

The sRGB output follows IEC 61966-2-1 behavior (D65 white and the standard piecewise transfer function). Adobe RGB (1998) uses D65 and gamma 2.19921875. See the ICC Three Component Color Encoding Registry:

- sRGB: https://registry.color.org/rgb-registry/srgb
- Adobe RGB (1998): https://registry.color.org/rgb-registry/adobergb
