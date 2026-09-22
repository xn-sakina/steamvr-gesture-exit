# Brand icon assets

The icon was generated with ImageGen as a square raster master. Keep
`bailout-master.png` as the source. `bailout.png` is the 256 × 256 production
image. The Dashboard thumbnail decodes that PNG from the executable's RCDATA
resource; the Windows application icon uses `bailout.ico`.

Rebuild both production assets from the master with ImageMagick:

```sh
magick assets/brand/bailout-master.png -resize 256x256 -strip -define png:color-type=6 PNG32:assets/brand/bailout.png
magick assets/brand/bailout.png -define icon:auto-resize=256,128,96,64,48,32,24,16 assets/brand/bailout.ico
magick compare -metric AE assets/brand/bailout.png 'assets/brand/bailout.ico[0]' null:
```

The final command must print `0 (0)`: the ICO's 256 × 256 layer is then
pixel-identical to the production PNG. The other seven layers are scaled from
the same PNG. Windows builds use these checked-in assets directly and do not
require ImageMagick.

## ImageGen prompt

```text
Use case: logo-brand
Asset type: production Windows desktop app icon and SteamVR dashboard thumbnail source, square 1:1 raster master
Primary request: Design a polished, memorable icon for an app that lets a VR player safely leave a game after confirming. Make the core symbol a single bold doorway or portal with a clear outward motion cue, conveying a calm, deliberate exit. This is a completely new design, not a revision of the existing C-shaped arrow icon.
Style/medium: premium flat vector-like iconography, precise clean geometry, crisp anti-aliased edges, no 3D rendering, no illustration textures, no shadows, no glassmorphism.
Composition/framing: centered single emblem with generous safe margins; readable when scaled to 16x16. Square app tile with gently rounded corners; genuinely transparent pixels only outside the tile. Large, simple shapes; visually balanced silhouette.
Color palette: deep midnight blue tile, restrained electric cyan and soft white emblem. High contrast without neon glow.
Constraints: no letters, no words, no numbers, no watermark, no tiny details, no extra symbols, no duplicate icons, no mockup or device frame. Do not include the Steam or SteamVR logos. Output one finished icon only, straight-on view, 1024x1024 square.
```
