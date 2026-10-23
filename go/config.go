package openh264

import (
	"fmt"
	"math"

	"github.com/define42/openh264/go/internal/native"
)

// RDPABIVersion is the native extension ABI required by this package.
const RDPABIVersion = 1

// Config controls an encoder. Width and Height must be even I420 dimensions
// of at least 16 pixels, and their aligned frame size must not exceed 36,864
// macroblocks. QP is the fixed H.264 quantization parameter in [0, 51], where
// lower values retain more detail. Fixed-QP coding is required for the RDP hint
// semantics. FrameRate must be in [1, 60] frames per second. IDRInterval is the
// number of frames between periodic IDR frames; it must not exceed MaxInt32,
// and zero disables periodic IDR frames after the initial frame.
type Config struct {
	Width       int
	Height      int
	QP          int
	FrameRate   float64
	IDRInterval uint32
}

func validateConfig(config Config) (native.Config, error) {
	if config.Width < 16 || config.Width > math.MaxInt32 {
		return native.Config{}, fmt.Errorf("%w: width must be in [16, %d]", ErrInvalidConfig, math.MaxInt32)
	}
	if config.Height < 16 || config.Height > math.MaxInt32 {
		return native.Config{}, fmt.Errorf("%w: height must be in [16, %d]", ErrInvalidConfig, math.MaxInt32)
	}
	if config.Width%2 != 0 || config.Height%2 != 0 {
		return native.Config{}, fmt.Errorf("%w: I420 width and height must be even", ErrInvalidConfig)
	}
	macroblocks := MacroblockCount(config.Width, config.Height)
	if macroblocks == 0 || macroblocks > 36864 {
		return native.Config{}, fmt.Errorf(
			"%w: aligned frame size is %d macroblocks, maximum is 36864",
			ErrInvalidConfig,
			macroblocks,
		)
	}
	if config.QP < 0 || config.QP > 51 {
		return native.Config{}, fmt.Errorf("%w: QP must be in [0, 51]", ErrInvalidConfig)
	}
	if math.IsNaN(config.FrameRate) || math.IsInf(config.FrameRate, 0) ||
		config.FrameRate < 1 || config.FrameRate > 60 {
		return native.Config{}, fmt.Errorf("%w: frame rate must be finite and in [1, 60]", ErrInvalidConfig)
	}
	if config.IDRInterval > math.MaxInt32 {
		return native.Config{}, fmt.Errorf(
			"%w: IDR interval must be at most %d",
			ErrInvalidConfig,
			math.MaxInt32,
		)
	}

	return native.Config{
		Width:       int32(config.Width),
		Height:      int32(config.Height),
		QP:          int32(config.QP),
		FrameRate:   float32(config.FrameRate),
		IDRInterval: config.IDRInterval,
	}, nil
}
