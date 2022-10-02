#!/bin/bash
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"
cd $DIR

if [ ! -d LimeSuite ]; then
  git clone https://github.com/myriadrf/LimeSuite.git
  cd LimeSuite
  # checkout latest version which has firmware updates available
  git checkout v20.10.0
  cp ../mcu_error.patch .
  git apply mcu_error.patch
  mkdir builddir && cd builddir
  cmake -DCMAKE_BUILD_TYPE=Release ..
  make -j4
  cd ../..
fi

if [ ! -d LimeGPS ]; then
  git clone https://github.com/osqzss/LimeGPS.git
  cd LimeGPS
  sed -i 's/LimeSuite/LimeSuite -I..\/LimeSuite\/src -L..\/LimeSuite\/builddir\/src/' makefile

  cp ../inc_ephem_array_size.patch .
  git apply inc_ephem_array_size.patch

  make
  cd ..
fi
