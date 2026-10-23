//go:build cgo && !openh264_shared

package native

/*
#cgo pkg-config: --static openh264-rdp-static
*/
import "C"
