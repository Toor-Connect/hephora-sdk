FROM mcr.microsoft.com/devcontainers/base:alpine-3.22

RUN apk add --no-cache \
    xz \
    python3 \
    py3-pip \
    git \
    bash \
    cmake \
    gdb \
    sqlite-dev \
    lua5.4-dev

# Alpine ships liblua-5.4 without a liblua.so symlink; add it for -llua linkers.
RUN if [ -f /usr/lib/liblua-5.4.so.0 ] && [ ! -e /usr/lib/liblua.so ]; then \
        ln -s /usr/lib/liblua-5.4.so.0 /usr/lib/liblua.so; \
    fi


