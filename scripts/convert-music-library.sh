#!/usr/bin/env bash
# Converts a music library into a flat, SD-card-ready MP3 copy, matching
# source quality (LAME VBR -V0, ~245kbps avg / up to 320kbps) while
# avoiding AAC/M4A on the device — see docs/adr/0002-v1-format-and-mcu-scope.md.
#
# Resumable by design: it compares against files already present in the
# destination (by relative path with the extension swapped to .mp3), not
# against a source manifest, so re-running after an interruption only
# processes what's missing. This intentionally does NOT use rsync — the
# destination has already-converted files with different extensions and
# names than the source, so rsync's file-identity comparison doesn't
# apply here.
#
# Usage: convert-music-library.sh <source-dir> <dest-dir>
set -euo pipefail

SRC="${1:?Usage: convert-music-library.sh <source-dir> <dest-dir>}"
DEST="${2:?Usage: convert-music-library.sh <source-dir> <dest-dir>}"

if [[ ! -d "$SRC" ]]; then
  echo "Source directory not found: $SRC" >&2
  exit 1
fi

mkdir -p "$DEST"

# Already-lossy-compressed formats we don't re-encode (preserves quality,
# avoids a lossy-to-lossy generation loss): mp3 is copied as-is.
# Everything else we recognize as audio (m4a, flac, wav, ogg) gets
# transcoded to MP3 -V0.
COPY_AS_IS_EXTS=("mp3")
TRANSCODE_EXTS=("m4a" "flac" "wav" "ogg" "aac" "wma")

total=0
converted=0
copied=0
skipped=0
failed=0

is_in() {
  local needle="$1"; shift
  for x in "$@"; do [[ "$x" == "$needle" ]] && return 0; done
  return 1
}

# Build the find expression once for every extension we care about.
find_args=()
for ext in "${COPY_AS_IS_EXTS[@]}" "${TRANSCODE_EXTS[@]}"; do
  find_args+=(-iname "*.${ext}" -o)
done
unset 'find_args[${#find_args[@]}-1]' # drop trailing -o

while IFS= read -r -d '' src_file; do
  base_name="$(basename "$src_file")"
  # Skip macOS AppleDouble sidecar files (e.g. "._Track.mp3") that SMB
  # shares expose alongside the real file — not audio, just resource-fork
  # junk that happens to match the same extension.
  if [[ "$base_name" == ._* ]]; then
    continue
  fi

  total=$((total + 1))

  rel_path="${src_file#"$SRC"/}"
  ext="${rel_path##*.}"
  ext_lower="$(echo "$ext" | tr '[:upper:]' '[:lower:]')"
  rel_no_ext="${rel_path%.*}"
  dest_file="$DEST/${rel_no_ext}.mp3"

  if [[ -s "$dest_file" ]]; then
    skipped=$((skipped + 1))
    continue
  fi

  mkdir -p "$(dirname "$dest_file")"

  if is_in "$ext_lower" "${COPY_AS_IS_EXTS[@]}"; then
    if cp "$src_file" "$dest_file"; then
      copied=$((copied + 1))
    else
      echo "FAILED (copy): $rel_path" >&2
      rm -f "$dest_file"
      failed=$((failed + 1))
    fi
  else
    # -map 0:v? copies embedded cover art (if present) as the MP3's ID3
    # APIC frame; the trailing "?" makes it optional so files without
    # embedded art still convert instead of erroring.
    if ffmpeg -nostdin -y -loglevel error -i "$src_file" \
        -map 0:a:0 -map 0:v? -codec:a libmp3lame -q:a 0 -codec:v copy \
        -id3v2_version 3 -write_id3v1 1 \
        "$dest_file" </dev/null; then
      converted=$((converted + 1))
    else
      echo "FAILED (transcode): $rel_path" >&2
      rm -f "$dest_file"
      failed=$((failed + 1))
    fi
  fi

  if (( total % 100 == 0 )); then
    echo "... $total processed (converted=$converted copied=$copied skipped=$skipped failed=$failed)"
  fi
done < <(find "$SRC" -type f \( "${find_args[@]}" \) -print0)

echo "== Done =="
echo "Total source audio files seen: $total"
echo "Transcoded to MP3:             $converted"
echo "Copied as-is (already MP3):    $copied"
echo "Already present (skipped):     $skipped"
echo "Failed:                        $failed"
du -sh "$DEST" 2>/dev/null || true
