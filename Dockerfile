FROM golang:1.11.4-stretch

RUN apt-get update && apt-get install -y \
    build-essential \
    curl \
    sudo \
    gawk \
    iptables \
    jq \
    pkg-config \
    libaio-dev \
    libcap-dev \
    libprotobuf-dev \
    libprotobuf-c0-dev \
    libnl-3-dev \
    libnet-dev \
    libseccomp2 \
    libseccomp-dev \
    libapparmor-dev \
    protobuf-c-compiler \
    protobuf-compiler \
    python-minimal \
    uidmap \
    kmod \
    libseccomp-dev \
    --no-install-recommends \
&& apt-get clean

COPY . /go/src/github.com/opencontainers/runc
WORKDIR /go/src/github.com/opencontainers/runc
RUN for VER in v1.12.6 v1.13.1 v17.03.2 v17.06.2 v17.09.1 v17.12.1 v18.03.1 v18.06.1; do \
    git checkout release-${VER} && \
    for GOARCH in $(go env GOARCH); do \
        export GOARCH && \
        make BUILDTAGS="seccomp selinux apparmor" static && \
        mkdir -p dist && \
        mv runc dist/runc-${VER}-${GOARCH} \
    ; done ; done && \
    cd dist && \
    sha256sum * > sha256sum-${GOARCH}.txt
