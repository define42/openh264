//go:build cgo

package native

/*
#cgo CXXFLAGS: -std=c++11
#include "bridge.h"
*/
import "C"

import "unsafe"

// Available reports whether the native implementation was compiled in.
const Available = true

// NewEncoder creates a native encoder.
func NewEncoder(config Config) (*Encoder, int) {
	var pointer *C.OpenH264RDPBridgeEncoder
	status := C.openh264_rdp_bridge_encoder_create(
		C.int32_t(config.Width),
		C.int32_t(config.Height),
		C.int32_t(config.QP),
		C.float(config.FrameRate),
		C.uint32_t(config.IDRInterval),
		&pointer,
	)
	if status != StatusOK {
		return nil, int(status)
	}
	return &Encoder{pointer: unsafe.Pointer(pointer)}, StatusOK
}

// Encode invokes the encoder and copies its output before returning.
func (encoder *Encoder) Encode(picture Picture) (Encoded, int) {
	if encoder == nil || encoder.pointer == nil {
		return Encoded{}, StatusClosed
	}

	var output C.OpenH264RDPBridgeOutput
	status := C.openh264_rdp_bridge_encoder_encode(
		(*C.OpenH264RDPBridgeEncoder)(encoder.pointer),
		(*C.uint8_t)(picture.Y),
		C.int32_t(picture.YStride),
		(*C.uint8_t)(picture.U),
		C.int32_t(picture.UStride),
		(*C.uint8_t)(picture.V),
		C.int32_t(picture.VStride),
		C.int64_t(picture.Timestamp),
		(*C.uint8_t)(picture.Unchanged),
		C.int32_t(picture.UnchangedCount),
		(*C.OpenH264RDPBridgeMotionVector)(picture.Motion),
		C.int32_t(picture.MotionCount),
		&output,
	)
	defer C.openh264_rdp_bridge_output_free(&output)
	if status != StatusOK {
		return Encoded{}, int(status)
	}

	var data []byte
	if output.size != 0 {
		data = C.GoBytes(unsafe.Pointer(output.data), output.size)
	}
	return Encoded{
		Data:      data,
		FrameType: int32(output.frame_type),
		Timestamp: int64(output.timestamp),
	}, StatusOK
}

// ForceIDR requests an IDR frame from the native encoder.
func (encoder *Encoder) ForceIDR() int {
	if encoder == nil || encoder.pointer == nil {
		return StatusClosed
	}
	return int(C.openh264_rdp_bridge_encoder_force_idr(
		(*C.OpenH264RDPBridgeEncoder)(encoder.pointer),
	))
}

// Close releases the native encoder.
func (encoder *Encoder) Close() int {
	if encoder == nil || encoder.pointer == nil {
		return StatusOK
	}
	C.openh264_rdp_bridge_encoder_destroy(
		(*C.OpenH264RDPBridgeEncoder)(encoder.pointer),
	)
	encoder.pointer = nil
	return StatusOK
}
