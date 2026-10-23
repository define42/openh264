// Package openh264 provides an encoder-first Go binding for the
// openh264-rdp native library.
//
// The package accepts planar I420 pictures and exposes the RDP fork's
// unchanged-macroblock and motion-vector hints. Encoder calls are serialized;
// an Encoder may therefore be shared by goroutines, although frames are still
// encoded one at a time. Encoded data is copied into Go-owned memory before
// Encode returns.
//
// The native library is linked statically by default. Build with the
// openh264_shared tag to link the shared library instead.
package openh264
