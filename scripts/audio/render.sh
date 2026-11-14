#!/bin/sh
# Offline only. All intermediates stay in an owned, uniquely named directory.
set -eu
umask 022

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
assets="$root/native/assets/audio"
converter=/usr/bin/afconvert
compiler=${CXX:-/usr/bin/clang++}

if [ ! -x "$converter" ]; then
    printf '%s\n' 'Blocker: /usr/bin/afconvert is required for the AAC deliverables.' >&2
    exit 1
fi
if ! command -v "$compiler" >/dev/null 2>&1; then
    printf '%s\n' 'Blocker: a C++20 compiler is required for offline rendering.' >&2
    exit 1
fi

stage=$(mktemp -d "$assets/.render.XXXXXXXX")
trap 'rm -r -- "$stage"' 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
mkdir "$stage/sfx" "$stage/music" "$stage/decoded"

"$compiler" -std=c++20 -O2 -Wall -Wextra -Wpedantic \
    "$root/scripts/audio/compose.cpp" -o "$stage/compose"

# Catalog is an input only. Existing legacy aliases are never regenerated.
"$stage/compose" plan "$assets" > "$stage/plan.tsv"
tab=$(printf '\t')
while IFS="$tab" read -r kind id; do
    case "$kind" in
        sfx)
            "$stage/compose" render "$assets" sfx "$id" "$stage/sfx/$id.wav"
            ;;
        music)
            "$stage/compose" render "$assets" music "$id" "$stage/source.wav"
            "$converter" -f m4af -d aac -b 192000 -q 127 \
                "$stage/source.wav" "$stage/music/$id.m4a"
            "$converter" -f WAVE -d LEI16 \
                "$stage/music/$id.m4a" "$stage/decoded/$id.wav"
            rm -- "$stage/source.wav"
            ;;
        *) printf '%s\n' 'Invalid generation plan.' >&2; exit 1 ;;
    esac
done < "$stage/plan.tsv"

# Audit the delivered codec's decoded PCM, not just its synthesis input.
"$stage/compose" audit "$assets" "$stage"

# Publish only after the complete cohort passes. Preserve catalog metadata.
mkdir -p "$assets/sfx" "$assets/music"
while IFS="$tab" read -r kind id; do
    case "$kind" in
        sfx) mv -f -- "$stage/sfx/$id.wav" "$assets/sfx/$id.wav" ;;
        music) mv -f -- "$stage/music/$id.m4a" "$assets/music/$id.m4a" ;;
    esac
done < "$stage/plan.tsv"
printf '%s\n' 'Published 20 WAV cues and 20 AAC/M4A tracks. Catalog metadata was not modified.'
