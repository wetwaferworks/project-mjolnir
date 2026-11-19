# Project MJOLNIR v5 — GitHub art pack

Approved by Jaydee Wetwork, November 18, 2026.

**Project MJOLNIR** — *The SI researcher that dreams.*

Backronym (by Jaydee Wetwork): **Measured Journaled Operational Learning Neural Intelligent Researcher**

Art by CloudAtlas, Wet Wafer Works. Rendered from the approved v5 SVGs with `resvg` (do not use cairosvg: it collapses the `Project MJOLNIR` tspan and drops the middle-dot glyph). PNGs are sRGB with 300 DPI metadata, losslessly optimized.

## Suggested repo layout

Place this pack at `docs/art/` in [wetwaferworks/project-mjolnir](https://github.com/wetwaferworks/project-mjolnir), keeping the subfolders:

```
docs/art/
  contact-sheet.png
  social-preview/
  readme/
  avatar/
  social/
  favicon/
  svg/
```

## README.md snippet

```html
<p align="center">
  <img src="docs/art/readme/mjolnir-header-1600x400.png"
       alt="Project MJOLNIR — Measured Journaled Operational Learning Neural Intelligent Researcher"
       width="1600">
</p>
```

Optional badge (transparent):

```html
<p align="center">
  <img src="docs/art/readme/mjolnir-badge-512.png"
       alt="Project MJOLNIR badge"
       width="256">
</p>
```

High-DPI displays can use the `@2x` / larger companions (`mjolnir-header-3200x800.png`, `mjolnir-badge-1024.png`, and the `2560×1280` social preview).

## Where each file goes on GitHub

### Social preview — `social-preview/`

GitHub repository unfurl image (Open Graph). **Settings › General › Social preview**. GitHub recommends **1280×640** and under 1 MB. Use `mjolnir-social-preview-1280x640.png`. The `2560×1280` file is the `@2x` master; do not upload it as the official social preview (it is over the recommended size).

| File | Pixels | Size | Notes |
|---|---:|---:|---|
| `social-preview/mjolnir-social-preview-1280x640.png` | 1280×640 | 298.0 KB | RGB · 300 DPI |
| `social-preview/mjolnir-social-preview-2560x1280.png` | 2560×1280 | 739.5 KB | RGB · 300 DPI |

### README header and badges — `readme/`

Drop into the repository README as shown above. Badge-only PNGs have transparent corners (outside the ring) for use on light or dark README themes, and as a repo / profile badge.

| File | Pixels | Size | Notes |
|---|---:|---:|---|
| `readme/mjolnir-badge-1024.png` | 1024×1024 | 546.3 KB | RGBA · 300 DPI |
| `readme/mjolnir-badge-2048.png` | 2048×2048 | 1.41 MB | RGBA · 300 DPI |
| `readme/mjolnir-badge-256.png` | 256×256 | 96.0 KB | RGBA · 300 DPI |
| `readme/mjolnir-badge-512.png` | 512×512 | 191.1 KB | RGBA · 300 DPI |
| `readme/mjolnir-header-1600x400.png` | 1600×400 | 219.8 KB | RGB · 300 DPI |
| `readme/mjolnir-header-3200x800.png` | 3200×800 | 547.5 KB | RGB · 300 DPI |

### Organization / repo avatar — `avatar/`

Square mark on wafer background `#090724`, badge centered with a 10% safe margin so a circular crop (GitHub org and profile avatars) does not clip the ring.

- **Settings › [Organization] Profile › Profile picture**, or the user profile photo, using `mjolnir-avatar-1024.png` (or `mjolnir-avatar-500.png` if a smaller file is required).

| File | Pixels | Size | Notes |
|---|---:|---:|---|
| `avatar/mjolnir-avatar-1024.png` | 1024×1024 | 504.3 KB | RGB · 300 DPI |
| `avatar/mjolnir-avatar-500.png` | 500×500 | 177.5 KB | RGB · 300 DPI |

### Social images — `social/`

Banners and squares for docs, a GitHub Pages site, or to post on social media. Each 1× size has a 2× companion.

| File | Pixels | Size | Notes |
|---|---:|---:|---|
| `social/mjolnir-social-header-1500x500.png` | 1500×500 | 330.3 KB | RGB · 300 DPI |
| `social/mjolnir-social-header-3000x1000.png` | 3000×1000 | 858.3 KB | RGB · 300 DPI |
| `social/mjolnir-social-pair-1920x1080.png` | 1920×1080 | 799.3 KB | RGB · 300 DPI |
| `social/mjolnir-social-pair-3840x2160.png` | 3840×2160 | 2.07 MB | RGB · 300 DPI |
| `social/mjolnir-social-square-1080.png` | 1080×1080 | 510.5 KB | RGB · 300 DPI |
| `social/mjolnir-social-square-2160.png` | 2160×2160 | 1.26 MB | RGB · 300 DPI |
| `social/pchie-social-square-1080.png` | 1080×1080 | 516.6 KB | RGB · 300 DPI |
| `social/pchie-social-square-2160.png` | 2160×2160 | 1.33 MB | RGB · 300 DPI |

### Favicon — `favicon/`

For GitHub Pages or the project site. `favicon.ico` is a multi-size icon (16, 32, 48, 64). `apple-touch-icon-180.png` is the 180×180 touch icon. All are the badge with transparent corners.

| File | Pixels | Size | Notes |
|---|---:|---:|---|
| `favicon/apple-touch-icon-180.png` | 180×180 | 56.6 KB | RGBA · 300 DPI |
| `favicon/favicon-32.png` | 32×32 | 5.4 KB | RGBA · 300 DPI |
| `favicon/favicon-64.png` | 64×64 | 11.8 KB | RGBA · 300 DPI |
| `favicon/favicon.ico` | multi-size ICO (16, 32, 48, 64) | 19.4 KB |  |

### Source SVGs — `svg/`

Approved v5 masters, plus `pchie-harness-B.svg` (the approved Bolt PCHIE badge, byte-identical at 1024 to the embeds in the social SVGs). Rasters are rendered with `resvg` using the Orbitron and Rajdhani Google fonts. Do not use cairosvg: it collapses the `Project MJOLNIR` tspan and drops the middle-dot glyph.

| File | Pixels | Size | Notes |
|---|---:|---:|---|
| `svg/github-readme-header-1600x400.svg` | SVG source | 745.5 KB |  |
| `svg/github-social-preview-1280x640.svg` | SVG source | 745.5 KB |  |
| `svg/mjolnir-harness.svg` | SVG source | 14.1 KB |  |
| `svg/pchie-harness-B.svg` | SVG source | 9.5 KB |  |
| `svg/social-header-1500x500.svg` | SVG source | 1.54 MB |  |
| `svg/social-pair-1920x1080.svg` | SVG source | 1.54 MB |  |
| `svg/social-square-mjolnir-1080.svg` | SVG source | 745.7 KB |  |
| `svg/social-square-pchie-1080.svg` | SVG source | 827.2 KB |  |

### Contact sheet

| File | Pixels | Size | Notes |
|---|---:|---:|---|
| `contact-sheet.png` | 2272×4953 | 4.19 MB | RGB · 300 DPI |

## Credits

- Art: CloudAtlas
- MJOLNIR backronym, “Measured Journaled Operational Learning Neural Intelligent Researcher”: Jaydee Wetwork
- Slogan: *The SI researcher that dreams.*
- Center-peach B badge is unchanged from the approved v3 set
