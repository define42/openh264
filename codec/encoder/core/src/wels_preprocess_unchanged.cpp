/*!
 * \copy
 *     Copyright (c)  2009-2013, Cisco Systems
 *     All rights reserved. BSD-2-Clause; see LICENSE.
 *
 * \file    wels_preprocess_unchanged.cpp
 *
 * \brief   Unchanged-macroblock hints in preprocessing (DevBox-Gateway-kvm
 *          extension): VAA SADs and source copies skip certified macroblocks.
 *          Counterparts of the pure-Go port's wels_preprocess_unchanged.go and
 *          wels_preprocess_source.go. Neither changes the encoded stream.
 */
#include <string.h>

#include "wels_preprocess.h"
#include "encoder_context.h"
#include "unchanged.h"

namespace WelsEnc {

void CWelsPreProcess::VaaUnchangedReset() {
  memset (&m_sVaaUnchanged, 0, sizeof (m_sVaaUnchanged));
}

void CWelsPreProcess::VaaUnchangedBegin (sWelsEncCtx* pCtx, const SSourcePicture* kpSrc) {
  SVaaUnchangedFrameState* pState = &m_sVaaUnchanged;
  pState->pMask = NULL;
  pState->iMaskCount = 0;
  pState->pCurrent = NULL;
  pState->bEnabled = false;
  if (kpSrc == NULL || pCtx == NULL || !UnchangedConfiguration (pCtx->pSvcParam) ||
      kpSrc->iColorFormat != videoFormatI420 || pCtx->pVpp != this || m_sScaledPicture.pScaledInputPicture != NULL) {
    VaaUnchangedReset();
    return;
  }
  const SWelsSvcCodingParam* kpParam = pCtx->pSvcParam;
  if (kpParam->SUsedPicRect.iLeft != 0 || kpParam->SUsedPicRect.iTop != 0 ||
      kpParam->SUsedPicRect.iWidth != kpSrc->iPicWidth || kpParam->SUsedPicRect.iHeight != kpSrc->iPicHeight ||
      kpSrc->iPicWidth < 16 || kpSrc->iPicHeight < 16 ||
      kpSrc->iPicWidth != kpParam->iPicWidth || kpSrc->iPicHeight != kpParam->iPicHeight ||
      kpSrc->iPicWidth != kpParam->sDependencyLayers[0].iActualWidth ||
      kpSrc->iPicHeight != kpParam->sDependencyLayers[0].iActualHeight) {
    VaaUnchangedReset();
    return;
  }
  if (pState->iWidth != kpSrc->iPicWidth || pState->iHeight != kpSrc->iPicHeight)
    pState->pReference = NULL;
  pState->iWidth = kpSrc->iPicWidth;
  pState->iHeight = kpSrc->iPicHeight;
  pState->bEnabled = true;
  const int64_t kiCount = ((int64_t)pState->iWidth + 15) / 16 * (((int64_t)pState->iHeight + 15) / 16);
  if (pState->pReference != NULL && kpSrc->pUnchangedMacroblocks != NULL &&
      (int64_t)kpSrc->iUnchangedMacroblockCount == kiCount) {
    pState->pMask = kpSrc->pUnchangedMacroblocks;
    pState->iMaskCount = kpSrc->iUnchangedMacroblockCount;
  }
}

const unsigned char* CWelsPreProcess::VaaUnchangedMask (SPicture* pCurrent, SPicture* pReference, int32_t* pCount) {
  const SVaaUnchangedFrameState* kpState = &m_sVaaUnchanged;
  sWelsEncCtx* pCtx = m_pEncCtx;
  *pCount = 0;
  if (!kpState->bEnabled || kpState->pMask == NULL || pCtx == NULL || pCtx->pVpp != this ||
      pCtx->eSliceType != P_SLICE || !UnchangedConfiguration (pCtx->pSvcParam) ||
      pCurrent == NULL || pReference == NULL || pCurrent == pReference || pCurrent != kpState->pCurrent ||
      pReference != kpState->pReference || pReference->iFramePoc != kpState->iPoc ||
      pReference != m_pLastSpatialPicture[0][0])
    return NULL;
  // Padding is constant, so an unchanged partial edge block also has equal
  // padded source samples. Require the exact padded geometry.
  const int32_t kiWidth = (kpState->iWidth + 15) & ~15;
  const int32_t kiHeight = (kpState->iHeight + 15) & ~15;
  if (pCurrent->iWidthInPixel != kiWidth || pCurrent->iHeightInPixel != kiHeight ||
      pCurrent->iWidthInPixel != pReference->iWidthInPixel || pCurrent->iHeightInPixel != pReference->iHeightInPixel ||
      pCurrent->iLineSize[0] != pReference->iLineSize[0])
    return NULL;
  *pCount = kpState->iMaskCount;
  return kpState->pMask;
}

void CWelsPreProcess::VaaUnchangedFinish (sWelsEncCtx* pCtx, const SFrameBSInfo* kpBsInfo, bool bSuccess) {
  SPicture* pCurrent = m_sVaaUnchanged.pCurrent;
  const int32_t kiWidth = m_sVaaUnchanged.iWidth;
  const int32_t kiHeight = m_sVaaUnchanged.iHeight;
  const bool kbEnabled = m_sVaaUnchanged.bEnabled;
  VaaUnchangedReset();
  if (!kbEnabled || !bSuccess || pCtx == NULL || pCtx->pVpp != this || kpBsInfo == NULL ||
      pCtx->iEncoderError != ENC_RETURN_SUCCESS || !UnchangedConfiguration (pCtx->pSvcParam) ||
      kpBsInfo->iFrameSizeInBytes <= 0 ||
      (kpBsInfo->eFrameType != videoFrameTypeP && kpBsInfo->eFrameType != videoFrameTypeIDR) ||
      pCurrent == NULL || pCurrent != pCtx->pEncPic || pCurrent != m_pLastSpatialPicture[0][0])
    return;
  m_sVaaUnchanged.pReference = pCurrent;
  m_sVaaUnchanged.iPoc = pCurrent->iFramePoc;
  m_sVaaUnchanged.iWidth = kiWidth;
  m_sVaaUnchanged.iHeight = kiHeight;
}

void CWelsPreProcess::SourceCopyInvalidate() {
  SSourceCopyState* pState = m_pSourceCopy;
  if (pState == NULL)
    return;
  for (int32_t i = 0; i < 3; ++i) {
    SSourceCopyBuffer* pBuffer = &pState->sBuffers[i];
    pBuffer->pPicture = NULL;
    memset (pBuffer->pData, 0, sizeof (pBuffer->pData));
    memset (pBuffer->iLineSize, 0, sizeof (pBuffer->iLineSize));
    pBuffer->bValid = false;
  }
  pState->pReference = NULL;
  pState->pCurrent = NULL;
  pState->bPartial = false;
}

// Runs after VAA's independent original-reference validation. Only a
// successfully published source establishes the next input generation.
void CWelsPreProcess::SourceCopyFinish (const SFrameBSInfo* kpBsInfo) {
  SSourceCopyState* pState = m_pSourceCopy;
  if (pState == NULL)
    return;
  if (kpBsInfo == NULL || pState->pCurrent == NULL || pState->pCurrent != m_sVaaUnchanged.pReference) {
    SourceCopyInvalidate();
    return;
  }
  pState->pReference = pState->pCurrent;
  pState->iPoc = m_sVaaUnchanged.iPoc;
  if (kpBsInfo->eFrameType == videoFrameTypeIDR) {
    for (int32_t i = 0; i < 3; ++i) {
      if (pState->sBuffers[i].pPicture != pState->pCurrent)
        pState->sBuffers[i].bValid = false;
    }
  }
  pState->pCurrent = NULL;
}

// Sparse damage leaves long stretches of equal bytes; test eight at a time.
static inline uint64_t LoadBytes8 (const void* kpData) {
  uint64_t uiWord;
  memcpy (&uiWord, kpData, sizeof (uiWord));
  return uiWord;
}

// Index of the first zero byte in kpBytes[iFrom, iEnd), or iEnd.
static int32_t NextZeroByte (const unsigned char* kpBytes, int32_t iFrom, int32_t iEnd) {
  const uint64_t kuiOnes = 0x0101010101010101ULL, kuiHighs = 0x8080808080808080ULL;
  while (iFrom + 8 <= iEnd) {
    const uint64_t kuiWord = LoadBytes8 (kpBytes + iFrom);
    if (((kuiWord - kuiOnes) & ~kuiWord & kuiHighs) != 0)
      break;
    iFrom += 8;
  }
  while (iFrom < iEnd && kpBytes[iFrom] != 0)
    ++iFrom;
  return iFrom;
}

// Index of the first true entry in kpFlags[iFrom, iEnd), or iEnd.
static int32_t NextTrue (const bool* kpFlags, int32_t iFrom, int32_t iEnd) {
  while (iFrom + 8 <= iEnd && LoadBytes8 (kpFlags + iFrom) == 0)
    iFrom += 8;
  while (iFrom < iEnd && !kpFlags[iFrom])
    ++iFrom;
  return iFrom;
}

static void CopySourceDirtyRuns (SPicture* pDst, const SSourcePicture* kpSrc, const bool* kpDirty, int32_t iCols,
                                 int32_t iRows) {
  for (int32_t iMbY = 0; iMbY < iRows; ++iMbY) {
    const bool* kpRow = kpDirty + iMbY * iCols;
    for (int32_t iMbX = NextTrue (kpRow, 0, iCols); iMbX < iCols; iMbX = NextTrue (kpRow, iMbX, iCols)) {
      const int32_t kiStart = iMbX;
      while (iMbX < iCols && kpRow[iMbX])
        ++iMbX;
      const int32_t kiX0 = kiStart * 16, kiX1 = WELS_MIN (iMbX * 16, kpSrc->iPicWidth);
      const int32_t kiY0 = iMbY * 16, kiY1 = WELS_MIN ((iMbY + 1) * 16, kpSrc->iPicHeight);
      for (int32_t iPlane = 0; iPlane < 3; ++iPlane) {
        const int32_t kiShift = iPlane != 0 ? 1 : 0;
        const int32_t kiLeft = kiX0 >> kiShift, kiRight = kiX1 >> kiShift;
        for (int32_t y = kiY0 >> kiShift; y < (kiY1 >> kiShift); ++y) {
          memcpy (pDst->pData[iPlane] + y * pDst->iLineSize[iPlane] + kiLeft,
                  kpSrc->pData[iPlane] + y * kpSrc->iStride[iPlane] + kiLeft, kiRight - kiLeft);
        }
      }
    }
  }
}

// Returns true when it copied only pending dirty runs. False leaves copying to
// WelsMoveMemory_c; the resident record is committed only if the encode then
// publishes this original picture.
bool CWelsPreProcess::TryCopySource (SWelsSvcCodingParam* pParam, SPicture* pDst, const SSourcePicture* kpSrc,
                                     int32_t iWidth, int32_t iHeight) {
  SSourceCopyState* pState = m_pSourceCopy;
  const SVaaUnchangedFrameState* kpVaa = &m_sVaaUnchanged;
  sWelsEncCtx* pCtx = m_pEncCtx;
  if (pState == NULL)
    return false;
  const int32_t kiCols = (iWidth + 15) / 16, kiRows = (iHeight + 15) / 16;
  if (pCtx == NULL || pCtx->pVpp != this || !kpVaa->bEnabled || !UnchangedConfiguration (pParam) ||
      kpSrc->iColorFormat != videoFormatI420 || m_sScaledPicture.pScaledInputPicture != NULL ||
      iWidth != kpSrc->iPicWidth || iHeight != kpSrc->iPicHeight || iWidth < 16 || iHeight < 16 || (iWidth & 1) ||
      (iHeight & 1) || iWidth != pParam->iPicWidth || iHeight != pParam->iPicHeight ||
      iWidth != pParam->sDependencyLayers[0].iActualWidth || iHeight != pParam->sDependencyLayers[0].iActualHeight ||
      pParam->SUsedPicRect.iLeft != 0 || pParam->SUsedPicRect.iTop != 0 || pParam->SUsedPicRect.iWidth != iWidth ||
      pParam->SUsedPicRect.iHeight != iHeight || pDst == NULL || pDst->iWidthInPixel != ((iWidth + 15) & ~15) ||
      pDst->iHeightInPixel != ((iHeight + 15) & ~15) || kiCols * kiRows > SOURCE_COPY_MAX_MB_COUNT) {
    SourceCopyInvalidate();
    return false;
  }
  for (int32_t iPlane = 0; iPlane < 3; ++iPlane) {
    const int32_t kiPlaneWidth = iPlane == 0 ? iWidth : iWidth / 2;
    if (kpSrc->iStride[iPlane] < kiPlaneWidth || pDst->iLineSize[iPlane] < kiPlaneWidth ||
        kpSrc->pData[iPlane] == NULL || pDst->pData[iPlane] == NULL) {
      SourceCopyInvalidate();
      return false;
    }
  }

  const int32_t kiCount = kiCols * kiRows;
  const SSpatialLayerConfig* kpLayer = &pParam->sSpatialLayers[0];
  const unsigned char* kpMask = kpVaa->pMask;
  const bool kbValidChain = pState->pReference != NULL && pState->pReference == kpVaa->pReference &&
                            pState->pReference == m_pLastSpatialPicture[0][0] && pState->pReference->iFramePoc == pState->iPoc &&
                            pState->iPoc == kpVaa->iPoc && pDst != pState->pReference && pState->iWidth == iWidth &&
                            pState->iHeight == iHeight && pState->iQp == kpLayer->iDLayerQp &&
                            pState->iSliceMode == (int32_t)kpLayer->sSliceArgument.uiSliceMode &&
                            pState->uiSlices == kpLayer->sSliceArgument.uiSliceNum && kpMask != NULL && kpVaa->iMaskCount == kiCount &&
                            !pParam->sDependencyLayers[0].bEncCurFrmAsIdrFlag && (pCtx->pVaa == NULL || !pCtx->pVaa->bIdrPeriodFlag);
  if (!kbValidChain)
    SourceCopyInvalidate();
  pState->iWidth = iWidth;
  pState->iHeight = iHeight;
  pState->iQp = kpLayer->iDLayerQp;
  pState->iSliceMode = (int32_t)kpLayer->sSliceArgument.uiSliceMode;
  pState->uiSlices = kpLayer->sSliceArgument.uiSliceNum;
  pState->pCurrent = pDst;
  pState->bPartial = false;

  SSourceCopyBuffer* pBuffer = NULL;
  for (int32_t i = 0; i < 3 && pBuffer == NULL; ++i) {
    if (pState->sBuffers[i].pPicture == pDst)
      pBuffer = &pState->sBuffers[i];
  }
  for (int32_t i = 0; i < 3 && pBuffer == NULL; ++i) {
    if (pState->sBuffers[i].pPicture == NULL)
      pBuffer = &pState->sBuffers[i];
  }
  if (pBuffer == NULL) {
    // Unexpected source-buffer lifecycle: refresh fully and forget every
    // older generation.
    SourceCopyInvalidate();
    pState->pCurrent = pDst;
    pBuffer = &pState->sBuffers[0];
  }
  bool bSamePlanes = pBuffer->iDirtyCount == kiCount;
  for (int32_t iPlane = 0; iPlane < 3; ++iPlane) {
    bSamePlanes = bSamePlanes && pBuffer->pData[iPlane] == pDst->pData[iPlane] &&
                  pBuffer->iLineSize[iPlane] == pDst->iLineSize[iPlane];
  }
  if (!bSamePlanes)
    pBuffer->bValid = false;
  pBuffer->pPicture = pDst;
  for (int32_t iPlane = 0; iPlane < 3; ++iPlane) {
    pBuffer->pData[iPlane] = pDst->pData[iPlane];
    pBuffer->iLineSize[iPlane] = pDst->iLineSize[iPlane];
  }
  if (pBuffer->iDirtyCount != kiCount) {
    memset (pBuffer->bDirty, 0, (size_t)kiCount * sizeof (bool));
    pBuffer->iDirtyCount = kiCount;
  }
  if (kbValidChain) {
    for (int32_t iMb = NextZeroByte (kpMask, 0, kiCount); iMb < kiCount; iMb = NextZeroByte (kpMask, iMb + 1, kiCount)) {
      for (int32_t i = 0; i < 3; ++i) {
        if (pState->sBuffers[i].bValid)
          pState->sBuffers[i].bDirty[iMb] = true;
      }
    }
  }
  if (pBuffer->bValid) {
    // Some macroblock is still clean: copy only the dirty ones.
    const bool* kpDirty = pBuffer->bDirty;
    int32_t iClean = 0;
    while (iClean + 8 <= kiCount && LoadBytes8 (kpDirty + iClean) == 0x0101010101010101ULL)
      iClean += 8;
    while (iClean < kiCount && kpDirty[iClean])
      ++iClean;
    if (iClean < kiCount) {
      CopySourceDirtyRuns (pDst, kpSrc, pBuffer->bDirty, kiCols, kiRows);
      pState->bPartial = true;
    }
  }
  memset (pBuffer->bDirty, 0, (size_t)kiCount * sizeof (bool));
  pBuffer->bValid = true;
  return pState->bPartial;
}

}
