CVE Builds for legacy docker-runc
---------------------------------

This repo provides a backport of patches for CVE-2019-5736 for older versions of runc
that were packaged with Docker.

## Build and Releases

Refer to the [releases](https://github.com/rancher/runc-cve/releases) section of this repo for the binaries. In order to build yourself,
or build for different architectures, just run `make` and the binaries will end up in
`./dist`.

The binaries will be of the form runc-${VERSION}-${ARCHITECTURE} where VERSION is the
associated Docker version, not the version of runc.

> **Note:** The runc-cve release for Docker 17.03.2 can be used for Docker 17.03.3 as the runc binary between these two Docker releases use the same runc binary. 

## Installing

To install, find the runc for you docker version, for example Docker 17.06.2 for amd64
will be runc-v17.06.2-amd64.  For Linux 3.x kernels use the binaries that end with **no-memfd_create**.
Then replace the docker-runc on your host with the patched one.

```bash
# Figure out where your docker-runc is, typically in /usr/bin/docker-runc
which docker-runc

# Backup
mv /usr/bin/docker-runc /usr/bin/docker-runc.orig.$(date -Iseconds)

# Copy file
cp runc-v17.06.2-amd64 /usr/bin/docker-runc

# Ensure it's executable
chmod +x /usr/bin/docker-runc

# Test it works
docker-runc -v
docker run -it --rm ubuntu echo OK
```
