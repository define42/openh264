package openh264

import (
	"errors"
	"math"
	"strings"
	"testing"
	"unsafe"

	"github.com/define42/openh264/go/internal/native"
)

func TestMacroblockCount(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name          string
		width, height int
		want          int
	}{
		{name: "invalid width", width: 0, height: 16, want: 0},
		{name: "invalid height", width: 16, height: -1, want: 0},
		{name: "one block", width: 16, height: 16, want: 1},
		{name: "round up", width: 17, height: 31, want: 4},
		{name: "full HD", width: 1920, height: 1080, want: 8160},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			t.Parallel()
			if got := MacroblockCount(test.width, test.height); got != test.want {
				t.Fatalf("MacroblockCount(%d, %d) = %d, want %d", test.width, test.height, got, test.want)
			}
		})
	}
}

func TestValidateConfig(t *testing.T) {
	t.Parallel()

	valid := Config{Width: 1920, Height: 1080, QP: 26, FrameRate: 30}
	tests := []struct {
		name   string
		mutate func(*Config)
	}{
		{name: "zero width", mutate: func(config *Config) { config.Width = 0 }},
		{name: "width below minimum", mutate: func(config *Config) { config.Width = 14 }},
		{name: "odd width", mutate: func(config *Config) { config.Width = 1919 }},
		{name: "zero height", mutate: func(config *Config) { config.Height = 0 }},
		{name: "height below minimum", mutate: func(config *Config) { config.Height = 14 }},
		{name: "odd height", mutate: func(config *Config) { config.Height = 1079 }},
		{name: "too many macroblocks", mutate: func(config *Config) {
			config.Width = 3074
			config.Height = 3072
		}},
		{name: "negative QP", mutate: func(config *Config) { config.QP = -1 }},
		{name: "QP above maximum", mutate: func(config *Config) { config.QP = 52 }},
		{name: "frame rate below minimum", mutate: func(config *Config) { config.FrameRate = 0.5 }},
		{name: "frame rate above maximum", mutate: func(config *Config) { config.FrameRate = 60.5 }},
		{name: "NaN frame rate", mutate: func(config *Config) { config.FrameRate = math.NaN() }},
		{name: "infinite frame rate", mutate: func(config *Config) { config.FrameRate = math.Inf(1) }},
		{name: "IDR interval above maximum", mutate: func(config *Config) {
			config.IDRInterval = math.MaxInt32 + 1
		}},
	}

	validBoundaries := []Config{
		{Width: 16, Height: 16, QP: 0, FrameRate: 1},
		{Width: 3072, Height: 3072, QP: 51, FrameRate: 60, IDRInterval: math.MaxInt32},
	}
	for _, config := range validBoundaries {
		if _, err := validateConfig(config); err != nil {
			t.Errorf("validateConfig(%+v): %v", config, err)
		}
	}
	if _, err := validateConfig(valid); err != nil {
		t.Fatalf("validateConfig(valid): %v", err)
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			t.Parallel()
			config := valid
			test.mutate(&config)
			if _, err := validateConfig(config); !errors.Is(err, ErrInvalidConfig) {
				t.Fatalf("validateConfig() error = %v, want ErrInvalidConfig", err)
			}
		})
	}
}

func TestValidateFrame(t *testing.T) {
	t.Parallel()

	const width, height = 32, 18
	valid := I420Frame{
		Y: make([]byte, width*height),
		U: make([]byte, width*height/4),
		V: make([]byte, width*height/4),
	}

	validated, err := validateFrame(valid, width, height)
	if err != nil {
		t.Fatalf("validateFrame(valid): %v", err)
	}
	if validated.yStride != width || validated.uStride != width/2 || validated.vStride != width/2 {
		t.Fatalf("default strides = %+v", validated)
	}

	tests := []struct {
		name   string
		mutate func(*I420Frame)
	}{
		{name: "short Y", mutate: func(frame *I420Frame) { frame.Y = frame.Y[:len(frame.Y)-1] }},
		{name: "short U", mutate: func(frame *I420Frame) { frame.U = frame.U[:len(frame.U)-1] }},
		{name: "short V", mutate: func(frame *I420Frame) { frame.V = frame.V[:len(frame.V)-1] }},
		{name: "small Y stride", mutate: func(frame *I420Frame) { frame.YStride = width - 1 }},
		{name: "small U stride", mutate: func(frame *I420Frame) { frame.UStride = width/2 - 1 }},
		{name: "wrong unchanged count", mutate: func(frame *I420Frame) { frame.Hints.Unchanged = []byte{1} }},
		{name: "wrong motion count", mutate: func(frame *I420Frame) { frame.Hints.Motion = []MotionVector{{}} }},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			t.Parallel()
			frame := valid
			test.mutate(&frame)
			if _, err := validateFrame(frame, width, height); !errors.Is(err, ErrInvalidFrame) {
				t.Fatalf("validateFrame() error = %v, want ErrInvalidFrame", err)
			}
		})
	}
}

func TestValidatePlaneNativeOffsetLimit(t *testing.T) {
	t.Parallel()

	const width, height = 32, 32
	const chromaWidth, chromaHeight = width / 2, height / 2
	tests := []struct {
		name               string
		stride             int
		width              int
		height             int
		wantNativeLimitErr bool
	}{
		{name: "Y maximum accepted", stride: (math.MaxInt32 - width) / (height - 1), width: width, height: height},
		{name: "Y first rejected", stride: (math.MaxInt32-width)/(height-1) + 1, width: width, height: height, wantNativeLimitErr: true},
		{name: "chroma maximum accepted", stride: (math.MaxInt32 - chromaWidth) / (chromaHeight - 1), width: chromaWidth, height: chromaHeight},
		{name: "chroma first rejected", stride: (math.MaxInt32-chromaWidth)/(chromaHeight-1) + 1, width: chromaWidth, height: chromaHeight, wantNativeLimitErr: true},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			t.Parallel()
			err := validatePlane("test", nil, test.stride, test.width, test.height)
			if !errors.Is(err, ErrInvalidFrame) {
				t.Fatalf("validatePlane() error = %v, want ErrInvalidFrame", err)
			}
			gotNativeLimitErr := strings.Contains(err.Error(), "native 32-bit offset limit")
			if gotNativeLimitErr != test.wantNativeLimitErr {
				t.Fatalf("validatePlane() error = %v, native limit error = %v, want %v", err, gotNativeLimitErr, test.wantNativeLimitErr)
			}
		})
	}

	if unsafe.Sizeof(int(0)) > 4 {
		strideAboveInt32 := int(int64(math.MaxInt32) + 1)
		err := validatePlane("test", nil, strideAboveInt32, width, height)
		if !errors.Is(err, ErrInvalidFrame) || !strings.Contains(err.Error(), "stride must be") {
			t.Fatalf("validatePlane(stride above int32) error = %v, want stride error", err)
		}
	}
}

func TestValidateFrameWithPaddedStridesAndHints(t *testing.T) {
	t.Parallel()

	const width, height = 32, 18
	const yStride, chromaStride = 40, 20
	macroblocks := MacroblockCount(width, height)
	frame := I420Frame{
		Y:       make([]byte, (height-1)*yStride+width),
		U:       make([]byte, (height/2-1)*chromaStride+width/2),
		V:       make([]byte, (height/2-1)*chromaStride+width/2),
		YStride: yStride,
		UStride: chromaStride,
		VStride: chromaStride,
		Hints: MacroblockHints{
			Unchanged: make([]byte, macroblocks),
			Motion:    make([]MotionVector, macroblocks),
		},
	}

	if _, err := validateFrame(frame, width, height); err != nil {
		t.Fatalf("validateFrame(padded): %v", err)
	}
}

func TestNativeBridgeRejectsPlaneSpanAboveInt32(t *testing.T) {
	if !native.Available {
		t.Skip("cgo is disabled")
	}

	encoder, status := native.NewEncoder(native.Config{
		Width: 32, Height: 32, QP: 26, FrameRate: 30,
	})
	if status != native.StatusOK {
		t.Fatalf("native.NewEncoder() status = %d, want 0", status)
	}
	defer func() {
		if status := encoder.Close(); status != native.StatusOK {
			t.Errorf("native Encoder.Close() status = %d, want 0", status)
		}
	}()

	pixel := []byte{0}
	pointer := unsafe.Pointer(&pixel[0])
	valid := native.Picture{
		Y: pointer, U: pointer, V: pointer,
		YStride: 32, UStride: 16, VStride: 16,
	}
	tests := []struct {
		name   string
		mutate func(*native.Picture)
	}{
		{name: "Y", mutate: func(picture *native.Picture) { picture.YStride = 69273666 }},
		{name: "U", mutate: func(picture *native.Picture) { picture.UStride = 143165576 }},
		{name: "V", mutate: func(picture *native.Picture) { picture.VStride = 143165576 }},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			picture := valid
			test.mutate(&picture)
			if _, status := encoder.Encode(picture); status != native.StatusInvalidArgument {
				t.Fatalf("native Encoder.Encode() status = %d, want %d", status, native.StatusInvalidArgument)
			}
		})
	}
}

func TestMotionVectorLayout(t *testing.T) {
	t.Parallel()

	var vector MotionVector
	if got := unsafe.Sizeof(vector); got != 4 {
		t.Fatalf("sizeof(MotionVector) = %d, want 4", got)
	}
	if got := unsafe.Offsetof(vector.Y); got != 2 {
		t.Fatalf("offsetof(MotionVector.Y) = %d, want 2", got)
	}
}

func TestFrameTypeString(t *testing.T) {
	t.Parallel()

	tests := []struct {
		frameType FrameType
		want      string
	}{
		{frameType: FrameTypeInvalid, want: "invalid"},
		{frameType: FrameTypeIDR, want: "IDR"},
		{frameType: FrameTypeI, want: "I"},
		{frameType: FrameTypeP, want: "P"},
		{frameType: FrameTypeSkip, want: "skip"},
		{frameType: FrameTypeIPMixed, want: "I/P mixed"},
		{frameType: FrameType(255), want: "invalid"},
	}

	for _, test := range tests {
		if got := test.frameType.String(); got != test.want {
			t.Errorf("FrameType(%d).String() = %q, want %q", test.frameType, got, test.want)
		}
	}
}

func TestStatusErrorClassification(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name              string
		status            int
		wantInvalidConfig bool
	}{
		{name: "invalid native parameter", status: native.StatusInitParameter, wantInvalidConfig: true},
		{name: "bridge out of memory", status: native.StatusOutOfMemory},
		{name: "native C++ exception", status: native.StatusNativeException},
		{name: "codec out of memory", status: 3},
		{name: "unknown codec failure", status: 99},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			t.Parallel()
			err := statusError("create encoder", test.status, ErrInvalidConfig)
			if got := errors.Is(err, ErrInvalidConfig); got != test.wantInvalidConfig {
				t.Fatalf("errors.Is(%v, ErrInvalidConfig) = %v, want %v", err, got, test.wantInvalidConfig)
			}
		})
	}
}

func TestEncodeClosedBeforeFrameValidation(t *testing.T) {
	t.Parallel()

	encoder := &Encoder{width: 32, height: 32}
	_, err := encoder.Encode(I420Frame{})
	if !errors.Is(err, ErrClosed) {
		t.Fatalf("Encode(invalid frame on closed encoder) error = %v, want ErrClosed", err)
	}
}
