# Brand icon assets

`origin.png` is the canonical square raster source. `bailout-master.png` is a
byte-for-byte synchronized copy kept for compatibility with the existing asset
layout. `bailout.png` is the 256 × 256 production image. The packaged Dashboard
thumbnail loads that PNG through OpenVR's file API; a direct EXE launch can
still decode the executable's RCDATA copy. The Windows application icon uses
`bailout.ico`.
The Release package also includes `bailout.png` because the SteamVR application
manifest refers to it with `image_path`; the EXE resource alone is not visible
to SteamVR's application list.

Rebuild both production assets from the master with ImageMagick:

```sh
cp assets/brand/origin.png assets/brand/bailout-master.png
magick assets/brand/origin.png -filter Lanczos -resize 256x256 -strip -colorspace sRGB -define png:color-type=6 PNG32:assets/brand/bailout.png
magick assets/brand/bailout.png -filter Lanczos -define icon:auto-resize=256,128,96,64,48,40,32,24,20,16 assets/brand/bailout.ico
magick compare -metric AE assets/brand/bailout.png 'assets/brand/bailout.ico[0]' null:
```

The final command must print `0 (0)`: the ICO's 256 × 256 layer is then
pixel-identical to the production PNG. The other nine layers are scaled from
the same PNG. The 20 × 20 and 40 × 40 layers provide exact matches for common
Windows high-DPI title bar, context menu, and system tray sizes. Windows builds
use these checked-in assets directly and do not require ImageMagick.
