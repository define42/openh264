package openh264_test

import (
	"fmt"
	"log"

	openh264 "github.com/define42/openh264/go"
)

func ExampleMacroblockCount() {
	fmt.Println(openh264.MacroblockCount(1920, 1080))
	// Output: 8160
}

func ExampleEncoder() {
	const width, height = 1280, 720
	encoder, err := openh264.NewEncoder(openh264.Config{
		Width:     width,
		Height:    height,
		QP:        26,
		FrameRate: 30,
	})
	if err != nil {
		log.Print(err)
		return
	}
	defer encoder.Close()

	frame := openh264.I420Frame{
		Y: make([]byte, width*height),
		U: make([]byte, width*height/4),
		V: make([]byte, width*height/4),
	}
	encoded, err := encoder.Encode(frame)
	if err != nil {
		log.Print(err)
		return
	}
	_ = encoded.Data
}
