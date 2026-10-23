#include "bridge.h"

#include <wels/codec_api.h>

#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define OPENH264_RDP_EXCEPTIONS_ENABLED 1
#else
#define OPENH264_RDP_EXCEPTIONS_ENABLED 0
#endif

struct OpenH264RDPBridgeEncoder {
  ISVCEncoder* encoder;
  bool initialized;
  bool needs_idr;
  int32_t width;
  int32_t height;
};

static_assert(sizeof(OpenH264RDPBridgeMotionVector) == sizeof(SMacroblockMotion),
              "motion-vector size mismatch");
static_assert(offsetof(OpenH264RDPBridgeMotionVector, x) ==
                  offsetof(SMacroblockMotion, iX),
              "motion-vector X offset mismatch");
static_assert(offsetof(OpenH264RDPBridgeMotionVector, y) ==
                  offsetof(SMacroblockMotion, iY),
              "motion-vector Y offset mismatch");

namespace {

const unsigned int kExpectedRDPABIVersion = 1u;

int CheckRDPABI() {
  OpenH264RDPABIInfo info;
  std::memset(&info, 0, sizeof(info));
  info.uiStructSize = sizeof(info);
  if (WelsGetRDPABIInfo(&info) != 0) {
    return OPENH264_RDP_BRIDGE_ABI_MISMATCH;
  }

  const unsigned int required_capabilities =
      OPENH264_RDP_CAP_UNCHANGED_MACROBLOCKS |
      OPENH264_RDP_CAP_MACROBLOCK_MOTION;
  if (info.uiStructSize < sizeof(OpenH264RDPABIInfo) ||
      info.uiABIVersion != kExpectedRDPABIVersion ||
      (info.uiCapabilities & required_capabilities) != required_capabilities ||
      info.uiSourcePictureSize != sizeof(SSourcePicture)) {
    return OPENH264_RDP_BRIDGE_ABI_MISMATCH;
  }
  return 0;
}

void DestroyEncoder(OpenH264RDPBridgeEncoder* encoder) noexcept {
  if (encoder == NULL) {
    return;
  }
  if (encoder->encoder != NULL) {
    if (encoder->initialized) {
#if OPENH264_RDP_EXCEPTIONS_ENABLED
      try {
#endif
        encoder->encoder->Uninitialize();
#if OPENH264_RDP_EXCEPTIONS_ENABLED
      } catch (...) {
      }
#endif
    }
#if OPENH264_RDP_EXCEPTIONS_ENABLED
    try {
#endif
      WelsDestroyRDPSVCEncoder(encoder->encoder);
#if OPENH264_RDP_EXCEPTIONS_ENABLED
    } catch (...) {
    }
#endif
  }
  std::free(encoder);
}

class ScopedEncoder {
 public:
  explicit ScopedEncoder(OpenH264RDPBridgeEncoder* encoder)
      : encoder_(encoder) {}

  ~ScopedEncoder() {
    DestroyEncoder(encoder_);
  }

  OpenH264RDPBridgeEncoder* Release() {
    OpenH264RDPBridgeEncoder* encoder = encoder_;
    encoder_ = NULL;
    return encoder;
  }

 private:
  OpenH264RDPBridgeEncoder* encoder_;
};

bool PlaneSpanFitsInt32(int32_t stride, int32_t width, int32_t height) {
  if (width <= 0 || height <= 0 || stride < width) {
    return false;
  }
  const int64_t span =
      static_cast<int64_t>(height - 1) * stride + width;
  return span <= std::numeric_limits<int32_t>::max();
}

int EncodedSize(const SFrameBSInfo& info, size_t* size) {
  if (size == NULL || info.iLayerNum < 0 ||
      info.iLayerNum > MAX_LAYER_NUM_OF_FRAME) {
    return OPENH264_RDP_BRIDGE_CORRUPT_OUTPUT;
  }

  size_t total = 0;
  for (int layer_index = 0; layer_index < info.iLayerNum; ++layer_index) {
    const SLayerBSInfo& layer = info.sLayerInfo[layer_index];
    if (layer.iNalCount < 0 ||
        layer.iNalCount > MAX_NAL_UNITS_IN_LAYER ||
        (layer.iNalCount != 0 &&
         (layer.pNalLengthInByte == NULL || layer.pBsBuf == NULL))) {
      return OPENH264_RDP_BRIDGE_CORRUPT_OUTPUT;
    }
    for (int nal_index = 0; nal_index < layer.iNalCount; ++nal_index) {
      const int nal_size = layer.pNalLengthInByte[nal_index];
      if (nal_size < 0 ||
          total > std::numeric_limits<size_t>::max() -
                      static_cast<size_t>(nal_size)) {
        return OPENH264_RDP_BRIDGE_CORRUPT_OUTPUT;
      }
      total += static_cast<size_t>(nal_size);
    }
  }

  if (total > static_cast<size_t>(INT_MAX) ||
      info.iFrameSizeInBytes < 0 ||
      total != static_cast<size_t>(info.iFrameSizeInBytes)) {
    return OPENH264_RDP_BRIDGE_CORRUPT_OUTPUT;
  }
  *size = total;
  return 0;
}

}  // namespace

extern "C" int openh264_rdp_bridge_encoder_create(
    int32_t width,
    int32_t height,
    int32_t qp,
    float frame_rate,
    uint32_t idr_interval,
    OpenH264RDPBridgeEncoder** output)
#if OPENH264_RDP_EXCEPTIONS_ENABLED
try
#endif
{
  if (output == NULL) {
    return OPENH264_RDP_BRIDGE_INVALID_ARGUMENT;
  }
  *output = NULL;

  const int64_t macroblock_width =
      (static_cast<int64_t>(width) + 15) / 16;
  const int64_t macroblock_height =
      (static_cast<int64_t>(height) + 15) / 16;
  if (width < 16 || height < 16 ||
      (width & 1) != 0 || (height & 1) != 0 ||
      macroblock_width * macroblock_height > 36864 ||
      qp < 0 || qp > 51 ||
      !(frame_rate >= 1.0f && frame_rate <= 60.0f) ||
      idr_interval > static_cast<uint32_t>(INT_MAX)) {
    return OPENH264_RDP_BRIDGE_INVALID_ARGUMENT;
  }

  const int abi_status = CheckRDPABI();
  if (abi_status != 0) {
    return abi_status;
  }

  OpenH264RDPBridgeEncoder* bridge =
      static_cast<OpenH264RDPBridgeEncoder*>(
          std::calloc(1, sizeof(OpenH264RDPBridgeEncoder)));
  if (bridge == NULL) {
    return OPENH264_RDP_BRIDGE_OUT_OF_MEMORY;
  }
  bridge->width = width;
  bridge->height = height;
  ScopedEncoder bridge_guard(bridge);

  int status = WelsCreateRDPSVCEncoder(&bridge->encoder);
  if (status != 0 || bridge->encoder == NULL) {
    return status != 0 ? status : OPENH264_RDP_BRIDGE_OUT_OF_MEMORY;
  }

  SEncParamExt parameters;
  std::memset(&parameters, 0, sizeof(parameters));
  status = bridge->encoder->GetDefaultParams(&parameters);
  if (status != 0) {
    return status;
  }

  // The native hint algorithms require a fixed, single-reference coding
  // profile. Keep this in sync with UnchangedConfiguration in the codec.
  parameters.iUsageType = CAMERA_VIDEO_REAL_TIME;
  parameters.iPicWidth = width;
  parameters.iPicHeight = height;
  parameters.iTargetBitrate = 0;
  parameters.iRCMode = RC_OFF_MODE;
  parameters.iComplexityMode = LOW_COMPLEXITY;
  parameters.iNumRefFrame = 1;
  parameters.iEntropyCodingModeFlag = 0;
  parameters.iLoopFilterDisableIdc = 1;
  parameters.bEnableDenoise = false;
  parameters.bEnableAdaptiveQuant = false;
  parameters.bEnableBackgroundDetection = false;
  parameters.bEnableLongTermReference = false;
  parameters.bSimulcastAVC = false;
  parameters.bEnableSceneChangeDetect = false;
  parameters.bEnableFrameSkip = false;
  parameters.bEnableFrameCroppingFlag = true;
  parameters.iMultipleThreadIdc = 1;
  parameters.bUseLoadBalancing = false;
  parameters.fMaxFrameRate = frame_rate;
  parameters.iTemporalLayerNum = 1;
  parameters.iSpatialLayerNum = 1;
  parameters.uiIntraPeriod = idr_interval;
  parameters.sSpatialLayers[0].iVideoWidth = width;
  parameters.sSpatialLayers[0].iVideoHeight = height;
  parameters.sSpatialLayers[0].fFrameRate = frame_rate;
  parameters.sSpatialLayers[0].uiProfileIdc = PRO_BASELINE;
  parameters.sSpatialLayers[0].iDLayerQp = qp;
  parameters.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;

  status = bridge->encoder->InitializeExt(&parameters);
  if (status != 0) {
    return status;
  }
  bridge->initialized = true;

  int color_format = videoFormatI420;
  status = bridge->encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &color_format);
  if (status != 0) {
    return status;
  }

  *output = bridge_guard.Release();
  return 0;
}
#if OPENH264_RDP_EXCEPTIONS_ENABLED
catch (const std::bad_alloc&) {
  return OPENH264_RDP_BRIDGE_OUT_OF_MEMORY;
} catch (...) {
  return OPENH264_RDP_BRIDGE_NATIVE_EXCEPTION;
}
#endif

extern "C" void openh264_rdp_bridge_encoder_destroy(
    OpenH264RDPBridgeEncoder* encoder) {
  DestroyEncoder(encoder);
}

extern "C" int openh264_rdp_bridge_encoder_force_idr(
    OpenH264RDPBridgeEncoder* encoder)
#if OPENH264_RDP_EXCEPTIONS_ENABLED
try
#endif
{
  if (encoder == NULL || encoder->encoder == NULL || !encoder->initialized) {
    return OPENH264_RDP_BRIDGE_CLOSED;
  }
  return encoder->encoder->ForceIntraFrame(true, -1);
}
#if OPENH264_RDP_EXCEPTIONS_ENABLED
catch (const std::bad_alloc&) {
  return OPENH264_RDP_BRIDGE_OUT_OF_MEMORY;
} catch (...) {
  return OPENH264_RDP_BRIDGE_NATIVE_EXCEPTION;
}
#endif

extern "C" int openh264_rdp_bridge_encoder_encode(
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
    OpenH264RDPBridgeOutput* output)
#if OPENH264_RDP_EXCEPTIONS_ENABLED
try
#endif
{
  if (output == NULL) {
    return OPENH264_RDP_BRIDGE_INVALID_ARGUMENT;
  }
  std::memset(output, 0, sizeof(*output));
  if (encoder == NULL || encoder->encoder == NULL || !encoder->initialized) {
    return OPENH264_RDP_BRIDGE_CLOSED;
  }
  const int32_t macroblock_count =
      ((encoder->width + 15) / 16) * ((encoder->height + 15) / 16);
  if (y == NULL || u == NULL || v == NULL ||
      !PlaneSpanFitsInt32(y_stride, encoder->width, encoder->height) ||
      !PlaneSpanFitsInt32(
          u_stride, encoder->width / 2, encoder->height / 2) ||
      !PlaneSpanFitsInt32(
          v_stride, encoder->width / 2, encoder->height / 2) ||
      unchanged_count < 0 || motion_count < 0 ||
      (unchanged_count != 0 &&
       (unchanged == NULL || unchanged_count != macroblock_count)) ||
      (motion_count != 0 &&
       (motion == NULL || motion_count != macroblock_count))) {
    return OPENH264_RDP_BRIDGE_INVALID_ARGUMENT;
  }

  if (encoder->needs_idr) {
    const int recovery_status =
        encoder->encoder->ForceIntraFrame(true, -1);
    if (recovery_status != 0) {
      return recovery_status;
    }
    unchanged = NULL;
    unchanged_count = 0;
    motion = NULL;
    motion_count = 0;
  }

  SSourcePicture picture;
  std::memset(&picture, 0, sizeof(picture));
  picture.iColorFormat = videoFormatI420;
  picture.iStride[0] = y_stride;
  picture.iStride[1] = u_stride;
  picture.iStride[2] = v_stride;
  picture.pData[0] = const_cast<uint8_t*>(y);
  picture.pData[1] = const_cast<uint8_t*>(u);
  picture.pData[2] = const_cast<uint8_t*>(v);
  picture.iPicWidth = encoder->width;
  picture.iPicHeight = encoder->height;
  picture.uiTimeStamp = timestamp;
  picture.pUnchangedMacroblocks = unchanged;
  picture.iUnchangedMacroblockCount = unchanged_count;
  picture.pMacroblockMotion =
      reinterpret_cast<const SMacroblockMotion*>(motion);
  picture.iMacroblockMotionCount = motion_count;

  SFrameBSInfo info;
  std::memset(&info, 0, sizeof(info));
  const int status = encoder->encoder->EncodeFrame(&picture, &info);
  if (status != 0) {
    return status;
  }

  size_t encoded_size = 0;
  const int size_status = EncodedSize(info, &encoded_size);
  if (size_status != 0) {
    encoder->needs_idr = true;
    return size_status;
  }

  uint8_t* encoded = NULL;
  if (encoded_size != 0) {
    encoded = static_cast<uint8_t*>(std::malloc(encoded_size));
    if (encoded == NULL) {
      encoder->needs_idr = true;
      return OPENH264_RDP_BRIDGE_OUT_OF_MEMORY;
    }

    size_t offset = 0;
    for (int layer_index = 0; layer_index < info.iLayerNum; ++layer_index) {
      const SLayerBSInfo& layer = info.sLayerInfo[layer_index];
      size_t layer_size = 0;
      for (int nal_index = 0; nal_index < layer.iNalCount; ++nal_index) {
        layer_size += static_cast<size_t>(layer.pNalLengthInByte[nal_index]);
      }
      if (layer_size != 0) {
        std::memcpy(encoded + offset, layer.pBsBuf, layer_size);
        offset += layer_size;
      }
    }
  }

  encoder->needs_idr = false;
  output->data = encoded;
  output->size = static_cast<int32_t>(encoded_size);
  output->frame_type = static_cast<int32_t>(info.eFrameType);
  output->timestamp = info.uiTimeStamp;
  return 0;
}
#if OPENH264_RDP_EXCEPTIONS_ENABLED
catch (const std::bad_alloc&) {
  if (encoder != NULL) {
    encoder->needs_idr = true;
  }
  if (output != NULL) {
    std::memset(output, 0, sizeof(*output));
  }
  return OPENH264_RDP_BRIDGE_OUT_OF_MEMORY;
} catch (...) {
  if (encoder != NULL) {
    encoder->needs_idr = true;
  }
  if (output != NULL) {
    std::memset(output, 0, sizeof(*output));
  }
  return OPENH264_RDP_BRIDGE_NATIVE_EXCEPTION;
}
#endif

extern "C" void openh264_rdp_bridge_output_free(
    OpenH264RDPBridgeOutput* output) {
  if (output == NULL) {
    return;
  }
  std::free(output->data);
  std::memset(output, 0, sizeof(*output));
}
