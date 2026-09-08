# Virtual Light Booth

A C++/OpenCV application that renders the classic 24-patch ColorChecker from spectral data. The project models

```
illuminant source power distribution ->
neutral-density transmission ->
surface reflectance ->
spectral energy leaving the sample ->
observer/camera response ->
display RGB
```

The displayed scene is a rendering of a 6 x 4 ColorChecker. The checker surround is modeled as a spectrally flat 3% neutral reflector and the outer background as a spectrally flat 18% gray reflector. Both therefore respond to the selected illuminant, ND transmission, response model, and output color space.

## Data included and acquired

- CIE 1931 2-degree color-matching functions, tabulated in the project at 5 nm from 380-730 nm.
- CIE illuminants A, D50, D55, D65, D75, and C, tabulated at 5 nm from 380-730 nm.
- BabelColor average-of-30 classic ColorChecker reflectance measurements (pre-November-2014 formulation), measured at 10 nm from 380-730 nm and linearly resampled onto the computational grid.
- The complete Jiang et al. (2013) camera spectral-sensitivity database: 28 measured RGB camera responses from 400-720 nm at 10 nm sampling. CMake downloads the original `camspec_database.txt` from the archived Zenodo record on first configure unless you have already placed the file in `data/`.

The default computational grid is 5 nm. You may select another interval with `--sampling-nm`, for example 2 nm. Interpolation to a finer computational grid does **not** create new spectral information beyond the sampling density of the source measurements.

## Spectral model

For wavelength `lambda`, patch reflectance `R(lambda)`, illuminant spectral power distribution `E(lambda)`, and neutral-density transmission `T`, the spectrum leaving the patch is proportional to

```text
L(lambda) = k E(lambda) T R(lambda)
```

where `k` normalizes the **unfiltered** illuminant so that a perfect reflecting diffuser has CIE Y = 1 at `T = 1`. Applying the ND transmission after that normalization is important: otherwise illuminant renormalization would cancel the intended dimming.

### CIE observer path

The reflected spectrum is integrated with the CIE 1931 2-degree color-matching functions to obtain XYZ. The app can optionally Bradford-adapt the selected illuminant white to D65. Adaptation is off by default so changing the illuminant visibly changes the rendered colors.

### Camera path

The reflected spectrum can instead be integrated against any of the 28 measured camera spectral sensitivities from Jiang et al (2013). Those three values are camera-raw responses, not display RGB. At startup the program therefore fits a separate 3 x 3 camera-raw-to-XYZ color-correction matrix for every camera by least squares using the 24 ColorChecker patches under D65.

Optional diagonal camera white balance can be enabled independently. It maps the response of a perfect spectrally flat white reflector under the current illuminant to that camera's response to the same reflector under D65.

These fitted matrices are pedagogical approximations; they are not manufacturer profiles and do not reproduce the complete in-camera imaging pipelines.

The default camera is the **Canon 5DMarkII**.

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

Optional spectral sampling interval:

```bash
./build/bin/virtual_light_booth --sampling-nm 2
```

If you prefer not to let CMake download the camera database, download the original Jiang `camspec_database.txt` and place it in the project's `data/` directory before configuring.

If the executable cannot locate its complete runtime data directory, specify one explicitly:

```bash
./build/bin/virtual_light_booth --data-dir /path/to/data
```

or set the `VLB_DATA_DIR` environment variable.

## Controls

```text
i / I     next / previous illuminant
[ / ]     decrease / increase ND transmission by 0.05
+ / -     aliases for ND increase / decrease
c         toggle CIE observer / camera spectral response
m / M     next / previous Jiang camera model (camera mode only)
o         toggle sRGB / Adobe RGB (1998) encoding
a         toggle Bradford adaptation (CIE mode)
w         toggle camera white balance (camera mode)
Esc       reset to D65, ND=1, CIE observer (default camera), sRGB
s         save current rendered image using a file-save dialog
?         display controls
q         quit
```

Mode-specific controls are also enforced strictly: camera-selection and camera white-balance keys are ignored unless the camera-response path is active, and the Bradford-adaptation key is ignored unless the CIE standard-observer path is active.

When you press `s`, the program opens a file dialog so you can choose both the directory and filename for the rendered image. The suggested filename encodes the current illuminant, ND transmission, response path, selected camera when applicable, white-balance/adaptation state, and output color space. PNG, JPEG, TIFF, and BMP are supported.

## Important display note

The sRGB mode is the appropriate default for `cv::imshow()` on an ordinary desktop. Adobe RGB (1998) mode computes Adobe RGB code values, but OpenCV HighGUI does not provide an ICC-managed display pipeline. Consequently, the Adobe RGB image shown by `imshow()` is not guaranteed to be visually color-correct on a normal display.

## Replacing data

The spectral math is separated from the data input. Your own ColorChecker measurements, additional illuminants, or other camera-response databases can be substituted without changing the renderer architecture. See `DATA_SOURCES.md` for provenance and expected sources.

## License

This project is licensed under the GNU General Public License v3.0. See `LICENSE` for details.

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
