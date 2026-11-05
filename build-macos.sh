#!/bin/sh
#
# Build MMDAgent-EX on macOS (Apple Silicon, Homebrew).
#
# Poco is built from the bundled Library_Poco/poco-1.12.4-all.zip (patched so it compiles
# against OpenSSL 3/4) rather than taken from Homebrew, and installed under
# build/poco-install. The MMDAgent-EX binary loads it from there at run time, so keep the
# build directory.
#
# Also needed on current macOS (already in the tree): no AGL framework in the link line,
# and libpng must not include the classic Mac OS <fp.h>.
#
# Dependencies:
#   brew install cmake openssl@4 pcre2 glew jpeg-turbo minizip libomp libsndfile \
#     libsamplerate portaudio re2 abseil librdkafka rabbitmq-c
#
set -e
cd "$(dirname "$0")"
TOP=$(pwd)

OPENSSL=$(brew --prefix openssl@4 2>/dev/null || brew --prefix openssl@3)
POCO_SRC=$TOP/build/poco-src/poco-1.12.4-all
POCO_INSTALL=$TOP/build/poco-install
JOBS=$(sysctl -n hw.ncpu)

if [ ! -f "$POCO_INSTALL/lib/cmake/Poco/PocoConfig.cmake" ]; then
  echo "== Building Poco 1.12.4 into $POCO_INSTALL"
  rm -rf "$TOP/build/poco-src"
  mkdir -p "$TOP/build/poco-src"
  unzip -q Library_Poco/poco-1.12.4-all.zip -d "$TOP/build/poco-src"
  patch -d "$POCO_SRC" -p1 < Library_Poco/poco-1.12.4-openssl3.patch
  cmake -S "$POCO_SRC" -B "$POCO_SRC/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$POCO_INSTALL" \
    -DOPENSSL_ROOT_DIR="$OPENSSL" \
    -DPOCO_UNBUNDLED=ON \
    -DENABLE_TESTS=OFF \
    -DENABLE_FOUNDATION=ON -DENABLE_XML=ON -DENABLE_JSON=ON -DENABLE_NET=ON \
    -DENABLE_NETSSL=ON -DENABLE_CRYPTO=ON -DENABLE_DATA=ON -DENABLE_ZIP=ON \
    -DENABLE_UTIL=ON -DENABLE_ENCODINGS=ON \
    -DENABLE_JWT=OFF -DENABLE_MONGODB=OFF -DENABLE_REDIS=OFF -DENABLE_PDF=OFF \
    -DENABLE_PROMETHEUS=OFF -DENABLE_SEVENZIP=OFF -DENABLE_PAGECOMPILER=OFF \
    -DENABLE_PAGECOMPILER_FILE2PAGE=OFF -DENABLE_ACTIVERECORD=OFF \
    -DENABLE_ACTIVERECORD_COMPILER=OFF -DENABLE_DATA_SQLITE=OFF \
    -DENABLE_DATA_MYSQL=OFF -DENABLE_DATA_POSTGRESQL=OFF -DENABLE_DATA_ODBC=OFF
  cmake --build "$POCO_SRC/build" -j "$JOBS"
  cmake --install "$POCO_SRC/build"
fi

echo "== Building MMDAgent-EX"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$POCO_INSTALL" \
  -DOPENSSL_ROOT_DIR="$OPENSSL"
cmake --build build -j "$JOBS"

echo "== Done: Release/MMDAgent-EX"
