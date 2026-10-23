package openh264

import (
	"errors"
	"fmt"
	"runtime"
	"sync"

	"github.com/define42/openh264/go/internal/native"
)

// Encoder wraps one native OpenH264 encoder. Its methods serialize access to
// the native instance. An Encoder must not be copied after first use. Call
// Close when the encoder is no longer needed.
type Encoder struct {
	mu     sync.Mutex
	native *native.Encoder
	width  int
	height int
}

// NewEncoder creates an RDP-tuned I420 encoder. It verifies the native fork's
// ABI and required macroblock-hint capabilities before allocating the encoder.
func NewEncoder(config Config) (*Encoder, error) {
	if !native.Available {
		return nil, ErrCGODisabled
	}

	nativeConfig, err := validateConfig(config)
	if err != nil {
		return nil, err
	}

	nativeEncoder, status := native.NewEncoder(nativeConfig)
	if status != native.StatusOK {
		return nil, statusError("create encoder", status, ErrInvalidConfig)
	}

	return &Encoder{
		native: nativeEncoder,
		width:  config.Width,
		height: config.Height,
	}, nil
}

// Encode encodes one I420 picture. All input buffers are borrowed only during
// the call; the returned bitstream is an independent Go-owned copy.
func (encoder *Encoder) Encode(frame I420Frame) (EncodedFrame, error) {
	if encoder == nil {
		return EncodedFrame{}, ErrClosed
	}

	encoder.mu.Lock()
	defer encoder.mu.Unlock()
	if encoder.native == nil {
		return EncodedFrame{}, ErrClosed
	}

	validated, err := validateFrame(frame, encoder.width, encoder.height)
	if err != nil {
		return EncodedFrame{}, err
	}

	encoded, status := encoder.native.Encode(frameForNative(frame, validated))
	// The native call borrows all frame storage, including the hint slices.
	// Keep the complete value live until after it returns.
	runtime.KeepAlive(frame)
	if status != native.StatusOK {
		return EncodedFrame{}, statusError("encode frame", status, ErrInvalidFrame)
	}

	return EncodedFrame{
		Data:      encoded.Data,
		Type:      FrameType(encoded.FrameType),
		Timestamp: encoded.Timestamp,
	}, nil
}

// ForceIDR makes the next encoded picture an IDR frame.
func (encoder *Encoder) ForceIDR() error {
	if encoder == nil {
		return ErrClosed
	}

	encoder.mu.Lock()
	defer encoder.mu.Unlock()
	if encoder.native == nil {
		return ErrClosed
	}

	status := encoder.native.ForceIDR()
	if status != native.StatusOK {
		return statusError("force IDR frame", status, nil)
	}
	return nil
}

// Close releases the native encoder. It is safe to call Close more than once.
func (encoder *Encoder) Close() error {
	if encoder == nil {
		return nil
	}

	encoder.mu.Lock()
	defer encoder.mu.Unlock()
	if encoder.native == nil {
		return nil
	}

	status := encoder.native.Close()
	encoder.native = nil
	if status != native.StatusOK {
		return statusError("close encoder", status, nil)
	}
	return nil
}

func statusError(operation string, status int, invalid error) error {
	switch status {
	case native.StatusCGODisabled:
		return ErrCGODisabled
	case native.StatusClosed:
		return ErrClosed
	case native.StatusABIMismatch:
		return fmt.Errorf("%w: require ABI %d with macroblock hint capabilities", ErrIncompatibleABI, RDPABIVersion)
	case native.StatusInvalidArgument:
		if invalid != nil {
			return fmt.Errorf("%w: native bridge rejected arguments", invalid)
		}
	case native.StatusInitParameter:
		if errors.Is(invalid, ErrInvalidConfig) {
			return fmt.Errorf("%w: native codec rejected parameters", invalid)
		}
	}

	return &codecError{operation: operation, status: status}
}
