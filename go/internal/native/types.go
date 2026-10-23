package native

import "unsafe"

// Bridge-specific status values are negative so they cannot collide with the
// non-negative CM_RETURN values from OpenH264.
const (
	StatusOK            = 0
	StatusInitParameter = 1

	StatusInvalidArgument = -10000
	StatusOutOfMemory     = -10001
	StatusABIMismatch     = -10002
	StatusCorruptOutput   = -10003
	StatusClosed          = -10004
	StatusCGODisabled     = -10005
	StatusNativeException = -10006
)

// Config is the validated native encoder configuration.
type Config struct {
	Width       int32
	Height      int32
	QP          int32
	FrameRate   float32
	IDRInterval uint32
}

// Picture describes borrowed input buffers for one native call.
type Picture struct {
	Y unsafe.Pointer
	U unsafe.Pointer
	V unsafe.Pointer

	YStride int32
	UStride int32
	VStride int32

	Timestamp int64

	Unchanged      unsafe.Pointer
	UnchangedCount int32
	Motion         unsafe.Pointer
	MotionCount    int32
}

// Encoded is copied into Go-owned storage before Encode returns.
type Encoded struct {
	Data      []byte
	FrameType int32
	Timestamp int64
}

// Encoder stores the opaque C++ bridge object.
type Encoder struct {
	pointer unsafe.Pointer
}
