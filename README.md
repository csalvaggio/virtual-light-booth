# Virtual Light Booth

A C++/OpenCV application that renders the classic 24-patch ColorChecker from spectral data. The project models

```text
illuminant spectral power distribution ->
neutral-density transmission ->
surface reflectance ->
spectral energy leaving the sample ->
standard-observer or camera response ->
observer-consistent reference display RGB
```

The displayed scene is a 6 x 4 ColorChecker. The checker surround is modeled as a spectrally flat 3% neutral reflector and the outer background as a spectrally flat 18% gray reflector. Both therefore respond to the selected illuminant, ND transmission, response model, and output display model.

## Data included and acquired

- CIE 1931 2-degree and CIE 1964 10-degree color-matching functions in a combined project file sampled at 1 nm from 360-830 nm.
- CIE illuminants A, D50, D55, D65, D75, and C, tabulated at 5 nm from 380-730 nm.
- BabelColor average-of-30 classic ColorChecker reflectance measurements (pre-November-2014 formulation), measured at 10 nm from 380-730 nm.
- The complete Jiang et al. (2013) camera spectral-sensitivity database: 28 measured RGB camera responses from 400-720 nm at 10 nm sampling. CMake obtains the original `camspec_database.txt` if it is not already present in `data/`.

See `DATA_SOURCES.md` for provenance, literature references, licensing notes, and the exact source URLs.

## Computational spectral grid

The default computational wavelength grid is **380-730 nm at 5 nm spacing**.

```bash
./build/bin/virtual_light_booth --sampling-nm 1
```

selects a 1 nm working grid. This does not create new measured information in coarser source data:

- the observer CMFs are natively sampled at 1 nm;
- the illuminants are linearly interpolated from 5 nm samples;
- the ColorChecker reflectances are linearly interpolated from 10 nm samples;
- the Jiang camera sensitivities are linearly interpolated from 10 nm samples and are zero outside their measured 400-720 nm range.

All colorimetric and camera integrations are performed on the selected working grid.

## Spectral scene model

For wavelength `lambda`, surface reflectance `R(lambda)`, illuminant SPD `E(lambda)`, and neutral-density transmission `T`, the spectrum leaving a surface is proportional to

```text
L(lambda) = k E(lambda) T R(lambda)
```

The normalization factor `k` is computed from the **unfiltered** illuminant. The ND transmission is applied afterward, so reducing ND transmission genuinely reduces the rendered signal rather than being canceled by a subsequent illuminant renormalization.

The normalization convention depends on the active response path:

- **CIE mode:** `k` is computed with the currently selected observer's `y_bar(lambda)` so that a perfect reflecting diffuser has `Y = 1` at `T = 1` for that observer.
- **Camera mode:** `k` is computed with the fixed CIE 1931 2-degree camera-reference observer. This keeps the camera pipeline on one consistent photometric/colorimetric reference while cameras are changed.

Thus, illuminant selection primarily changes spectral shape while the ND control provides an independent level/exposure change.

## CIE standard-observer path

The CIE path supports two standard colorimetric observers:

- **CIE 1931 2-degree observer**
- **CIE 1964 10-degree observer** — the application default

The CIE 1931 functions represent small visual fields (approximately 1-4 degrees), while the CIE 1964 functions represent larger fields. The selected observer's `x_bar`, `y_bar`, and `z_bar` functions are used to integrate the reflected spectrum to XYZ.

In CIE mode, `m / M` cycles between the observers. Observer selection changes both:

1. the scene spectrum-to-XYZ integration, and
2. the XYZ-to-RGB characterization of the synthetic reference display.

The app can optionally apply Bradford chromatic adaptation from the selected illuminant white to D65. When a different observer is selected, the source and D65 white tristimulus values are recomputed with that observer before the Bradford transform is applied. Bradford adaptation is **off by default**, which is the preferred setting when the purpose is to preserve illuminant-induced differences for classifier demonstrations.

Observer selection is confined to the CIE rendering path. It does not change the selected camera or refit the camera models.

## Camera path

The reflected spectrum can instead be integrated against any of the 28 measured camera spectral sensitivities from Jiang et al. (2013). These three integrals are camera-raw RGB responses, not display RGB values.

At startup, the program fits a separate 3 x 3 raw-camera-RGB-to-XYZ matrix for every camera by least squares using all 24 ColorChecker patches under D65. The XYZ targets for these fits are always computed with the **CIE 1931 2-degree observer**.

This fixed 2-degree reference is intentional:

- the camera spectral sensitivities themselves are physical sensor responses and do not depend on a CIE observer;
- only the camera RGB-to-XYZ color-correction fit needs a colorimetric reference observer;
- changing the selected observer in CIE mode therefore has no hidden effect on camera mode.

Optional diagonal camera white balance maps the camera response of a perfect spectrally flat white reflector under the current illuminant to that same camera's perfect-white response under D65. It is a simple pedagogical white-balance model, not a simulation of a manufacturer's automatic white-balance algorithm.

For classifier demonstrations intended to preserve illuminant differences, camera white balance should normally remain **off**.

The fitted camera matrices are pedagogical approximations; they are not manufacturer profiles and do not reproduce complete in-camera processing pipelines.

The default camera is **Canon 5DMarkII**.

## Spectral reference display model

The application does **not** apply one conventional 1931-2-degree XYZ-to-RGB matrix to both CIE observers. Instead, it constructs a reproducible synthetic physical display model and derives a display matrix separately for each observer.

### Reference primary chromaticities

For the reference sRGB display, the target CIE 1931 2-degree primary chromaticities are

```text
R = (0.64, 0.33)
G = (0.30, 0.60)
B = (0.15, 0.06)
```

For the reference Adobe RGB (1998) display, red and blue are the same and green is

```text
G = (0.21, 0.71)
```

These are the conventional primary chromaticities of the corresponding RGB encodings. They do **not** uniquely specify physical display spectra.

### Synthetic primary spectra

To create a physical spectral model, each primary is represented by a nonnegative mixture of three smooth Gaussian basis spectra with 20 nm FWHM. The basis wavelengths are fixed by the project, and the mixture weights are solved at startup so that the resulting primary reproduces the target chromaticity under the **CIE 1931 2-degree observer** on the current computational wavelength grid.

The resulting R, G, and B spectra are then frozen. They are project-defined synthetic spectra, not measurements of a particular monitor.

### Observer-specific display matrices

For each supported observer, the same frozen primary spectra are reintegrated with that observer's color-matching functions. The three primary scale factors are then solved so that equal RGB values reproduce that observer's numerically integrated D65 white over the project's working spectral range.

This produces an observer-specific

```text
RGB -> XYZ
```

matrix and its inverse

```text
XYZ -> linear RGB
```

for the **same assumed physical display**.

Consequently, switching from the 2-degree to the 10-degree observer in CIE mode changes both the scene colorimetry and the display colorimetry while leaving the physical scene and assumed display spectra unchanged.

In camera mode, the raw-to-XYZ camera matrices are defined in CIE 1931 2-degree XYZ, so camera output is always rendered using the 2-degree characterization of the selected reference display. This avoids mixing incompatible XYZ definitions.

The reference sRGB output uses the standard sRGB transfer function. The reference Adobe RGB (1998) output uses gamma `563/256` (`2.19921875`). Linear RGB values outside `[0, 1]` are clipped before encoding, so highly out-of-gamut colors can be channel-clipped.

## Reading the printed state

A typical state block is

```text
Illuminant: D65
ND transmission: 1.00
Response: CIE 1964 10-degree observer
Output: Reference sRGB spectral display
Bradford adaptation: OFF
Grid spacing: 5.00 nm
```

The fields mean:

- **Illuminant** — the SPD illuminating the simulated scene.
- **ND transmission** — the spectrally flat multiplicative transmission applied after illuminant normalization.
- **Response** — the response model actually used to render the current image: either the selected CIE observer or the selected Jiang camera.
- **Output** — the synthetic reference display family used for the final XYZ-to-RGB conversion and encoding.
- **Bradford adaptation / Camera WB** — the mode-specific compensation currently active.
- **Grid spacing** — the computational wavelength sampling interval, not necessarily the native sampling of every source data set.

In CIE mode, the output display matrix follows the selected observer. In camera mode, the display matrix is fixed to the CIE 1931 2-degree characterization because the camera RGB-to-XYZ matrices use that reference.

When `m / M` is pressed, the program also prints a one-line `Selected observer:` or `Selected camera:` acknowledgement before the next full state block. That acknowledgement reports the newly chosen response model; the following full state block describes the image rendered with that new selection.

## Jiang camera models

The database contains:

```text
Canon 1DMarkIII
Canon 20D
Canon 300D
Canon 40D
Canon 500D
Canon 50D
Canon 5DMarkII
Canon 600D
Canon 60D
Hasselblad H2
Nikon D3X
Nikon D200
Nikon D3
Nikon D300s
Nikon D40
Nikon D50
Nikon D5100
Nikon D700
Nikon D80
Nikon D90
Nokia N900
Olympus E-PL2
Pentax K-5
Pentax Q
Point Grey Grasshopper 50S5C
Point Grey Grasshopper2 14S5C
Phase One
SONY NEX-5N
```

## Build

Requirements:

- CMake 3.20 or newer
- A C++20 compiler
- OpenCV with `core`, `highgui`, `imgcodecs`, and `imgproc`
- Internet access during the first configure so CMake can fetch `portable-file-dialogs` and, unless already supplied in `data/`, the Jiang camera database

```bash
cmake -S . -B build
cmake --build build
./build/bin/virtual_light_booth
```

If you prefer not to let CMake download the camera database, place the original `camspec_database.txt` in the project's `data/` directory before configuring.

If the executable cannot locate its complete runtime data directory, specify one explicitly:

```bash
./build/bin/virtual_light_booth --data-dir /path/to/data
```

or set the `VLB_DATA_DIR` environment variable.

## Controls

```text
i / I     next / previous illuminant
[ / ]     decrease / increase ND transmission by 0.05
c         toggle CIE standard-observer / camera spectral response
m / M     next / previous response model
          (standard observer in CIE mode; Jiang camera in camera mode)
o         toggle reference sRGB / Adobe RGB (1998) spectral display
a         toggle Bradford adaptation (CIE mode only)
w         toggle camera white balance (camera mode only)
Esc       reset to D65, ND=1, 10-degree observer, default camera, sRGB
s         save current rendered image using a file-save dialog
?         display controls
q         quit
```

Mode-specific keys do not alter hidden state in the inactive mode. Bradford adaptation is available only in CIE mode, and camera white balance is available only in camera mode.

When you press `s`, the program opens a portable save-file dialog so you can choose the directory and filename. The suggested filename records the illuminant, ND transmission, active observer or camera, adaptation/white-balance state, and output display family. PNG, JPEG, TIFF, and BMP are supported.

## Display and saved-file caveats

The encoded pixels describe the application's **synthetic reference spectral display**, not the actual monitor on which `cv::imshow()` happens to run. An ordinary monitor will generally have different physical primary spectra.

OpenCV HighGUI is not an ICC-managed display path, and images written by `cv::imwrite()` are not being supplied with an ICC profile by this application. The saved images are therefore best interpreted as controlled simulation/classifier stimuli in the project's reference RGB encoding, not as a guarantee of colorimetric appearance on arbitrary external displays.

A future measured-display option could replace the synthetic reference primaries with spectroradiometrically measured R, G, and B primary spectra from a specific monitor.

## Replacing data

The spectral math is separated from the data input. Your own ColorChecker measurements, additional illuminants, standard-observer functions, camera-response databases, or measured display-primary spectra can be substituted without changing the overall rendering architecture. See `DATA_SOURCES.md` for provenance and expected sources.

## License

This project is licensed under the GNU General Public License v3.0. See `LICENSE` for details. Third-party numerical data may have different licensing terms; see `DATA_SOURCES.md`.

## Contact

### Author

Carl Salvaggio, Ph.D.  
Professor of Imaging Science  
Director, Digital Imaging and Remote Sensing (DIRS) Laboratory

### E-mail

carl.salvaggio@rit.edu

### Organization

Chester F. Carlson Center for Imaging Science  
Rochester Institute of Technology  
Rochester, New York, 14623  
United States
