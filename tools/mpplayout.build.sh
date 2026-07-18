#!/bin/sh
# Build mpplayout standalone against this fork's freshly-built static libs.
set -e
FORK="$(cd "$(dirname "$0")/.." && pwd)"
cc -O2 -Wall -I"$FORK" -o "$FORK/mpplayout" "$FORK/tools/mpplayout.c" \
  "$FORK/libavformat/libavformat.a" \
  "$FORK/libavcodec/libavcodec.a" \
  "$FORK/libswresample/libswresample.a" \
  "$FORK/libavutil/libavutil.a" \
  -lm -lbz2 -lz -latomic -lssl -lcrypto -pthread -llzma -lx264 \
  -lva -lva-drm -lva-x11 -lvdpau -lX11 -ldrm
echo "built $FORK/mpplayout"
