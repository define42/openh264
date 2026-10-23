#ifndef OPENH264_RDP_GO_BRIDGE_H
#define OPENH264_RDP_GO_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  OPENH264_RDP_BRIDGE_INVALID_ARGUMENT = -10000,
  OPENH264_RDP_BRIDGE_OUT_OF_MEMORY = -10001,
  OPENH264_RDP_BRIDGE_ABI_MISMATCH = -10002,
  OPENH264_RDP_BRIDGE_CORRUPT_OUTPUT = -10003,
  OPENH264_RDP_BRIDGE_CLOSED = -10004,
  OPENH264_RDP_BRIDGE_NATIVE_EXCEPTION = -10006
};

typedef struct OpenH264RDPBridgeEncoder OpenH264RDPBridgeEncoder;

typedef struct {
  int16_t x;
  int16_t y;
} OpenH264RDPBridgeMotionVector;

typedef struct {
  uint8_t* data;
  int32_t size;
  int32_t frame_type;
  int64_t timestamp;
} OpenH264RDPBridgeOutput;

int openh264_rdp_bridge_encoder_create(int32_t width,
                                       int32_t height,
                                       int32_t qp,
                                       float frame_rate,
                                       uint32_t idr_interval,
                                       OpenH264RDPBridgeEncoder** output);

void openh264_rdp_bridge_encoder_destroy(OpenH264RDPBridgeEncoder* encoder);

int openh264_rdp_bridge_encoder_force_idr(OpenH264RDPBridgeEncoder* encoder);

int openh264_rdp_bridge_encoder_encode(
    OpenH264RDPBridgeEncoder* encoder,
    const uint8_t* y,
    int32_t y_stride,
    const uint8_t* u,
    int32_t u_stride,
    const uint8_t* v,
    int32_t v_stride,
    int64_t timestamp,
    const uint8_t* unchanged,
    int32_t unchanged_count,
    const OpenH264RDPBridgeMotionVector* motion,
    int32_t motion_count,
    OpenH264RDPBridgeOutput* output);

void openh264_rdp_bridge_output_free(OpenH264RDPBridgeOutput* output);

#ifdef __cplusplus
}
#endif

#endif
