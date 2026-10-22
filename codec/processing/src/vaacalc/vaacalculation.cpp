/*!
 * \copy
 *     Copyright (c)  2013, Cisco Systems
 *     All rights reserved.
 *
 *     Redistribution and use in source and binary forms, with or without
 *     modification, are permitted provided that the following conditions
 *     are met:
 *
 *        * Redistributions of source code must retain the above copyright
 *          notice, this list of conditions and the following disclaimer.
 *
 *        * Redistributions in binary form must reproduce the above copyright
 *          notice, this list of conditions and the following disclaimer in
 *          the documentation and/or other materials provided with the
 *          distribution.
 *
 *     THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *     "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *     LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *     FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *     COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *     INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *     BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *     LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *     CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *     LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *     ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *     POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <string.h>
#include "vaacalculation.h"
#include "cpu.h"

WELSVP_NAMESPACE_BEGIN


///////////////////////////////////////////////////////////////////////////////////////////////////////////////

CVAACalculation::CVAACalculation (int32_t iCpuFlag) {
  m_iCPUFlag = iCpuFlag;
  m_eMethod   = METHOD_VAA_STATISTICS;

  WelsMemset (&m_sCalcParam, 0, sizeof (m_sCalcParam));
  WelsMemset (&m_sVaaFuncs, 0, sizeof (m_sVaaFuncs));
  InitVaaFuncs (m_sVaaFuncs, m_iCPUFlag);
}

CVAACalculation::~CVAACalculation() {
}

void CVAACalculation::InitVaaFuncs (SVaaFuncs& sVaaFuncs, int32_t iCpuFlag) {
  sVaaFuncs.pfVAACalcSad         = VAACalcSad_c;
  sVaaFuncs.pfVAACalcSadBgd      = VAACalcSadBgd_c;
  sVaaFuncs.pfVAACalcSadSsd      = VAACalcSadSsd_c;
  sVaaFuncs.pfVAACalcSadSsdBgd   = VAACalcSadSsdBgd_c;
  sVaaFuncs.pfVAACalcSadVar      = VAACalcSadVar_c;
#ifdef X86_ASM
  if ((iCpuFlag & WELS_CPU_SSE2) == WELS_CPU_SSE2) {
    sVaaFuncs.pfVAACalcSad       = VAACalcSad_sse2;
    sVaaFuncs.pfVAACalcSadBgd    = VAACalcSadBgd_sse2;
    sVaaFuncs.pfVAACalcSadSsd    = VAACalcSadSsd_sse2;
    sVaaFuncs.pfVAACalcSadSsdBgd = VAACalcSadSsdBgd_sse2;
    sVaaFuncs.pfVAACalcSadVar    = VAACalcSadVar_sse2;
  }
#ifdef HAVE_AVX2
  if (iCpuFlag & WELS_CPU_AVX2) {
    sVaaFuncs.pfVAACalcSad       = VAACalcSad_avx2;
    sVaaFuncs.pfVAACalcSadBgd    = VAACalcSadBgd_avx2;
    sVaaFuncs.pfVAACalcSadSsd    = VAACalcSadSsd_avx2;
    sVaaFuncs.pfVAACalcSadSsdBgd = VAACalcSadSsdBgd_avx2;
    sVaaFuncs.pfVAACalcSadVar    = VAACalcSadVar_avx2;
  }
#endif
#endif//X86_ASM
#ifdef HAVE_NEON
  if ((iCpuFlag & WELS_CPU_NEON) == WELS_CPU_NEON) {
    sVaaFuncs.pfVAACalcSad       = VAACalcSad_neon;
    sVaaFuncs.pfVAACalcSadBgd    = VAACalcSadBgd_neon;
    sVaaFuncs.pfVAACalcSadSsd    = VAACalcSadSsd_neon;
    sVaaFuncs.pfVAACalcSadSsdBgd = VAACalcSadSsdBgd_neon;
    sVaaFuncs.pfVAACalcSadVar    = VAACalcSadVar_neon;
  }
#endif//HAVE_NEON

#if defined(HAVE_NEON_AARCH64) && defined(__aarch64__)
  if ((iCpuFlag & WELS_CPU_NEON) == WELS_CPU_NEON) {
    sVaaFuncs.pfVAACalcSad       = VAACalcSad_AArch64_neon;
    sVaaFuncs.pfVAACalcSadBgd    = VAACalcSadBgd_AArch64_neon;
    sVaaFuncs.pfVAACalcSadSsd    = VAACalcSadSsd_AArch64_neon;
    sVaaFuncs.pfVAACalcSadSsdBgd = VAACalcSadSsdBgd_AArch64_neon;
    sVaaFuncs.pfVAACalcSadVar    = VAACalcSadVar_AArch64_neon;
  }
#endif//HAVE_NEON_AARCH64

#ifdef HAVE_MMI
  if ((iCpuFlag & WELS_CPU_MMI) == WELS_CPU_MMI) {
    sVaaFuncs.pfVAACalcSad       = VAACalcSad_mmi;
    sVaaFuncs.pfVAACalcSadBgd    = VAACalcSadBgd_mmi;
    sVaaFuncs.pfVAACalcSadSsd    = VAACalcSadSsd_mmi;
    sVaaFuncs.pfVAACalcSadSsdBgd = VAACalcSadSsdBgd_mmi;
    sVaaFuncs.pfVAACalcSadVar    = VAACalcSadVar_mmi;
  }
#endif//HAVE_MMI

#ifdef HAVE_LSX
  if ((iCpuFlag & WELS_CPU_LSX) == WELS_CPU_LSX) {
    sVaaFuncs.pfVAACalcSad       = VAACalcSad_lsx;
    sVaaFuncs.pfVAACalcSadBgd    = VAACalcSadBgd_lsx;
  }
#endif//HAVE_LSX
#ifdef HAVE_LASX
  if ((iCpuFlag & WELS_CPU_LASX) == WELS_CPU_LASX) {
    sVaaFuncs.pfVAACalcSadBgd    = VAACalcSadBgd_lasx;
  }
#endif//HAVE_LASX
}

// Computes SADs over changed runs within each macroblock row. A valid hint
// certifies source equality, so skipped SADs are exactly zero.
static void VAACalcSadMasked (PVAACalcSadFunc pfCalc, const uint8_t* pCurData, const uint8_t* pRefData,
                              int32_t iPicWidth, int32_t iPicHeight, int32_t iPicStride, int32_t* pFrameSad,
                              int32_t* pSad8x8, const unsigned char* pUnchanged, int32_t iCount) {
  const int32_t kiMbWidth = iPicWidth >> 4;
  const int32_t kiMbHeight = iPicHeight >> 4;
  if (iPicWidth <= 0 || iPicHeight <= 0 || (iPicWidth & 15) || (iPicHeight & 15) || pUnchanged == NULL ||
      iCount != kiMbWidth * kiMbHeight) {
    pfCalc (pCurData, pRefData, iPicWidth, iPicHeight, iPicStride, pFrameSad, pSad8x8);
    return;
  }
  memset (pSad8x8, 0, (size_t)iCount * 4 * sizeof (int32_t));
  int32_t iTotal = 0;
  for (int32_t y = 0; y < kiMbHeight; ++y) {
    const int32_t kiRow = y * kiMbWidth;
    for (int32_t x = 0; x < kiMbWidth;) {
      if (pUnchanged[kiRow + x]) {
        ++x;
        continue;
      }
      const int32_t kiStart = x;
      while (x < kiMbWidth && !pUnchanged[kiRow + x])
        ++x;
      const int32_t kiOffset = y * 16 * iPicStride + kiStart * 16;
      int32_t iRunSad = 0;
      pfCalc (pCurData + kiOffset, pRefData + kiOffset, (x - kiStart) * 16, 16, iPicStride, &iRunSad,
              pSad8x8 + (kiRow + kiStart) * 4);
      iTotal += iRunSad;
    }
  }
  *pFrameSad = iTotal;
}

EResult CVAACalculation::Process (int32_t iType, SPixMap* pSrcPixMap, SPixMap* pRefPixMap) {
  const unsigned char* pUnchanged = m_sCalcParam.pUnchangedMacroblocks;
  const int32_t kiUnchangedCount = m_sCalcParam.iUnchangedMacroblockCount;
  // Never retain the caller-owned hints.
  m_sCalcParam.pUnchangedMacroblocks = NULL;
  m_sCalcParam.iUnchangedMacroblockCount = 0;
  uint8_t* pCurData     = (uint8_t*)pSrcPixMap->pPixel[0];
  uint8_t* pRefData     = (uint8_t*)pRefPixMap->pPixel[0];
  int32_t iPicWidth     = pSrcPixMap->sRect.iRectWidth;
  int32_t iPicHeight    = pSrcPixMap->sRect.iRectHeight;
  int32_t iPicStride    = pSrcPixMap->iStride[0];

  SVAACalcResult* pResult = m_sCalcParam.pCalcResult;

  if (pCurData == NULL || pRefData == NULL) {
    return RET_INVALIDPARAM;
  }

  pResult->pCurY = pCurData;
  pResult->pRefY = pRefData;
  if (m_sCalcParam.iCalcBgd) {
    if (m_sCalcParam.iCalcSsd) {
      m_sVaaFuncs.pfVAACalcSadSsdBgd (pCurData, pRefData, iPicWidth, iPicHeight, iPicStride, &pResult->iFrameSad,
                                      (int32_t*)pResult->pSad8x8, pResult->pSum16x16, pResult->pSumOfSquare16x16, pResult->pSsd16x16,
                                      (int32_t*)pResult->pSumOfDiff8x8, (uint8_t*)pResult->pMad8x8);
    } else {
      m_sVaaFuncs.pfVAACalcSadBgd (pCurData, pRefData, iPicWidth, iPicHeight, iPicStride, &pResult->iFrameSad,
                                   (int32_t*) (pResult->pSad8x8), (int32_t*) (pResult->pSumOfDiff8x8), (uint8_t*)pResult->pMad8x8);
    }
  } else {
    if (m_sCalcParam.iCalcSsd) {
      m_sVaaFuncs.pfVAACalcSadSsd (pCurData, pRefData, iPicWidth, iPicHeight, iPicStride, &pResult->iFrameSad,
                                   (int32_t*)pResult->pSad8x8, pResult->pSum16x16, pResult->pSumOfSquare16x16, pResult->pSsd16x16);
    } else {
      if (m_sCalcParam.iCalcVar) {
        m_sVaaFuncs.pfVAACalcSadVar (pCurData, pRefData, iPicWidth, iPicHeight, iPicStride, &pResult->iFrameSad,
                                     (int32_t*)pResult->pSad8x8, pResult->pSum16x16, pResult->pSumOfSquare16x16);
      } else {
        VAACalcSadMasked (m_sVaaFuncs.pfVAACalcSad, pCurData, pRefData, iPicWidth, iPicHeight, iPicStride,
                          &pResult->iFrameSad, (int32_t*)pResult->pSad8x8, pUnchanged, kiUnchangedCount);
      }
    }
  }

  return RET_SUCCESS;
}

EResult CVAACalculation::Set (int32_t iType, void* pParam) {
  if (pParam == NULL || ((SVAACalcParam*)pParam)->pCalcResult == NULL) {
    return RET_INVALIDPARAM;
  }

  m_sCalcParam = * (SVAACalcParam*)pParam;

  return RET_SUCCESS;
}


WELSVP_NAMESPACE_END
