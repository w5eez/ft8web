#!/bin/sh
# Build ft2sim (FT2 = FT4 compressed 2x) and install it into the WSJT-X build dir.
# ft8web's TxEngine runs vendor/wsjtx-3.1.0/build/ft2sim exactly like ft4sim.
#
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
WSJTX=$REPO/vendor/wsjtx-3.1.0
B=$WSJTX/build                       # vendor build dir: libs, .mod files, output ft2sim
FT4=$WSJTX/src/wsjtx/lib/ft4         # ft4_params.f90 include lives here (pristine upstream)
L="/usr/lib/$(gfortran -print-multiarch)"
gfortran -fno-second-underscore -O2 -w -o "$B/ft2sim" "$HERE/ft2sim.f90" \
  -I"$FT4" -I"$B" "$B/libwsjt_fort.a" "$B/libwsjt_cxx.a" \
  "$L/libfftw3f_threads.so" "$L/libfftw3f.so" -lm \
  "$L"/libboost_log_setup.so.* "$L"/libboost_log.so.* "$L"/libboost_chrono.so.* \
  "$L"/libboost_filesystem.so.* "$L"/libboost_regex.so.* "$L"/libboost_thread.so.* \
  "$L"/libboost_atomic.so.* -lgfortran -lstdc++
echo "built $B/ft2sim"
