#include <gtest/gtest.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "cpu_core.h"
#include "cpu.h"
#include "macros.h"
#include "wels_func_ptr_def.h"
#include "../../codec/encoder/core/src/encoder.cpp"

using namespace WelsEnc;
#define MEMORYZEROTEST_NUM 1000

TEST (SetMemZeroFunTest, WelsSetMemZero) {
  int32_t iLen = 64;
  int32_t iCpuCores = 0;
  SWelsFuncPtrList sFuncPtrList;
  uint32_t uiCpuFlag = WelsCPUFeatureDetect (&iCpuCores);
  /* Functionality utilization of CPU instructions dependency */
  sFuncPtrList.pfSetMemZeroSize8           = WelsSetMemZero_c; // confirmed_safe_unsafe_usage
  sFuncPtrList.pfSetMemZeroSize64Aligned16 = WelsSetMemZero_c; // confirmed_safe_unsafe_usage
  sFuncPtrList.pfSetMemZeroSize64          = WelsSetMemZero_c; // confirmed_safe_unsafe_usage
#if defined(X86_ASM)
  if (uiCpuFlag & WELS_CPU_MMXEXT) {
    sFuncPtrList.pfSetMemZeroSize8           = WelsSetMemZeroSize8_mmx;  // confirmed_safe_unsafe_usage
    sFuncPtrList.pfSetMemZeroSize64Aligned16 = WelsSetMemZeroSize64_mmx; // confirmed_safe_unsafe_usage
    sFuncPtrList.pfSetMemZeroSize64          = WelsSetMemZeroSize64_mmx; // confirmed_safe_unsafe_usage
  }
  if (uiCpuFlag & WELS_CPU_SSE2) {
    sFuncPtrList.pfSetMemZeroSize8           = WelsSetMemZeroSize8_sse2;     // confirmed_safe_unsafe_usage
    sFuncPtrList.pfSetMemZeroSize64Aligned16 = WelsSetMemZeroAligned64_sse2; // confirmed_safe_unsafe_usage
    sFuncPtrList.pfSetMemZeroSize64          = WelsSetMemZeroSize64_sse2;    // confirmed_safe_unsafe_usage
  }
#else
  (void) uiCpuFlag; // Avoid warnings if no assembly is enabled
#endif//X86_ASM

#if defined(HAVE_NEON)
  if (uiCpuFlag & WELS_CPU_NEON) {
    sFuncPtrList.pfSetMemZeroSize8           = WelsSetMemZero_neon;
    sFuncPtrList.pfSetMemZeroSize64Aligned16 = WelsSetMemZero_neon;
    sFuncPtrList.pfSetMemZeroSize64          = WelsSetMemZero_neon;
  }
#endif

#if defined(HAVE_NEON_AARCH64) && defined(__aarch64__)
  if (uiCpuFlag & WELS_CPU_NEON) {
    sFuncPtrList.pfSetMemZeroSize8           = WelsSetMemZero_AArch64_neon;
    sFuncPtrList.pfSetMemZeroSize64Aligned16 = WelsSetMemZero_AArch64_neon;
    sFuncPtrList.pfSetMemZeroSize64          = WelsSetMemZero_AArch64_neon;
  }
#endif

  ENFORCE_STACK_ALIGN_2D (uint8_t, pInputAlign, 2, 64 * 101, 16)

  for (int32_t k = 0; k < MEMORYZEROTEST_NUM; k++) {
    memset (pInputAlign[0], 255, 64 * 101);
    memset (pInputAlign[1], 255, 64 * 101);
    iLen = 64 * (1 + (rand() % 100));
    WelsSetMemZero_c (pInputAlign[0], iLen);
    sFuncPtrList.pfSetMemZeroSize64Aligned16 (pInputAlign[1], iLen);
    for (int32_t i = 0 ; i < 64 * 101; i++) {
      ASSERT_EQ (pInputAlign[0][i], pInputAlign[1][i]);
    }
  }

  for (int32_t k = 0; k < MEMORYZEROTEST_NUM; k++) {
    memset (pInputAlign[0], 255, 64 * 101);
    memset (pInputAlign[1], 255, 64 * 101);
    iLen = 64 * (1 + (rand() % 100));
    WelsSetMemZero_c (pInputAlign[0] + 1, iLen);
    sFuncPtrList.pfSetMemZeroSize64 (pInputAlign[1] + 1, iLen);
    for (int32_t i = 0 ; i < 64 * 101; i++) {
      ASSERT_EQ (pInputAlign[0][i], pInputAlign[1][i]);
    }
  }

  memset (pInputAlign[0], 255, 64 * 101);
  memset (pInputAlign[1], 255, 64 * 101);
  iLen = 32;
  WelsSetMemZero_c (pInputAlign[0] + 1, iLen);
  sFuncPtrList.pfSetMemZeroSize8 (pInputAlign[1] + 1, iLen);
  for (int32_t i = 0 ; i < 64 * 101; i++) {
    ASSERT_EQ (pInputAlign[0][i], pInputAlign[1][i]);
  }

  memset (pInputAlign[0], 255, 64 * 101);
  memset (pInputAlign[1], 255, 64 * 101);
  iLen = 24;
  WelsSetMemZero_c (pInputAlign[0] + 1, iLen);
  sFuncPtrList.pfSetMemZeroSize8 (pInputAlign[1] + 1, iLen);
  for (int32_t i = 0 ; i < 64 * 101; i++) {
    ASSERT_EQ (pInputAlign[0][i], pInputAlign[1][i]);
  }
}



#if defined(X86_ASM)
// Every x86 variant against the C version, at unaligned offsets.
TEST (SetMemZeroFunTest, WelsSetMemZeroX86) {
  const uint32_t kuiCpuFlag = WelsCPUFeatureDetect (0);
  struct {
    PSetMemoryZero pfZero;
    int32_t iGranule;
    uint32_t uiCpu;
  } kFuncs[] = {
    {WelsSetMemZeroSize8_mmx, 8, WELS_CPU_MMXEXT},
    {WelsSetMemZeroSize64_mmx, 64, WELS_CPU_MMXEXT},
    {WelsSetMemZeroSize8_sse2, 8, WELS_CPU_SSE2},
    {WelsSetMemZeroSize64_sse2, 64, WELS_CPU_SSE2},
  };
  ENFORCE_STACK_ALIGN_2D (uint8_t, pBuf, 2, 64 * 102, 16)
  for (size_t f = 0; f < sizeof (kFuncs) / sizeof (kFuncs[0]); f++) {
    if (! (kuiCpuFlag & kFuncs[f].uiCpu))
      continue;
    for (int32_t k = 0; k < MEMORYZEROTEST_NUM; k++) {
      memset (pBuf[0], 255, 64 * 102);
      memset (pBuf[1], 255, 64 * 102);
      const int32_t kiLen = kFuncs[f].iGranule * (1 + (rand() % 100));
      const int32_t kiOffset = rand() % 16;
      WelsSetMemZero_c (pBuf[0] + kiOffset, kiLen);
      kFuncs[f].pfZero (pBuf[1] + kiOffset, kiLen);
      ASSERT_EQ (0, memcmp (pBuf[0], pBuf[1], 64 * 102)) << "function " << f << " length " << kiLen;
    }
  }
}
#endif
