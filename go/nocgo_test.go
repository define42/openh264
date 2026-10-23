//go:build !cgo

package openh264

import (
	"errors"
	"testing"
)

func TestNewEncoderWithoutCGO(t *testing.T) {
	t.Parallel()

	encoder, err := NewEncoder(Config{
		Width:     32,
		Height:    32,
		QP:        26,
		FrameRate: 30,
	})
	if encoder != nil {
		t.Fatal("NewEncoder() returned a non-nil encoder without cgo")
	}
	if !errors.Is(err, ErrCGODisabled) {
		t.Fatalf("NewEncoder() error = %v, want ErrCGODisabled", err)
	}
}
