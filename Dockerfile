# Build the CLI, then keep only the binary and the libraries it needs.
# The image is built by CI and is not published to a registry.

FROM debian:13-slim AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        clang-format \
        clang-tidy \
        curl \
        libcurl4-openssl-dev \
        libnghttp2-dev \
        libssl-dev \
        ninja-build \
        pkg-config \
        python3 \
    && rm -rf /var/lib/apt/lists/*

ARG CMAKE_VERSION=4.4.0
RUN arch="$(uname -m)" \
    && curl -fsSL -o /tmp/cmake.tgz \
        "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${arch}.tar.gz" \
    && tar -C /opt -xzf /tmp/cmake.tgz \
    && rm /tmp/cmake.tgz \
    && ln -s /opt/cmake-${CMAKE_VERSION}-linux-${arch}/bin/* /usr/local/bin/

WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DWRENCOTE_BUILD_TESTS=OFF \
        -DWRENCOTE_HARDEN=ON \
        -DWRENCOTE_WERROR=ON \
    && cmake --build build --target wrencote_cli

FROM debian:13-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libcurl4t64 \
        libnghttp2-14 \
        libssl3t64 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --create-home --shell /usr/sbin/nologin wrencote

COPY --from=build /src/build/wrencote /usr/local/bin/wrencote
COPY examples /opt/wrencote/examples

USER wrencote
EXPOSE 8080
ENTRYPOINT ["wrencote"]
CMD ["run", "/opt/wrencote/examples/app.wren", "--listen", "0.0.0.0:8080", "--allow", "http,json"]
