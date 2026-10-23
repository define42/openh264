package openh264

import (
	"fmt"
	"math"
	"unsafe"

	"github.com/define42/openh264/go/internal/native"
)

// MotionVector is a full-pixel luma displacement for one macroblock.
type MotionVector struct {
	X int16
	Y int16
}

// MacroblockHints carries optional row-major hints, one item per 16x16
// macroblock. Unchanged values are zero for an unknown/changed block and
// nonzero when every source sample is unchanged from the preceding successful
// frame. Motion vectors suggest where a block occurred in that frame.
//
// Each non-empty slice must contain exactly MacroblockCount(width, height)
// items. The encoder borrows the slices only for the duration of Encode.
type MacroblockHints struct {
	Unchanged []byte
	Motion    []MotionVector
}

// I420Frame is a planar 4:2:0 picture. Zero strides select tightly packed
// planes. Timestamp is expressed in milliseconds.
type I420Frame struct {
	Y []byte
	U []byte
	V []byte

	YStride int
	UStride int
	VStride int

	Timestamp int64
	Hints     MacroblockHints
}

// FrameType identifies the picture type reported by the native encoder.
type FrameType uint8

const (
	// FrameTypeInvalid indicates that the encoder did not report a valid type.
	FrameTypeInvalid FrameType = iota
	// FrameTypeIDR is an instantaneous decoder refresh frame.
	FrameTypeIDR
	// FrameTypeI is an intra-coded frame.
	FrameTypeI
	// FrameTypeP is a predicted frame.
	FrameTypeP
	// FrameTypeSkip indicates that rate control skipped the frame.
	FrameTypeSkip
	// FrameTypeIPMixed contains both I and P slices.
	FrameTypeIPMixed
)

// String returns the OpenH264 name of the frame type.
func (frameType FrameType) String() string {
	switch frameType {
	case FrameTypeIDR:
		return "IDR"
	case FrameTypeI:
		return "I"
	case FrameTypeP:
		return "P"
	case FrameTypeSkip:
		return "skip"
	case FrameTypeIPMixed:
		return "I/P mixed"
	default:
		return "invalid"
	}
}

// EncodedFrame contains an Annex B byte stream in Go-owned memory. Data is
// empty when Type is FrameTypeSkip.
type EncodedFrame struct {
	Data      []byte
	Type      FrameType
	Timestamp int64
}

// MacroblockCount returns the number of 16x16 macroblocks needed to cover the
// dimensions. It returns zero for non-positive dimensions or integer overflow.
func MacroblockCount(width, height int) int {
	if width <= 0 || height <= 0 {
		return 0
	}

	columns := (width-1)/16 + 1
	rows := (height-1)/16 + 1
	if columns > int(^uint(0)>>1)/rows {
		return 0
	}
	return columns * rows
}

type validatedFrame struct {
	yStride int32
	uStride int32
	vStride int32
}

func validateFrame(frame I420Frame, width, height int) (validatedFrame, error) {
	yStride := frame.YStride
	if yStride == 0 {
		yStride = width
	}
	uStride := frame.UStride
	if uStride == 0 {
		uStride = width / 2
	}
	vStride := frame.VStride
	if vStride == 0 {
		vStride = width / 2
	}

	if err := validatePlane("Y", frame.Y, yStride, width, height); err != nil {
		return validatedFrame{}, err
	}
	if err := validatePlane("U", frame.U, uStride, width/2, height/2); err != nil {
		return validatedFrame{}, err
	}
	if err := validatePlane("V", frame.V, vStride, width/2, height/2); err != nil {
		return validatedFrame{}, err
	}

	macroblocks := MacroblockCount(width, height)
	if len(frame.Hints.Unchanged) != 0 && len(frame.Hints.Unchanged) != macroblocks {
		return validatedFrame{}, fmt.Errorf(
			"%w: unchanged hint count is %d, want %d",
			ErrInvalidFrame,
			len(frame.Hints.Unchanged),
			macroblocks,
		)
	}
	if len(frame.Hints.Motion) != 0 && len(frame.Hints.Motion) != macroblocks {
		return validatedFrame{}, fmt.Errorf(
			"%w: motion hint count is %d, want %d",
			ErrInvalidFrame,
			len(frame.Hints.Motion),
			macroblocks,
		)
	}

	return validatedFrame{
		yStride: int32(yStride),
		uStride: int32(uStride),
		vStride: int32(vStride),
	}, nil
}

func validatePlane(name string, data []byte, stride, width, height int) error {
	if width <= 0 || height <= 0 {
		return fmt.Errorf(
			"%w: %s plane dimensions must be positive",
			ErrInvalidFrame,
			name,
		)
	}
	if stride < width || stride > math.MaxInt32 {
		return fmt.Errorf(
			"%w: %s stride must be in [%d, %d]",
			ErrInvalidFrame,
			name,
			width,
			math.MaxInt32,
		)
	}

	lastRow := int64(height - 1)
	if lastRow > (int64(math.MaxInt32)-int64(width))/int64(stride) {
		return fmt.Errorf(
			"%w: %s plane span exceeds the native 32-bit offset limit",
			ErrInvalidFrame,
			name,
		)
	}
	required := int(lastRow*int64(stride) + int64(width))
	if len(data) < required {
		return fmt.Errorf(
			"%w: %s plane has %d bytes, need at least %d",
			ErrInvalidFrame,
			name,
			len(data),
			required,
		)
	}
	return nil
}

func frameForNative(frame I420Frame, validated validatedFrame) native.Picture {
	return native.Picture{
		Y:              bytePointer(frame.Y),
		U:              bytePointer(frame.U),
		V:              bytePointer(frame.V),
		YStride:        validated.yStride,
		UStride:        validated.uStride,
		VStride:        validated.vStride,
		Timestamp:      frame.Timestamp,
		Unchanged:      bytePointer(frame.Hints.Unchanged),
		UnchangedCount: int32(len(frame.Hints.Unchanged)),
		Motion:         motionPointer(frame.Hints.Motion),
		MotionCount:    int32(len(frame.Hints.Motion)),
	}
}

func bytePointer(data []byte) unsafe.Pointer {
	if len(data) == 0 {
		return nil
	}
	return unsafe.Pointer(unsafe.SliceData(data))
}

func motionPointer(motion []MotionVector) unsafe.Pointer {
	if len(motion) == 0 {
		return nil
	}
	return unsafe.Pointer(unsafe.SliceData(motion))
}
