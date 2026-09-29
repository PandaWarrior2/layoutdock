# Application icon

`layoutdock.svg` is the editable source: the existing blue **LD** identity as a rounded keyboard key, with a small dock indicator. The letters are outlines, so rendering does not depend on fonts.

- `layoutdock.ico`: transparent 32-bit icon with 16, 20, 24, 32, 40, 48, 64, 128 and 256 px frames.
- `layoutdock.png`: 256 px preview.

The ICO is embedded in the executable and displayed in the settings header. The tray continues to show the active keyboard layout and uses the application icon when no layout is available.

Normal application builds need no image tools. To regenerate the ICO and PNG after editing the SVG, use Node.js and `sharp` from the project root:

```text
npm install --no-save --no-package-lock sharp
node tools/generate-icon.cjs
make
```
