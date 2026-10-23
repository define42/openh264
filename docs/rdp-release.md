# OpenH264 RDP release process

OpenH264 RDP ships as a native SDK and a nested Go module from the same
repository. The SDK contains both the static and shared native libraries. The
Go wrapper selects the static library by default.

## Versions and tags

Native SDK tags have this form:

```text
v2.7.0-rdp.1
```

The first three numbers identify the OpenH264 base version. The final number
is the RDP fork revision for that base version.

Go module tags have this form:

```text
go/v0.1.0
```

The `go/` prefix is required because `go.mod` is in the `go` subdirectory.
Applications still request the module with the normal semantic version:

```shell
go get github.com/define42/openh264/go@v0.1.0
```

The native and Go tags may point to the same commit. Their version numbers are
independent so native ABI revisions do not force unrelated Go API versions.

## Build a local SDK archive

Build and stage both native library forms, then package the staged prefix:

```shell
release_version=2.7.0-rdp.1
make -j"$(nproc)" RDP_RELEASE_VERSION="$release_version" libraries
make RDP_RELEASE_VERSION="$release_version" \
  PREFIX=/usr DESTDIR="$PWD/dist/stage" install
build/package-rdp-sdk.sh \
  "$PWD/dist/stage/usr" \
  "$PWD/dist" \
  "$release_version" \
  linux-amd64
```

The archive has one top-level directory and contains:

```text
include/wels/*.h
lib/libopenh264-rdp.a
lib/libopenh264-rdp.so*
lib/pkgconfig/openh264-rdp-static.pc
lib/pkgconfig/openh264-rdp.pc
LICENSE
BUILDINFO
```

The pkg-config files in the archive are rewritten to be relative to their own
location. The SDK can therefore be extracted to any directory.

The official Linux amd64 archive is built inside an `ubuntu:20.04` container
pinned by image digest, independently of the GitHub-hosted runner version. Its
shared library has a glibc 2.31 baseline. The release workflow inspects its
versioned dynamic symbols with `readelf` and rejects an artifact that requires
a newer GLIBC symbol version.

The static archive is linked into the consumer's executable. Its final system
library and runtime requirements therefore inherit the consumer's build and
deployment environment rather than the shared SDK's glibc baseline.

## Link from Go

Set `PKG_CONFIG_PATH` to the extracted SDK before building:

```shell
export OPENH264_RDP_SDK="$PWD/openh264-rdp-2.7.0-rdp.1-linux-amd64"
export PKG_CONFIG_PATH="$OPENH264_RDP_SDK/lib/pkgconfig"
export LD_LIBRARY_PATH="$OPENH264_RDP_SDK/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

go build ./...
go build -tags openh264_shared ./...
```

The first build links `libopenh264-rdp.a`. The `openh264_shared` build tag is
an explicit opt-in to the shared library. `LD_LIBRARY_PATH` is only needed
when running a program linked to the shared Linux library from the extracted
SDK.

## Publish a release

1. Ensure native and Go CI pass for the release commit.
2. Create signed native and Go tags as needed:

   ```shell
   git tag -s v2.7.0-rdp.1
   git tag -s go/v0.1.0
   git push origin v2.7.0-rdp.1 go/v0.1.0
   ```

3. Pushing the native tag builds the Linux amd64 SDK in pinned Ubuntu 20.04,
   enforces the glibc 2.31 shared-library baseline, writes `SHA256SUMS`, and
   creates a draft GitHub Release.
4. Inspect the draft assets and generated notes, then publish the release.

Pushing the Go tag publishes the nested module version through the repository.
It does not create a second binary release.
