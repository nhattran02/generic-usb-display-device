#!/bin/sh
# Render the standalone UI (main/ui) on a PC: one PPM per page in tools/ui_preview/out, plus how much of
# the screen each page flushes per second. Needs gcc (Linux, or MinGW on Windows) and LVGL fetched by a
# firmware build (managed_components/lvgl__lvgl).
set -e
cd "$(dirname "$0")"
ROOT=../..
LV=$ROOT/managed_components/lvgl__lvgl
[ -d "$LV/src" ] || { echo "run idf.py reconfigure first (fetches LVGL)" >&2; exit 1; }
mkdir -p obj out
# LVGL itself only once
[ -f obj/.done ] || { find "$LV/src" -name '*.c' | (cd obj && xargs -P 8 -n 20 gcc -c -O1 -w \
    -DLV_CONF_INCLUDE_SIMPLE -I.. -I"../$LV") && touch obj/.done; }
gcc -O1 -Wall -Wextra -DLV_CONF_INCLUDE_SIMPLE -I. -I"$LV" -I$ROOT/main/ui preview.c \
    $ROOT/main/ui/ui_app.c $ROOT/main/ui/ui_pages.c $ROOT/main/ui/ui_monitor.c $ROOT/main/ui/ui_wave.c \
    $ROOT/main/ui/ui_vitals.c obj/*.o -lm -o preview
./preview out
