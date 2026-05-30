#!/usr/bin/env bash
# Build a local comparison gallery for image conversion presets.
#
# Reads source images from SOURCE_DIR (defaults to configs/images/originals/), runs
# scripts/convert_image.py for each preset/dither combination, and writes
# the generated PNGs to OUTPUT_DIR (defaults to configs/images/gallery/).
#
# Usage: ./generate-gallery.sh [SOURCE_DIR] [OUTPUT_DIR]
# Use this to quickly compare preset and dithering results on sample images.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

SOURCE_DIR="${1:-${REPO_ROOT}/configs/images/originals}"
OUTPUT_DIR="${2:-${REPO_ROOT}/configs/images/gallery}"

# Create directories if they don't exist
mkdir -p "$SOURCE_DIR"
mkdir -p "$OUTPUT_DIR"

# Ensure venv exists
if [ ! -f "${REPO_ROOT}/.venv/bin/python" ]; then
    echo "Virtual environment not found. Run ./scripts/bootstrap-venv.sh first."
    exit 1
fi

shopt -s nullglob
files=("$SOURCE_DIR"/*)
if [ ${#files[@]} -eq 0 ]; then
    echo "No images found in $SOURCE_DIR."
    echo "Please add some source images to $SOURCE_DIR and run again."
    exit 0
fi

PRESETS=("default" "vivid" "graphics" "accurate")
DITHERS=("floyd-steinberg" "atkinson")

total_images=${#files[@]}
total_presets=${#PRESETS[@]}
total_dithers=${#DITHERS[@]}
total_combinations=$((total_images * total_presets * total_dithers))
current_step=0

echo "Starting conversion of $total_images images ($total_combinations combinations) into ${OUTPUT_DIR}..."

for img in "${files[@]}"; do
    [ -f "$img" ] || continue

    filename=$(basename -- "$img")
    stem="${filename%.*}"

    for preset in "${PRESETS[@]}"; do
        for dither in "${DITHERS[@]}"; do
            current_step=$((current_step + 1))
            out_file="${OUTPUT_DIR}/${stem}-${preset}-${dither}.png"
            echo "[$current_step/$total_combinations] Converting $filename (preset: $preset, dither: $dither)..."

            "${REPO_ROOT}/.venv/bin/python" "${REPO_ROOT}/scripts/convert_image.py" "$img" "$out_file" --preset "$preset" --dither "$dither" > /dev/null
        done
    done
done

echo "Done! Gallery images saved to $OUTPUT_DIR"
