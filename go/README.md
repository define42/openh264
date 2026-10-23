# openh264-rdp for Go

`github.com/define42/openh264/go` is an encoder-first Go binding for the
`openh264-rdp` native library. It accepts planar I420 pictures and exposes the
fork's unchanged-macroblock and motion-vector hints.

The module requires Go 1.22 or newer, cgo, a C++ compiler, `pkg-config`, and a
matching openh264-rdp SDK. It has no third-party Go dependencies.

## Install

Install the native SDK so that `pkg-config` can find
`openh264-rdp-static.pc`, then add the module:

```sh
export PKG_CONFIG_PATH=/opt/openh264-rdp/lib/pkgconfig
go get github.com/define42/openh264/go@v0.1.0
```

The default build links `libopenh264-rdp.a`. This statically embeds the codec,
but does not imply a completely static executable; platform and C++ runtime
libraries may still be dynamic.

To use the shared native library instead:

```sh
go build -tags openh264_shared ./...
```

The shared build uses `openh264-rdp.pc`. Configure the platform's runtime
library search path as required by the SDK installation location.

## Encode I420

```go
encoder, err := openh264.NewEncoder(openh264.Config{
    Width:     1920,
    Height:    1080,
    QP:        26,
    FrameRate: 30,
})
if err != nil {
    return err
}
defer encoder.Close()

encoded, err := encoder.Encode(openh264.I420Frame{
    Y:           y,
    U:           u,
    V:           v,
    YStride:     1920,
    UStride:      960,
    VStride:      960,
    Timestamp: timestampMS,
})
if err != nil {
    return err
}
send(encoded.Data)
```

Zero strides select tightly packed planes. The input slices are borrowed only
for the duration of `Encode`; the returned Annex B bitstream is copied into
Go-owned memory.

The RDP hint algorithms require fixed-QP coding, so `Config.QP` replaces a
target-bitrate setting. It accepts H.264 QP values from 0 through 51; lower
values preserve more detail and generally produce larger frames.

Width and height must be even and at least 16 pixels, with no more than 36,864
aligned 16x16 macroblocks per frame. `FrameRate` accepts 1 through 60 fps.
`IDRInterval` accepts 0 through `math.MaxInt32`; zero disables periodic IDR
frames after the initial frame.

## Macroblock hints

Hints are row-major and cover the coded dimensions in 16x16 blocks. Each
non-empty hint slice must have exactly `MacroblockCount(width, height)` items:

```go
count := openh264.MacroblockCount(width, height)
frame.Hints.Unchanged = make([]byte, count)
frame.Hints.Motion = make([]openh264.MotionVector, count)
```

A nonzero `Unchanged` value certifies that all source samples in the block are
unchanged from the preceding successfully encoded frame. A nonzero motion
vector supplies a full-pixel luma displacement. Incorrect motion suggestions
may cost bits, but residuals are still encoded.

Do not modify any input or hint slice concurrently with `Encode`.

## Errors and lifecycle

`NewEncoder` verifies RDP ABI version 1 and both hint capabilities. It returns
`ErrIncompatibleABI` when the installed native package does not match.

An `Encoder` serializes native operations and may be used by multiple
goroutines. `Close` is idempotent. Calling `Encode` or `ForceIDR` after closing
returns `ErrClosed`. If a frame is encoded but its output cannot be delivered,
the bridge ignores hints and forces the next successfully delivered frame to
be an IDR frame so caller and encoder reference state cannot diverge.

With `CGO_ENABLED=0`, the package still compiles, but `NewEncoder` returns
`ErrCGODisabled`.

## Test

Run validation and API tests without a native SDK:

```sh
CGO_ENABLED=0 go test ./...
```

Run the native integration test after installing openh264-rdp:

```sh
go test -tags integration ./...
go test -race -tags integration ./...
```

## License

See [LICENSE](LICENSE).
