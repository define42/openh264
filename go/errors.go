package openh264

import (
	"errors"
	"fmt"
)

var (
	// ErrCGODisabled is returned when the package was built without cgo.
	ErrCGODisabled = errors.New("openh264: cgo is disabled")

	// ErrClosed is returned when an operation requires a live encoder.
	ErrClosed = errors.New("openh264: encoder is closed")

	// ErrIncompatibleABI means the loaded library is not the expected RDP fork.
	ErrIncompatibleABI = errors.New("openh264: incompatible openh264-rdp ABI")

	// ErrInvalidConfig means an encoder configuration is invalid.
	ErrInvalidConfig = errors.New("openh264: invalid encoder configuration")

	// ErrInvalidFrame means an input picture or its hints are invalid.
	ErrInvalidFrame = errors.New("openh264: invalid I420 frame")
)

type codecError struct {
	operation string
	status    int
}

func (e *codecError) Error() string {
	return fmt.Sprintf("openh264: %s failed with native status %d", e.operation, e.status)
}
