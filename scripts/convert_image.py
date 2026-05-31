#!/usr/bin/env python3
"""Convert images to the fixed six-color Spectra 6 palette for e-paper display."""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

DISPLAY_WIDTH = 1200
DISPLAY_HEIGHT = 1600
DEFAULT_OUTPUT_DIR = "configs/images"

PALETTE = (
    ("black", (0, 0, 0)),
    ("white", (255, 255, 255)),
    ("yellow", (255, 255, 0)),
    ("red", (255, 0, 0)),
    ("blue", (0, 0, 255)),
    ("green", (0, 255, 0)),
)

PALETTE_NAMES = tuple(name for name, _ in PALETTE)
PALETTE_COLORS = tuple(color for _, color in PALETTE)

# Pre-computed BT.709 luma for each palette entry.
PALETTE_LUMA = tuple(0.2126 * r + 0.7152 * g + 0.0722 * b for r, g, b in PALETTE_COLORS)
# PNG palettes must contain 256 RGB entries (256 * 3 bytes), so the unused tail
# is zero-padded after the six panel colors.
PNG_PALETTE_BYTES = bytes(
    [component for color in PALETTE_COLORS for component in color]
    + [0] * (768 - len(PALETTE_COLORS) * 3)
)

# ---------------------------------------------------------------------------
# Dataclasses
# ---------------------------------------------------------------------------


@dataclass(slots=True)
class ConversionOptions:
    """All settings needed to convert a single image."""

    input_path: Path
    output_path: Path
    preset: str = "custom"
    width: int = DISPLAY_WIDTH
    height: int = DISPLAY_HEIGHT
    fit: str = "cover"
    dither: str = "atkinson"
    brightness: float = 1.0
    contrast: float = 1.0
    saturation: float = 1.0
    sharpness: float = 1.0
    rotate: int = 0
    background: str = "white"
    edge_enhance: bool = False
    smooth: bool = False
    filter_sharpen: bool = False


@dataclass(frozen=True, slots=True)
class Preset:
    """Named bundle of settings for a common conversion scenario."""

    name: str
    description: str
    values: dict[str, object]


# ---------------------------------------------------------------------------
# Preset definitions
# ---------------------------------------------------------------------------

PRESETS = (
    Preset(
        name="default",
        description="Best starting point for most images with a little extra color and contrast for the display.",
        values={
            "brightness": 1.03,
            "contrast": 1.10,
            "saturation": 1.08,
            "sharpness": 0.98,
        },
    ),
    Preset(
        name="vivid",
        description="Extra saturation and contrast when you want the image to pop.",
        values={
            "brightness": 1.03,
            "contrast": 1.25,
            "saturation": 1.31,
            "sharpness": 1.04,
        },
    ),
    Preset(
        name="graphics",
        description="Clean conversion for posters, icons, illustrations, and UI graphics.",
        values={
            "dither": "floyd-steinberg",
            "contrast": 1.20,
            "saturation": 1.14,
            "sharpness": 1.16,
            "edge_enhance": True,
        },
    ),
    Preset(
        name="accurate",
        description="Most faithful conversion for the best color accuracy and clear detail reproduction.",
        values={
            "brightness": 0.99,
            "contrast": 1.28,
            "saturation": 0.65,
            "sharpness": 0.96,
            "dither": "atkinson",
        },
    ),
)

PRESETS_BY_NAME = {preset.name: preset for preset in PRESETS}


def preset_names() -> tuple[str, ...]:
    """Return preset names in display order."""
    return tuple(PRESETS_BY_NAME)


DEFAULT_VALUES: dict[str, object] = {
    "width": DISPLAY_WIDTH,
    "height": DISPLAY_HEIGHT,
    "fit": "cover",
    "dither": "atkinson",
    "brightness": 1.0,
    "contrast": 1.0,
    "saturation": 1.0,
    "sharpness": 1.0,
    "rotate": 0,
    "background": "white",
    "edge_enhance": False,
    "smooth": False,
    "filter_sharpen": False,
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------


def parse_args(argv: list[str]) -> argparse.Namespace:
    """Parse CLI arguments."""
    parser = argparse.ArgumentParser(
        description=(
            "Convert any image into the exact six-color palette used by the "
            "epaper_spectra6_133 ESPHome display component.\n\n"
            "Palette: black, white, yellow, red, blue, green."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("input", nargs="?", help="Path to the source image")
    parser.add_argument("output", nargs="?", help="Output path (.png)")
    parser.add_argument(
        "-o",
        "--output",
        dest="output_flag",
        metavar="PATH",
        help="Output path (.png). Takes precedence over positional output.",
    )
    parser.add_argument(
        "--guided",
        action="store_true",
        help="Interactive prompt mode — walk through all settings step by step",
    )
    parser.add_argument(
        "--preset",
        choices=preset_names(),
        help="Named preset as base config; explicit flags override preset values.",
    )
    parser.add_argument(
        "--list-presets",
        action="store_true",
        help="Print all available presets and exit",
    )
    parser.add_argument(
        "--width",
        type=int,
        default=None,
        help=f"Canvas width in pixels (default: {DISPLAY_WIDTH})",
    )
    parser.add_argument(
        "--height",
        type=int,
        default=None,
        help=f"Canvas height in pixels (default: {DISPLAY_HEIGHT})",
    )
    parser.add_argument(
        "--fit",
        choices=("cover", "contain", "stretch"),
        default=None,
        help="Resize strategy (default: cover)",
    )
    parser.add_argument(
        "--dither",
        choices=("none", "floyd-steinberg", "atkinson"),
        default=None,
        help="Dithering algorithm (default: atkinson)",
    )
    parser.add_argument(
        "--brightness",
        type=float,
        default=None,
        help="Brightness multiplier (default: 1.0)",
    )
    parser.add_argument(
        "--contrast",
        type=float,
        default=None,
        help="Contrast multiplier (default: 1.0)",
    )
    parser.add_argument(
        "--saturation",
        type=float,
        default=None,
        help="Saturation multiplier (default: 1.0)",
    )
    parser.add_argument(
        "--sharpness",
        type=float,
        default=None,
        help="Sharpness multiplier (default: 1.0)",
    )
    parser.add_argument(
        "--rotate",
        type=int,
        choices=(0, 90, 180, 270),
        default=None,
        help="Rotation after EXIF normalization (default: 0)",
    )
    parser.add_argument(
        "--background",
        choices=PALETTE_NAMES,
        default=None,
        help="Fill color for contain mode letterboxing (default: white)",
    )
    parser.add_argument(
        "--edge-enhance",
        action=argparse.BooleanOptionalAction,
        default=None,
        help="PIL edge enhancement filter",
    )
    parser.add_argument(
        "--smooth",
        action=argparse.BooleanOptionalAction,
        default=None,
        help="PIL smoothing filter",
    )
    parser.add_argument(
        "--filter-sharpen",
        action=argparse.BooleanOptionalAction,
        default=None,
        help="PIL sharpen filter",
    )
    parser.add_argument(
        "--detail-stack",
        action="store_true",
        help="Enable edge-enhance + smooth + sharpen together",
    )
    return parser.parse_args(argv)


# ---------------------------------------------------------------------------
# Main entry point
# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    """CLI entry point."""
    args = parse_args(argv if argv is not None else sys.argv[1:])

    if args.list_presets:
        print_presets()
        return 0

    if args.guided or not args.input:
        options = guided_mode(args)
    else:
        options = build_options_from_args(args)

    validate_options(options)
    run_conversion(options)
    return 0


# ---------------------------------------------------------------------------
# Option building
# ---------------------------------------------------------------------------


def build_options_from_args(args: argparse.Namespace) -> ConversionOptions:
    """Construct options from CLI args, applying preset + explicit overrides."""
    input_path = Path(args.input).expanduser().resolve()
    if not input_path.exists():
        raise SystemExit(f"Error: input file not found: {input_path}")

    settings = resolve_settings(args)
    # --output flag wins over positional output argument
    output_arg = args.output_flag or args.output

    options = ConversionOptions(
        input_path=input_path,
        output_path=input_path,  # placeholder, resolved below
        preset=args.preset or "custom",
        width=int(settings["width"]),
        height=int(settings["height"]),
        fit=str(settings["fit"]),
        dither=str(settings["dither"]),
        brightness=float(settings["brightness"]),
        contrast=float(settings["contrast"]),
        saturation=float(settings["saturation"]),
        sharpness=float(settings["sharpness"]),
        rotate=int(settings["rotate"]),
        background=str(settings["background"]),
        edge_enhance=bool(settings["edge_enhance"]),
        smooth=bool(settings["smooth"]),
        filter_sharpen=bool(settings["filter_sharpen"]),
    )
    options.output_path = resolve_output_path(options, output_arg)
    return options


def resolve_settings(args: argparse.Namespace) -> dict[str, object]:
    """Layer defaults -> preset -> explicit CLI flags to produce final settings."""
    settings = dict(DEFAULT_VALUES)

    # Apply preset as base layer
    preset = PRESETS_BY_NAME.get(args.preset) if args.preset else None
    if preset is not None:
        settings.update(preset.values)

    # Explicit CLI flags override preset values
    for key in (
        "width",
        "height",
        "fit",
        "dither",
        "brightness",
        "contrast",
        "saturation",
        "sharpness",
        "rotate",
        "background",
    ):
        value = getattr(args, key, None)
        if value is not None:
            settings[key] = value

    # Resolve filter flags with detail-stack shortcut
    edge_enhance = bool(settings["edge_enhance"])
    smooth = bool(settings["smooth"])
    filter_sharpen = bool(settings["filter_sharpen"])

    if args.detail_stack:
        edge_enhance = smooth = filter_sharpen = True
    if args.edge_enhance is not None:
        edge_enhance = args.edge_enhance
    if args.smooth is not None:
        smooth = args.smooth
    if args.filter_sharpen is not None:
        filter_sharpen = args.filter_sharpen

    settings["edge_enhance"] = edge_enhance
    settings["smooth"] = smooth
    settings["filter_sharpen"] = filter_sharpen
    return settings


def validate_options(options: ConversionOptions) -> None:
    """Validate conversion options with clear user-facing error messages."""
    errors: list[str] = []

    if options.width <= 0:
        errors.append("--width must be greater than zero")
    if options.height <= 0:
        errors.append("--height must be greater than zero")
    if options.brightness <= 0:
        errors.append("--brightness must be greater than zero")
    if options.contrast <= 0:
        errors.append("--contrast must be greater than zero")
    if options.saturation <= 0:
        errors.append("--saturation must be greater than zero")
    if options.sharpness <= 0:
        errors.append("--sharpness must be greater than zero")
    if options.fit not in ("cover", "contain", "stretch"):
        errors.append(f"--fit must be cover, contain, or stretch (got: {options.fit})")
    if options.dither not in ("none", "floyd-steinberg", "atkinson"):
        errors.append(
            f"--dither must be none, floyd-steinberg, or atkinson (got: {options.dither})"
        )
    if options.rotate not in (0, 90, 180, 270):
        errors.append(f"--rotate must be 0, 90, 180, or 270 (got: {options.rotate})")
    if options.background not in PALETTE_NAMES:
        errors.append(f"--background must be one of {', '.join(PALETTE_NAMES)}")

    if errors:
        raise SystemExit("Error:\n  " + "\n  ".join(errors))


# ---------------------------------------------------------------------------
# Conversion pipeline
# ---------------------------------------------------------------------------


def run_conversion(options: ConversionOptions) -> None:
    """Execute the full pipeline: load -> enhance -> resize -> quantize -> save."""
    source_image = load_source_image(options.input_path)
    image = convert_loaded_image(source_image, options)
    save_converted_image(image, options.output_path)

    print_summary(options)


def load_source_image(input_path: Path):
    """Load an image from disk, applying EXIF orientation and RGB normalization."""
    Image, _, _, ImageOps = _import_pillow()
    if not input_path.exists():
        raise SystemExit(f"Error: input file not found: {input_path}")
    image = Image.open(input_path)
    return ImageOps.exif_transpose(image).convert("RGB")


def convert_loaded_image(image, options: ConversionOptions):
    """Apply the Spectra 6 conversion pipeline to an already loaded image."""
    Image, ImageEnhance, ImageFilter, ImageOps = _import_pillow()
    lanczos = (
        Image.Resampling.LANCZOS if hasattr(Image, "Resampling") else Image.LANCZOS
    )

    if image.mode != "RGB":
        image = image.convert("RGB")

    # Optional rotation is applied after source normalization.
    if options.rotate:
        image = image.rotate(-options.rotate, expand=True)

    image = _apply_enhancements(image, options, ImageEnhance, ImageFilter)
    image = _fit_to_canvas(image, options, Image, ImageOps, lanczos)
    return _quantize_to_palette(image, options.dither, Image)


def save_converted_image(image, output_path: Path) -> None:
    """Persist a converted indexed image to disk as a lossless PNG."""
    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path, format="PNG", optimize=False, compress_level=6)


# ---------------------------------------------------------------------------
# Image processing helpers
# ---------------------------------------------------------------------------


def _apply_enhancements(image, options: ConversionOptions, ImageEnhance, ImageFilter):
    """Apply brightness, contrast, saturation, sharpness, and optional filters."""
    if options.brightness != 1.0:
        image = ImageEnhance.Brightness(image).enhance(options.brightness)
    if options.contrast != 1.0:
        image = ImageEnhance.Contrast(image).enhance(options.contrast)
    if options.saturation != 1.0:
        image = ImageEnhance.Color(image).enhance(options.saturation)
    if options.sharpness != 1.0:
        image = ImageEnhance.Sharpness(image).enhance(options.sharpness)
    if options.edge_enhance:
        image = image.filter(ImageFilter.EDGE_ENHANCE)
    if options.smooth:
        image = image.filter(ImageFilter.SMOOTH)
    if options.filter_sharpen:
        image = image.filter(ImageFilter.SHARPEN)
    return image


def _fit_to_canvas(image, options: ConversionOptions, Image, ImageOps, lanczos):
    """Resize the image to the target canvas using the selected fit mode."""
    size = (options.width, options.height)

    if options.fit == "stretch":
        return image.resize(size, lanczos)

    if options.fit == "cover":
        return ImageOps.fit(image, size, method=lanczos, centering=(0.5, 0.5))

    # contain: scale to fit within bounds, center on colored background
    contained = ImageOps.contain(image, size, method=lanczos)
    bg = _background_rgb(options.background)
    canvas = Image.new("RGB", size, bg)
    offset_x = (size[0] - contained.width) // 2
    offset_y = (size[1] - contained.height) // 2
    canvas.paste(contained, (offset_x, offset_y))
    return canvas


# ---------------------------------------------------------------------------
# Palette helpers
# ---------------------------------------------------------------------------


def _quantize_to_palette(image, dither: str, Image):
    """Reduce an RGB image to the fixed six-color palette as an indexed PNG."""
    width, height = image.size
    src_bytes = image.tobytes()
    dst_indices = bytearray(width * height)

    if dither == "none":
        _map_without_dither(src_bytes, dst_indices)
    elif dither == "floyd-steinberg":
        _dither_floyd_steinberg(src_bytes, dst_indices, width, height)
    else:
        _dither_atkinson(src_bytes, dst_indices, width, height)

    result = Image.new("P", image.size)
    result.putpalette(PNG_PALETTE_BYTES)
    result.frombytes(bytes(dst_indices))
    return result


def _map_without_dither(src_bytes: bytes, dst_indices: bytearray) -> None:
    """Map each RGB pixel directly to the nearest palette entry."""
    out_pos = 0
    for src_pos in range(0, len(src_bytes), 3):
        dst_indices[out_pos] = _nearest_palette_index(
            src_bytes[src_pos],
            src_bytes[src_pos + 1],
            src_bytes[src_pos + 2],
        )
        out_pos += 1


def _nearest_palette_index(r: int, g: int, b: int) -> int:
    """Find the closest palette color using perceptually weighted distance.

    Scoring combines two terms:
      1. Weighted squared RGB distance (weights approximate human sensitivity:
         green > red > blue, similar to NTSC luminance coefficients).
      2. Squared luma difference penalty — discourages mapping to palette entries
         with very different perceived brightness even if RGB distance is close.

    This is intentionally simple and fast. For a six-color palette with maximally
    separated entries, it gives good results without full CIELAB conversion.
    """
    best_idx = 0
    best_score = float("inf")
    luma = 0.2126 * r + 0.7152 * g + 0.0722 * b

    for i, (pr, pg, pb) in enumerate(PALETTE_COLORS):
        dr = r - pr
        dg = g - pg
        db = b - pb
        # Weighted channel distance (perceptual importance: G > R > B)
        dist = dr * dr * 0.30 + dg * dg * 0.59 + db * db * 0.11
        # Luma penalty prevents brightness mismatches
        luma_diff = luma - PALETTE_LUMA[i]
        score = dist + luma_diff * luma_diff * 0.35
        if score < best_score:
            best_score = score
            best_idx = i

    return best_idx


@lru_cache(maxsize=65536)
def nearest_palette_color(r: int, g: int, b: int) -> tuple[int, int, int]:
    """Return the nearest palette color as an RGB tuple."""
    return PALETTE_COLORS[_nearest_palette_index(r, g, b)]


def _background_rgb(name: str) -> tuple[int, int, int]:
    """Look up an RGB tuple by palette color name."""
    for color_name, rgb in PALETTE:
        if color_name == name:
            return rgb
    raise ValueError(f"Unknown background color: {name}")


# ---------------------------------------------------------------------------
# Dithering algorithms
# ---------------------------------------------------------------------------


def _dither_floyd_steinberg(
    src_bytes: bytes, dst_indices: bytearray, width: int, height: int
) -> None:
    """Floyd-Steinberg error diffusion to the six-color palette.

    Distributes quantization error to neighboring pixels:
      right:        7/16
      below-left:   3/16
      below:        5/16
      below-right:  1/16

    Uses two row buffers (current + next) with width+2 padding to safely
    write the right-neighbor position without boundary checks.
    """
    cur_r = [0.0] * (width + 2)
    cur_g = [0.0] * (width + 2)
    cur_b = [0.0] * (width + 2)
    nxt_r = [0.0] * (width + 2)
    nxt_g = [0.0] * (width + 2)
    nxt_b = [0.0] * (width + 2)
    src_pos = 0
    out_pos = 0
    clamp = _clamp
    nearest_index = _nearest_palette_index
    palette_colors = PALETTE_COLORS

    for y in range(height):
        for x in range(width):
            sr = src_bytes[src_pos]
            sg = src_bytes[src_pos + 1]
            sb = src_bytes[src_pos + 2]
            src_pos += 3

            idx = x + 1
            rv = clamp(sr + cur_r[idx])
            gv = clamp(sg + cur_g[idx])
            bv = clamp(sb + cur_b[idx])

            palette_idx = nearest_index(rv, gv, bv)
            pr, pg, pb = palette_colors[palette_idx]
            dst_indices[out_pos] = palette_idx
            out_pos += 1

            er = rv - pr
            eg = gv - pg
            eb = bv - pb

            cur_r[idx + 1] += er * (7.0 / 16.0)
            cur_g[idx + 1] += eg * (7.0 / 16.0)
            cur_b[idx + 1] += eb * (7.0 / 16.0)

            nxt_r[idx - 1] += er * (3.0 / 16.0)
            nxt_g[idx - 1] += eg * (3.0 / 16.0)
            nxt_b[idx - 1] += eb * (3.0 / 16.0)

            nxt_r[idx] += er * (5.0 / 16.0)
            nxt_g[idx] += eg * (5.0 / 16.0)
            nxt_b[idx] += eb * (5.0 / 16.0)

            nxt_r[idx + 1] += er * (1.0 / 16.0)
            nxt_g[idx + 1] += eg * (1.0 / 16.0)
            nxt_b[idx + 1] += eb * (1.0 / 16.0)

        cur_r, nxt_r = nxt_r, [0.0] * (width + 2)
        cur_g, nxt_g = nxt_g, [0.0] * (width + 2)
        cur_b, nxt_b = nxt_b, [0.0] * (width + 2)


def _dither_atkinson(
    src_bytes: bytes, dst_indices: bytearray, width: int, height: int
) -> None:
    """Atkinson error diffusion to the six-color palette.

    Distributes 6/8 of the quantization error (1/8 each) to six neighbors:
      current row: x+1, x+2
      next row:    x-1, x, x+1
      two rows down: x

    Intentionally discards 2/8 of the error, producing higher contrast with
    less color bleed — well suited for limited-color e-paper displays.

    Uses three row buffers with width+4 padding for safe neighbor access.
    """
    cur_r = [0.0] * (width + 4)
    cur_g = [0.0] * (width + 4)
    cur_b = [0.0] * (width + 4)
    nxt_r = [0.0] * (width + 4)
    nxt_g = [0.0] * (width + 4)
    nxt_b = [0.0] * (width + 4)
    nxt2_r = [0.0] * (width + 4)
    nxt2_g = [0.0] * (width + 4)
    nxt2_b = [0.0] * (width + 4)
    src_pos = 0
    out_pos = 0
    clamp = _clamp
    nearest_index = _nearest_palette_index
    palette_colors = PALETTE_COLORS

    for y in range(height):
        for x in range(width):
            sr = src_bytes[src_pos]
            sg = src_bytes[src_pos + 1]
            sb = src_bytes[src_pos + 2]
            src_pos += 3
            idx = x + 1  # offset so idx-1 is always valid
            rv = clamp(sr + cur_r[idx])
            gv = clamp(sg + cur_g[idx])
            bv = clamp(sb + cur_b[idx])

            palette_idx = nearest_index(rv, gv, bv)
            pr, pg, pb = palette_colors[palette_idx]
            dst_indices[out_pos] = palette_idx
            out_pos += 1

            # Each neighbor gets 1/8 of error; 6 neighbors = 6/8 distributed
            er = (rv - pr) / 8.0
            eg = (gv - pg) / 8.0
            eb = (bv - pb) / 8.0

            cur_r[idx + 1] += er
            cur_g[idx + 1] += eg
            cur_b[idx + 1] += eb

            cur_r[idx + 2] += er
            cur_g[idx + 2] += eg
            cur_b[idx + 2] += eb

            nxt_r[idx - 1] += er
            nxt_g[idx - 1] += eg
            nxt_b[idx - 1] += eb

            nxt_r[idx] += er
            nxt_g[idx] += eg
            nxt_b[idx] += eb

            nxt_r[idx + 1] += er
            nxt_g[idx + 1] += eg
            nxt_b[idx + 1] += eb

            nxt2_r[idx] += er
            nxt2_g[idx] += eg
            nxt2_b[idx] += eb

        cur_r, nxt_r, nxt2_r = nxt_r, nxt2_r, [0.0] * (width + 4)
        cur_g, nxt_g, nxt2_g = nxt_g, nxt2_g, [0.0] * (width + 4)
        cur_b, nxt_b, nxt2_b = nxt_b, nxt2_b, [0.0] * (width + 4)


def _clamp(value: float) -> int:
    """Clamp a float to the 0-255 integer range."""
    return max(0, min(255, int(round(value))))


# ---------------------------------------------------------------------------
# Output path helpers
# ---------------------------------------------------------------------------


def resolve_output_path(options: ConversionOptions, output_arg: str | None) -> Path:
    """Determine the output file path.

    Priority:
      1. Explicit output argument (from --output flag or positional)
      2. Auto-generated path in configs/images/ with a descriptive slug
    """
    if output_arg:
        path = Path(output_arg).expanduser()
        if path.suffix.lower() != ".png":
            path = path.with_suffix(".png")
        return path.resolve()

    repo_root = Path(__file__).resolve().parents[1]
    output_dir = repo_root / DEFAULT_OUTPUT_DIR
    slug = _build_filename_slug(options)
    filename = f"{options.input_path.stem}-epaper-{slug}.png"
    return (output_dir / filename).resolve()


def _build_filename_slug(options: ConversionOptions) -> str:
    """Build a concise filename slug from active settings.

    Includes preset name, fit mode, and dither mode as the base.
    Only appends adjustment tokens for values that differ from 1.0,
    keeping filenames short when using presets with minimal customization.

    Examples:
            default-cover-atkinson
            vivid-cover-fs
            custom-contain-none-b090-s120
    """
    tokens: list[str] = [options.preset, options.fit, _short_dither(options.dither)]

    if options.brightness != 1.0:
        tokens.append(f"b{_fmt100(options.brightness)}")
    if options.contrast != 1.0:
        tokens.append(f"c{_fmt100(options.contrast)}")
    if options.saturation != 1.0:
        tokens.append(f"s{_fmt100(options.saturation)}")
    if options.sharpness != 1.0:
        tokens.append(f"h{_fmt100(options.sharpness)}")
    if options.edge_enhance:
        tokens.append("edge")
    if options.smooth:
        tokens.append("smooth")
    if options.filter_sharpen:
        tokens.append("sharp")
    if options.rotate:
        tokens.append(f"r{options.rotate}")
    if options.background != "white":
        tokens.append(f"bg{options.background}")
    if options.width != DISPLAY_WIDTH or options.height != DISPLAY_HEIGHT:
        tokens.append(f"{options.width}x{options.height}")

    return "-".join(tokens)


def _short_dither(dither: str) -> str:
    """Abbreviate dither mode for filenames."""
    return {"none": "none", "floyd-steinberg": "fs", "atkinson": "atkinson"}[dither]


def _fmt100(value: float) -> str:
    """Format a multiplier as a zero-padded percentage integer (1.08 -> 108)."""
    return f"{int(round(value * 100)):03d}"


# ---------------------------------------------------------------------------
# Guided prompt helpers
# ---------------------------------------------------------------------------


def guided_mode(args: argparse.Namespace) -> ConversionOptions:
    """Walk through all settings interactively."""
    print("Spectra 6 Image Converter")
    print("Palette: black, white, yellow, red, blue, green")
    print()

    input_value = _prompt_path("Source image", default=args.input, must_exist=True)
    preset_name = _prompt_choice(
        "Preset", ("custom",) + preset_names(), args.preset or "custom"
    )

    # Build effective settings using preset as base
    settings = dict(DEFAULT_VALUES)
    if preset_name != "custom":
        preset = PRESETS_BY_NAME[preset_name]
        settings.update(preset.values)

    options = ConversionOptions(
        input_path=Path(input_value).expanduser().resolve(),
        output_path=Path(input_value).expanduser().resolve(),
        preset=preset_name,
        width=_prompt_int("Width", int(settings["width"])),
        height=_prompt_int("Height", int(settings["height"])),
        fit=_prompt_choice(
            "Fit mode", ("cover", "contain", "stretch"), str(settings["fit"])
        ),
        dither=_prompt_choice(
            "Dither", ("none", "floyd-steinberg", "atkinson"), str(settings["dither"])
        ),
        brightness=_prompt_float("Brightness", float(settings["brightness"])),
        contrast=_prompt_float("Contrast", float(settings["contrast"])),
        saturation=_prompt_float("Saturation", float(settings["saturation"])),
        sharpness=_prompt_float("Sharpness", float(settings["sharpness"])),
        rotate=_prompt_choice(
            "Rotate", ("0", "90", "180", "270"), str(int(settings["rotate"])), cast=int
        ),
        background=_prompt_choice(
            "Background", PALETTE_NAMES, str(settings["background"])
        ),
        edge_enhance=_prompt_bool("Edge enhance", bool(settings["edge_enhance"])),
        smooth=_prompt_bool("Smooth", bool(settings["smooth"])),
        filter_sharpen=_prompt_bool("Filter sharpen", bool(settings["filter_sharpen"])),
    )

    output_default = str(resolve_output_path(options, args.output_flag or args.output))
    output_value = _prompt_text("Output file", default=output_default)
    options.output_path = Path(output_value).expanduser().resolve()
    return options


def _prompt_text(label: str, default: str | None = None) -> str:
    suffix = f" [{default}]" if default else ""
    while True:
        value = input(f"{label}{suffix}: ").strip()
        if value:
            return value
        if default is not None:
            return default


def _prompt_path(
    label: str, *, default: str | None = None, must_exist: bool = False
) -> str:
    while True:
        value = _prompt_text(label, default=default)
        path = Path(value).expanduser()
        if must_exist and not path.exists():
            print(f"  File not found: {path}")
            continue
        return value


def _prompt_int(label: str, default: int) -> int:
    while True:
        raw = _prompt_text(label, str(default))
        try:
            value = int(raw)
        except ValueError:
            print("  Please enter a valid integer.")
            continue
        if value <= 0:
            print("  Value must be greater than zero.")
            continue
        return value


def _prompt_float(label: str, default: float) -> float:
    while True:
        raw = _prompt_text(label, str(default))
        try:
            value = float(raw)
        except ValueError:
            print("  Please enter a valid number.")
            continue
        if value <= 0:
            print("  Value must be greater than zero.")
            continue
        return value


def _prompt_choice(label: str, choices: tuple[str, ...], default: str, cast=str):
    rendered = "/".join(choices)
    while True:
        value = _prompt_text(f"{label} ({rendered})", default)
        if value in choices:
            return cast(value)
        print(f"  Choose one of: {', '.join(choices)}")


def _prompt_bool(label: str, default: bool) -> bool:
    hint = "y" if default else "n"
    while True:
        value = _prompt_text(f"{label} (y/n)", hint).lower()
        if value in ("y", "yes"):
            return True
        if value in ("n", "no"):
            return False
        print("  Please answer y or n.")


# ---------------------------------------------------------------------------
# Summary output
# ---------------------------------------------------------------------------


def print_summary(options: ConversionOptions) -> None:
    """Print a terminal summary with settings and suggested ESPHome snippet."""
    rel_output = _relative_to_repo(options.output_path)
    image_id = _suggested_image_id(options.output_path)
    filters = _active_filters(options)

    print()
    print("Conversion complete")
    print(f"  input:      {options.input_path}")
    print(f"  output:     {options.output_path}")
    print(f"  preset:     {options.preset}")
    print(f"  canvas:     {options.width}\u00d7{options.height}")
    print(f"  fit:        {options.fit}")
    print(f"  dither:     {options.dither}")
    print(f"  filters:    {filters}")
    print("  palette:    black, white, yellow, red, blue, green")
    print()
    print("ESPHome snippet:")
    print()
    print("  image:")
    print(f"    - file: {rel_output}")
    print(f"      id: {image_id}")
    print("      type: RGB")


def print_presets() -> None:
    """Print available presets with descriptions."""
    print("Available presets:")
    print()
    print(f"  {'custom':<14} Use raw defaults without a preset.")
    for p in PRESETS_BY_NAME.values():
        print(f"  {p.name:<14} {p.description}")


def _relative_to_repo(path: Path) -> str:
    repo_root = Path(__file__).resolve().parents[1]
    try:
        return path.resolve().relative_to(repo_root).as_posix()
    except ValueError:
        return str(path)


def _suggested_image_id(path: Path) -> str:
    """Generate a valid ESPHome image ID from the output filename."""
    chars = []
    for c in path.stem.lower():
        chars.append(c if c.isalnum() else "_")
    collapsed = "".join(chars).strip("_")
    while "__" in collapsed:
        collapsed = collapsed.replace("__", "_")
    return f"img_{collapsed or 'artwork'}"


def _active_filters(options: ConversionOptions) -> str:
    names = []
    if options.edge_enhance:
        names.append("edge-enhance")
    if options.smooth:
        names.append("smooth")
    if options.filter_sharpen:
        names.append("sharpen")
    return ", ".join(names) if names else "none"


# ---------------------------------------------------------------------------
# Pillow import
# ---------------------------------------------------------------------------


def _import_pillow():
    """Import Pillow with a clear error if missing."""
    try:
        from PIL import Image, ImageEnhance, ImageFilter, ImageOps
    except ImportError as exc:
        raise SystemExit(
            "Pillow is required. Run ./scripts/setup.sh to set up the environment."
        ) from exc
    return Image, ImageEnhance, ImageFilter, ImageOps


# ---------------------------------------------------------------------------

if __name__ == "__main__":
    raise SystemExit(main())
