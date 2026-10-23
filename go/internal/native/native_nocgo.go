//go:build !cgo

package native

// Available reports whether the native implementation was compiled in.
const Available = false

// NewEncoder reports that cgo support is unavailable.
func NewEncoder(Config) (*Encoder, int) {
	return nil, StatusCGODisabled
}

// Encode reports that cgo support is unavailable.
func (*Encoder) Encode(Picture) (Encoded, int) {
	return Encoded{}, StatusCGODisabled
}

// ForceIDR reports that cgo support is unavailable.
func (*Encoder) ForceIDR() int {
	return StatusCGODisabled
}

// Close is a no-op when cgo support is unavailable.
func (*Encoder) Close() int {
	return StatusOK
}
