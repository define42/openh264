//go:build integration && cgo

package openh264

import (
	"bytes"
	"testing"
)

func TestEncoderIntegration(t *testing.T) {
	const width, height = 32, 32
	encoder, err := NewEncoder(Config{
		Width:       width,
		Height:      height,
		QP:          26,
		FrameRate:   30,
		IDRInterval: 0,
	})
	if err != nil {
		t.Fatalf("NewEncoder(): %v", err)
	}
	t.Cleanup(func() {
		if err := encoder.Close(); err != nil {
			t.Errorf("Close(): %v", err)
		}
	})

	frame := I420Frame{
		Y:         make([]byte, width*height),
		U:         make([]byte, width*height/4),
		V:         make([]byte, width*height/4),
		Timestamp: 42,
	}
	for index := range frame.Y {
		frame.Y[index] = 16
	}
	for index := range frame.U {
		frame.U[index] = 128
		frame.V[index] = 128
	}

	encoded, err := encoder.Encode(frame)
	if err != nil {
		t.Fatalf("Encode(first frame): %v", err)
	}
	if len(encoded.Data) == 0 {
		t.Fatal("Encode(first frame) returned an empty bitstream")
	}
	if encoded.Type != FrameTypeIDR {
		t.Fatalf("Encode(first frame) type = %v, want %v", encoded.Type, FrameTypeIDR)
	}
	if encoded.Timestamp != frame.Timestamp {
		t.Fatalf("Encode(first frame) timestamp = %d, want %d", encoded.Timestamp, frame.Timestamp)
	}

	frame.Timestamp++
	if _, err := encoder.Encode(frame); err != nil {
		t.Fatalf("Encode(warm-up P frame): %v", err)
	}

	frame.Timestamp++
	frame.Hints.Unchanged = make([]byte, MacroblockCount(width, height))
	for index := range frame.Hints.Unchanged {
		frame.Hints.Unchanged[index] = 1
	}
	if _, err := encoder.Encode(frame); err != nil {
		t.Fatalf("Encode(unchanged frame): %v", err)
	}

	frame.Timestamp++
	frame.Hints = MacroblockHints{
		Motion: make([]MotionVector, MacroblockCount(width, height)),
	}
	frame.Hints.Motion[0] = MotionVector{X: 1, Y: 1}
	if _, err := encoder.Encode(frame); err != nil {
		t.Fatalf("Encode(motion-hinted frame): %v", err)
	}

	if err := encoder.ForceIDR(); err != nil {
		t.Fatalf("ForceIDR(): %v", err)
	}
	frame.Timestamp++
	frame.Hints = MacroblockHints{}
	encoded, err = encoder.Encode(frame)
	if err != nil {
		t.Fatalf("Encode(forced IDR): %v", err)
	}
	if encoded.Type != FrameTypeIDR {
		t.Fatalf("Encode(forced IDR) type = %v, want %v", encoded.Type, FrameTypeIDR)
	}

	if err := encoder.Close(); err != nil {
		t.Fatalf("Close(first): %v", err)
	}
	if err := encoder.Close(); err != nil {
		t.Fatalf("Close(second): %v", err)
	}
}

func TestEncoderAcceptsMinimalPaddedPlanes(t *testing.T) {
	const width, height = 32, 18
	const yStride, chromaStride = 40, 20
	encoder, err := NewEncoder(Config{
		Width: width, Height: height, QP: 26, FrameRate: 30,
	})
	if err != nil {
		t.Fatalf("NewEncoder(): %v", err)
	}
	t.Cleanup(func() {
		if err := encoder.Close(); err != nil {
			t.Errorf("Close(): %v", err)
		}
	})

	frame := I420Frame{
		Y:       make([]byte, (height-1)*yStride+width),
		U:       make([]byte, (height/2-1)*chromaStride+width/2),
		V:       make([]byte, (height/2-1)*chromaStride+width/2),
		YStride: yStride,
		UStride: chromaStride,
		VStride: chromaStride,
	}
	for y := 0; y < height; y++ {
		for x := 0; x < width; x++ {
			frame.Y[y*yStride+x] = byte(x + y)
		}
	}
	for y := 0; y < height/2; y++ {
		for x := 0; x < width/2; x++ {
			frame.U[y*chromaStride+x] = 128
			frame.V[y*chromaStride+x] = 128
		}
	}

	encoded, err := encoder.Encode(frame)
	if err != nil {
		t.Fatalf("Encode(minimal padded planes): %v", err)
	}
	if len(encoded.Data) == 0 {
		t.Fatal("Encode(minimal padded planes) returned an empty bitstream")
	}
}

func TestMotionHintAffectsEncoding(t *testing.T) {
	const width, height = 64, 64
	config := Config{
		Width:     width,
		Height:    height,
		QP:        26,
		FrameRate: 30,
	}

	plain, err := NewEncoder(config)
	if err != nil {
		t.Fatalf("NewEncoder(plain): %v", err)
	}
	t.Cleanup(func() {
		if err := plain.Close(); err != nil {
			t.Errorf("Close(plain): %v", err)
		}
	})

	hinted, err := NewEncoder(config)
	if err != nil {
		t.Fatalf("NewEncoder(hinted): %v", err)
	}
	t.Cleanup(func() {
		if err := hinted.Close(); err != nil {
			t.Errorf("Close(hinted): %v", err)
		}
	})

	frame := I420Frame{
		Y: make([]byte, width*height),
		U: make([]byte, width*height/4),
		V: make([]byte, width*height/4),
	}
	for y := 0; y < height; y++ {
		for x := 0; x < width; x++ {
			frame.Y[y*width+x] = byte(x*37 + y*53 + x*y*3)
		}
	}
	for index := range frame.U {
		frame.U[index] = byte(index*29 + 17)
		frame.V[index] = byte((index+len(frame.U))*29 + 17)
	}

	if _, err := plain.Encode(frame); err != nil {
		t.Fatalf("plain.Encode(base): %v", err)
	}
	if _, err := hinted.Encode(frame); err != nil {
		t.Fatalf("hinted.Encode(base): %v", err)
	}

	frame.Timestamp = 1
	plainFrame, err := plain.Encode(frame)
	if err != nil {
		t.Fatalf("plain.Encode(P frame): %v", err)
	}

	frame.Hints.Motion = make([]MotionVector, MacroblockCount(width, height))
	for index := range frame.Hints.Motion {
		frame.Hints.Motion[index] = MotionVector{X: 3, Y: 2}
	}
	hintedFrame, err := hinted.Encode(frame)
	if err != nil {
		t.Fatalf("hinted.Encode(P frame): %v", err)
	}

	if plainFrame.Type != FrameTypeP || hintedFrame.Type != FrameTypeP {
		t.Fatalf("P-frame types = (%v, %v), want (%v, %v)",
			plainFrame.Type, hintedFrame.Type, FrameTypeP, FrameTypeP)
	}
	if bytes.Equal(plainFrame.Data, hintedFrame.Data) {
		t.Fatal("valid motion hints did not affect the encoded bitstream")
	}
}

func TestUnchangedHintAffectsEncoding(t *testing.T) {
	const width, height = 34, 18
	config := Config{
		Width:     width,
		Height:    height,
		QP:        26,
		FrameRate: 30,
	}

	newEncoder := func(name string) *Encoder {
		t.Helper()
		encoder, err := NewEncoder(config)
		if err != nil {
			t.Fatalf("NewEncoder(%s): %v", name, err)
		}
		t.Cleanup(func() {
			if err := encoder.Close(); err != nil {
				t.Errorf("Close(%s): %v", name, err)
			}
		})
		return encoder
	}
	plain := newEncoder("plain")
	hinted := newEncoder("hinted")

	frame := I420Frame{
		Y: make([]byte, width*height),
		U: make([]byte, width*height/4),
		V: make([]byte, width*height/4),
	}
	for index := range frame.Y {
		frame.Y[index] = 32
	}
	for index := range frame.U {
		frame.U[index] = 128
		frame.V[index] = 128
	}

	if _, err := plain.Encode(frame); err != nil {
		t.Fatalf("plain.Encode(IDR): %v", err)
	}
	if _, err := hinted.Encode(frame); err != nil {
		t.Fatalf("hinted.Encode(IDR): %v", err)
	}

	frame.Timestamp = 1
	plainWarmup, err := plain.Encode(frame)
	if err != nil {
		t.Fatalf("plain.Encode(warm-up P frame): %v", err)
	}
	hintedWarmup, err := hinted.Encode(frame)
	if err != nil {
		t.Fatalf("hinted.Encode(warm-up P frame): %v", err)
	}
	if plainWarmup.Type != FrameTypeP || hintedWarmup.Type != FrameTypeP {
		t.Fatalf("warm-up frame types = (%v, %v), want (%v, %v)",
			plainWarmup.Type, hintedWarmup.Type, FrameTypeP, FrameTypeP)
	}

	frame.Timestamp = 2
	for index := range frame.Y {
		frame.Y[index] = byte(index*31 + 47)
	}
	plainFrame, err := plain.Encode(frame)
	if err != nil {
		t.Fatalf("plain.Encode(changed P frame): %v", err)
	}

	// Deliberately claim that changed samples are unchanged so taking the
	// hint-only skip path has an observable bitstream effect.
	frame.Hints.Unchanged = make([]byte, MacroblockCount(width, height))
	for index := range frame.Hints.Unchanged {
		frame.Hints.Unchanged[index] = 1
	}
	hintedFrame, err := hinted.Encode(frame)
	if err != nil {
		t.Fatalf("hinted.Encode(changed P frame): %v", err)
	}
	if plainFrame.Type != FrameTypeP || hintedFrame.Type != FrameTypeP {
		t.Fatalf("hinted frame types = (%v, %v), want (%v, %v)",
			plainFrame.Type, hintedFrame.Type, FrameTypeP, FrameTypeP)
	}
	if bytes.Equal(plainFrame.Data, hintedFrame.Data) {
		t.Fatal("eligible unchanged hints did not affect the encoded bitstream")
	}
}
