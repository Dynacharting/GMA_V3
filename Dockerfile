# KRN engine image — multi-stage build that compiles the C++
# `krn_engine_server` (krn_host + the reference plugin) against a known-good
# third-party set and ships the binary on a slim Debian runtime. The market
# server, krn_server, and its krn.conf are krn-common's (ENC-1570).
#
# Build context is THIS repo root (so the sibling .dockerignore applies and
# host build/ artifacts are excluded — ENC-790/H6):
#
#   cd <path-to>/gma_v3
#   docker build -t krn:dev .
#
# Runtime image is `debian:stable-slim` (not scratch). The C++
# binary links against system libstdc++/libgcc; copying those out of
# the build image into scratch is fragile, so we accept the Debian base.
#
# Default port: 4000 — the engine WebSocket.
#
# Configuration: compiled defaults; pass an INI path as the second argument
# (bind-mount it) to override them.

FROM debian:stable-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ca-certificates \
    libboost-system-dev \
    libboost-thread-dev \
    libssl-dev \
    rapidjson-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copy the repo into the image. .dockerignore keeps build*/ (and its
# absolute-path CMakeCache.txt), .git and IDE cruft out of the context.
COPY . .

# Out-of-source build in a clean directory. Release uses a portable
# microarchitecture baseline by default (see KRN_NATIVE_ARCH / KRN_ARCH_BASELINE
# in CMakeLists.txt) so the shipped binary runs on any modern x86_64 host.
RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /build --target krn_engine_server -j

# ─────────────────────────────────────────────────────────────────────

FROM debian:stable-slim

# Boost is statically linked (Boost_USE_STATIC_LIBS ON), so the
# runtime needs no libboost*. We deliberately do NOT pin a boost soname here
# (the old libboost-system1.83.0 / libboost-thread1.83.0 pins broke whenever the
# base image's boost version drifted — ENC-806/L16). The only dynamic deps are
# libstdc++/libgcc (in the base) and OpenSSL (krn_engine links it when found).
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libssl3 \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /build/krn_engine_server /app/krn_engine_server

EXPOSE 4000
STOPSIGNAL SIGTERM

# CLI: ./krn_engine_server [wsPort] [conf].
ENTRYPOINT ["/app/krn_engine_server", "4000"]
