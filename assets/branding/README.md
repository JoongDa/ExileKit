# ExileKit application icon

`exilekit-source.png` is the artwork supplied by the project owner for the application icon.
The current artwork is the simple gold three-part ring supplied on 2026-09-16.
The original image is retained unchanged. No additional license is asserted for this artwork.

Run `scripts/build-icon.ps1` to generate `apps/toolbox/exilekit.ico` with 16, 20, 24, 28, 32, 40, 48, 56, 64, 96, 128 and 256 pixel RGBA PNG frames.
The original RGB image has an opaque near-black background. The conversion removes that matte (also inside the ring), feathering alpha between channel maxima of 16 and 32 before resizing. Gold RGB values, shape and padding are preserved; the source PNG is never modified. An RGBA source retains its existing alpha instead.
Each size is resampled directly from the source, including native small/large sizes for 100%, 125%, 150%, 175% and 200% Windows scaling.
The ICO is embedded in the executable; the full-size source image is not copied into the runtime package.
