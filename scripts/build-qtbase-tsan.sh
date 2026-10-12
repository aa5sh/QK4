#!/usr/bin/env bash
#
# Build Qt's qtbase with ThreadSanitizer and install it to a prefix, for the weekly TSan job
# (.github/workflows/scheduled-sanitizers.yml). Linux only.
#
# WHY QT HAS TO BE INSTRUMENTED: TSan only knows two threads were synchronised if it saw the
# synchronisation. A distro Qt is not built with TSan, and on Linux its mutex is a raw futex TSan
# cannot see, so every cross-thread queued call - the most ordinary thing a Qt program does - is
# reported as a data race in moc's qt_static_metacall. Measured 2026-10-12 on Ubuntu 24.04 with
# its Qt 6.4.2: a Qt-only program that moves one object to a thread and queues one call produced
# eight such reports (#172). Building Qt with -sanitize thread removes the blind spot instead of
# suppressing what it causes, so TSan still catches real races in QK4's code.
#
# WHY ONLY QTBASE: the tests link Core, Gui, Widgets, Network and Test, all in qtbase. QK4 is then
# configured with -DQK4_TESTS_ONLY=ON so nothing asks for the modules not built here.
#
# GLib is off for the same reason Qt is instrumented: the distro GLib is not, and Qt's GLib event
# dispatcher drew races of its own in the same measurement. Qt's own dispatcher replaces it.
#
# Usage: scripts/build-qtbase-tsan.sh <install-prefix>

set -euo pipefail

readonly QT_VERSION=6.10.1
# Qt's published checksum for qtbase-everywhere-src-6.10.1.tar.xz (download.qt.io, .sha256).
readonly QT_SHA256=5a6226f7e23db51fdc3223121eba53f3f5447cf0cc4d6cb82a3a2df7a65d265d

prefix=${1:?usage: $0 <install-prefix>}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

url="https://download.qt.io/official_releases/qt/${QT_VERSION%.*}/${QT_VERSION}/submodules/qtbase-everywhere-src-${QT_VERSION}.tar.xz"
curl -sSfL --retry 3 -o "$work/qtbase.tar.xz" "$url"
echo "${QT_SHA256}  $work/qtbase.tar.xz" | sha256sum -c -
tar -xf "$work/qtbase.tar.xz" -C "$work"

cmake -S "$work/qtbase-everywhere-src-${QT_VERSION}" -B "$work/build" -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DFEATURE_sanitize_thread=ON \
    -DFEATURE_glib=OFF \
    -DINPUT_opengl=no \
    -DFEATURE_vulkan=OFF \
    -DFEATURE_dbus=OFF \
    -DFEATURE_sql=OFF \
    -DFEATURE_icu=OFF \
    -DQT_BUILD_EXAMPLES=OFF \
    -DQT_BUILD_TESTS=OFF
cmake --build "$work/build" --parallel
cmake --install "$work/build"
