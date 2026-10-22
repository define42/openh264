/*!
 * \copy
 *     Copyright (c)  2009-2013, Cisco Systems
 *     All rights reserved. BSD-2-Clause; see LICENSE.
 *
 * \file    unchanged.cpp
 *
 * \brief   Optional unchanged-macroblock hints (DevBox-Gateway-kvm extension).
 *          A line-by-line counterpart of the pure-Go port's unchanged.go.
 */
#include <stddef.h>
#include <string.h>

#include "unchanged.h"
#include "encoder_context.h"
#include "md.h"
#include "mv_pred.h"
#include "svc_base_layer_md.h"
#include "svc_enc_slice_segment.h"
#include "svc_encode_slice.h"
#include "svc_mode_decision.h"
#include "svc_motion_estimate.h"
#include "mv_pred.h"

namespace WelsEnc {

bool UnchangedConfiguration (const SWelsSvcCodingParam* pParam) {
  if (pParam == NULL)
    return false;
  const bool kbOrdinarySingleReference = pParam->iUsageType == CAMERA_VIDEO_REAL_TIME &&
                                         pParam->iSpatialLayerNum == 1 && pParam->iTemporalLayerNum == 1 && pParam->iNumRefFrame == 1;
  const bool kbFixedIndependentCoding = pParam->iRCMode == RC_OFF_MODE && pParam->iComplexityMode == LOW_COMPLEXITY &&
                                        pParam->iEntropyCodingModeFlag == 0 && pParam->iLoopFilterDisableIdc == 1;
  const bool kbNoSourceOrReferencePolicy = !pParam->bEnableDenoise && !pParam->bEnableAdaptiveQuant &&
      !pParam->bEnableBackgroundDetection && !pParam->bEnableLongTermReference && !pParam->bSimulcastAVC;
  const SliceModeEnum kiMode = pParam->sSpatialLayers[0].sSliceArgument.uiSliceMode;
  const bool kbFixedSlices = kiMode == SM_SINGLE_SLICE || kiMode == SM_FIXEDSLCNUM_SLICE;
  return kbOrdinarySingleReference && kbFixedIndependentCoding && kbNoSourceOrReferencePolicy && kbFixedSlices;
}

void UnchangedFrameReset (sWelsEncCtx* pCtx) {
  // The stamps are read only below iFinalZeroSkipCount; leave them as is.
  memset (&pCtx->sUnchanged, 0, offsetof (SUnchangedFrameState, uiStampCounter));
}

static SReconstructedPictureVersion PictureVersion (SPicture* pPicture, uint64_t uiGeneration) {
  SReconstructedPictureVersion sVersion;
  memset (&sVersion, 0, sizeof (sVersion));
  sVersion.pPicture = pPicture;
  sVersion.uiGeneration = uiGeneration;
  if (pPicture != NULL) {
    for (int32_t i = 0; i < 3; ++i) {
      sVersion.pData[i] = pPicture->pData[i];
      sVersion.iLineSize[i] = pPicture->iLineSize[i];
    }
  }
  return sVersion;
}

static bool SameVersion (const SReconstructedPictureVersion* kpA, const SReconstructedPictureVersion* kpB) {
  if (kpA->pPicture != kpB->pPicture || kpA->uiGeneration != kpB->uiGeneration)
    return false;
  for (int32_t i = 0; i < 3; ++i) {
    if (kpA->pData[i] != kpB->pData[i] || kpA->iLineSize[i] != kpB->iLineSize[i])
      return false;
  }
  return true;
}

// The previous frame's prediction source, if its allocation still holds the
// same complete write; a zero-motion skip into it is already resident.
static SPicture* ReusableSource (const SUnchangedFrameState* kpState) {
  const SReconstructedPictureVersion* kpSource = &kpState->sSource;
  if (kpSource->pPicture == NULL || kpSource->uiGeneration == 0)
    return NULL;
  SReconstructedPictureVersion sCurrent = PictureVersion (kpSource->pPicture, kpSource->uiGeneration);
  if (!SameVersion (kpSource, &sCurrent))
    return NULL;
  for (int32_t i = 0; i < 2; ++i) {
    if (SameVersion (&kpState->sPictures[i], kpSource))
      return kpSource->pPicture;
  }
  return NULL;
}

static void CaptureFinalSkipMotion (SUnchangedFrameState* pState, const SDqLayer* kpLayer, bool bValidSource) {
  const int32_t kiCount = kpLayer->iMbWidth * kpLayer->iMbHeight;
  // Called after a successful frame, after every slice thread finished. A
  // macroblock coded outside WelsMdInterMbLoop keeps an older stamp, which
  // only forgoes reuse.
  if (!bValidSource || pState->uiCodingStamp == 0 || kiCount < 1 || kiCount > UNCHANGED_MAX_MB_COUNT) {
    pState->iFinalZeroSkipCount = 0;
    return;
  }
  pState->uiFinalStamp = pState->uiCodingStamp;
  pState->iFinalZeroSkipCount = kiCount;
}

static void FinishReconstruction (sWelsEncCtx* pCtx, SPicture* pReference, EVideoFrameType eFrameType) {
  SUnchangedFrameState* pState = &pCtx->sUnchanged;
  // A previous zero-motion skip equals the picture used to predict it. If that
  // allocation keeps the same generation, a matching block in the next
  // destination already holds the required reconstruction.
  SReconstructedPictureVersion sSource;
  memset (&sSource, 0, sizeof (sSource));
  if (eFrameType == videoFrameTypeP && pCtx->pCurDqLayer->pRefPic == pState->pReference) {
    for (int32_t i = 0; i < 2; ++i) {
      SReconstructedPictureVersion sCurrent = PictureVersion (pState->pReference, pState->sPictures[i].uiGeneration);
      if (pState->sPictures[i].pPicture == pState->pReference && SameVersion (&pState->sPictures[i], &sCurrent)) {
        sSource = pState->sPictures[i];
        break;
      }
    }
  }
  const SWelsSvcCodingParam* kpParam = pCtx->pSvcParam;
  if (eFrameType == videoFrameTypeIDR || pState->iWidth != kpParam->iPicWidth || pState->iHeight != kpParam->iPicHeight ||
      pState->iQp != kpParam->sSpatialLayers[0].iDLayerQp) {
    memset (pState->sPictures, 0, sizeof (pState->sPictures));
    memset (&sSource, 0, sizeof (sSource));
  }
  ++pState->uiGeneration;
  if (pState->uiGeneration == 0) {
    memset (pState->sPictures, 0, sizeof (pState->sPictures));
    memset (&sSource, 0, sizeof (sSource));
    pState->uiGeneration = 1;
  }
  // One short-term reference means two allocations: replace the destination's
  // previous version and keep the source's.
  const int32_t kiIndex = (pState->sPictures[0].pPicture == sSource.pPicture && sSource.pPicture != NULL) ? 1 : 0;
  pState->sPictures[kiIndex] = PictureVersion (pReference, pState->uiGeneration);
  pState->sSource = sSource;
  CaptureFinalSkipMotion (pState, pCtx->pCurDqLayer, sSource.pPicture != NULL);
}

void UnchangedFrameBegin (sWelsEncCtx* pCtx, const SSourcePicture* pSrc) {
  SUnchangedFrameState* pState = &pCtx->sUnchanged;
  pState->pMacroblocks = NULL;
  pState->iMacroblockCount = 0;
  pState->iChangedMacroblockCount = 0;
  pState->pMotion = NULL;
  pState->pReuseDestination = NULL;
  pState->uiCodingStamp = 0;
  if (pSrc == NULL || !UnchangedConfiguration (pCtx->pSvcParam))
    return;
  if (++pState->uiStampCounter == 0) {
    memset (pState->uiZeroSkipStamp, 0, sizeof (pState->uiZeroSkipStamp));
    pState->iFinalZeroSkipCount = 0;
    pState->uiStampCounter = 1;
  }
  pState->uiCodingStamp = pState->uiStampCounter;
  const SWelsSvcCodingParam* kpParam = pCtx->pSvcParam;
  const bool kbMatchingGeometry = pSrc->iPicWidth == kpParam->iPicWidth && pSrc->iPicHeight == kpParam->iPicHeight &&
                                  pSrc->iPicWidth == kpParam->sDependencyLayers[0].iActualWidth &&
                                  pSrc->iPicHeight == kpParam->sDependencyLayers[0].iActualHeight;
  if (!kbMatchingGeometry || pSrc->iPicWidth < 16 || pSrc->iPicHeight < 16)
    return;
  const int64_t kiCount = ((int64_t)pSrc->iPicWidth + 15) / 16 * (((int64_t)pSrc->iPicHeight + 15) / 16);
  const bool kbValidPrevious = pState->pReference != NULL && pState->iWidth == pSrc->iPicWidth &&
                               pState->iHeight == pSrc->iPicHeight && pState->iQp == kpParam->sSpatialLayers[0].iDLayerQp;
  if (kbValidPrevious && pSrc->pMacroblockMotion != NULL && (int64_t)pSrc->iMacroblockMotionCount == kiCount)
    pState->pMotion = pSrc->pMacroblockMotion;
  if (kbValidPrevious && pSrc->pUnchangedMacroblocks != NULL && (int64_t)pSrc->iUnchangedMacroblockCount == kiCount) {
    pState->pMacroblocks = pSrc->pUnchangedMacroblocks;
    pState->iMacroblockCount = pSrc->iUnchangedMacroblockCount;
    int32_t iChanged = 0;
    for (int32_t i = 0; i < pState->iMacroblockCount; ++i)
      iChanged += pState->pMacroblocks[i] == 0;
    pState->iChangedMacroblockCount = iChanged;
    pState->pReuseDestination = ReusableSource (pState);
  }
}

void UnchangedFrameFinish (sWelsEncCtx* pCtx, const SSourcePicture* pSrc, const SFrameBSInfo* pBsInfo, bool bSuccess) {
  SUnchangedFrameState* pState = &pCtx->sUnchanged;
  pState->pMacroblocks = NULL;
  pState->iMacroblockCount = 0;
  pState->iChangedMacroblockCount = 0;
  pState->pMotion = NULL;
  pState->pReuseDestination = NULL;
  if (!bSuccess || pSrc == NULL || pBsInfo == NULL || !UnchangedConfiguration (pCtx->pSvcParam)) {
    UnchangedFrameReset (pCtx);
    return;
  }
  const bool kbEncodedPicture = pBsInfo->iFrameSizeInBytes > 0 &&
                                (pBsInfo->eFrameType == videoFrameTypeP || pBsInfo->eFrameType == videoFrameTypeIDR);
  if (!kbEncodedPicture || pCtx->iEncoderError != ENC_RETURN_SUCCESS || pCtx->pCurDqLayer == NULL) {
    UnchangedFrameReset (pCtx);
    return;
  }
  // pCtx->pDecPic already points at the next free buffer after the reference
  // list update; the layer retains the reconstruction that was just published.
  SPicture* pReference = pCtx->pCurDqLayer->pDecPic;
  if (pReference == NULL || !pReference->bUsedAsRef || pReference->bIsLongRef) {
    UnchangedFrameReset (pCtx);
    return;
  }
  FinishReconstruction (pCtx, pReference, pBsInfo->eFrameType);
  pState->pReference = pReference;
  pState->iFrameNum = pReference->iFrameNum;
  pState->iPoc = pReference->iFramePoc;
  pState->iQp = pCtx->pSvcParam->sSpatialLayers[0].iDLayerQp;
  pState->iWidth = pSrc->iPicWidth;
  pState->iHeight = pSrc->iPicHeight;
}

// The reference is the picture the hints were made against.
static bool HintReferenceValid (sWelsEncCtx* pCtx) {
  const SUnchangedFrameState* kpState = &pCtx->sUnchanged;
  const SPicture* kpReference = pCtx->pCurDqLayer->pRefPic;
  return pCtx->eSliceType == P_SLICE && pCtx->iNumRef0 == 1 &&
         kpReference != NULL && kpReference == kpState->pReference && kpReference == pCtx->pRefList0[0] &&
         kpReference->iFrameNum == kpState->iFrameNum && kpReference->iFramePoc == kpState->iPoc;
}

static bool UnchangedMacroblockEligible (sWelsEncCtx* pCtx, const SMB* pCurMb) {
  const SUnchangedFrameState* kpState = &pCtx->sUnchanged;
  const int32_t kiIndex = pCurMb->iMbXY;
  if (kiIndex < 0 || kiIndex >= kpState->iMacroblockCount || kpState->pMacroblocks == NULL ||
      !kpState->pMacroblocks[kiIndex])
    return false;
  const SDqLayer* kpLayer = pCtx->pCurDqLayer;
  const SPicture* kpReference = kpLayer->pRefPic;
  if (!HintReferenceValid (pCtx) || kpLayer->iLoopFilterDisableIdc != 1 || (int32_t)pCurMb->uiLumaQp != kpState->iQp)
    return false;
  // Require a previous zero-motion P_SKIP. sMvList is search metadata, which
  // keeps first P frames after an IDR on the full path.
  return kpReference->iPictureType == P_SLICE && kpReference->uiRefMbType[kiIndex] == MB_TYPE_SKIP &&
         kpReference->sMvList[kiIndex].iMvX == 0 && kpReference->sMvList[kiIndex].iMvY == 0;
}

static bool UnchangedSkipPredictorIsZero (const SMB* pCurMb, const int32_t kiMbWidth) {
  const uint32_t kuiAvail = pCurMb->uiNeighborAvail;
  if (! (kuiAvail & LEFT_MB_POS) || ! (kuiAvail & TOP_MB_POS))
    return true;
  const SMB* kpLeft = pCurMb - 1;
  if (IS_SVC_INTER (kpLeft->uiMbType) && kpLeft->pRefIndex[1] == 0 && kpLeft->sMv[3].iMvX == 0 &&
      kpLeft->sMv[3].iMvY == 0)
    return true;
  const SMB* kpTop = pCurMb - kiMbWidth;
  return IS_SVC_INTER (kpTop->uiMbType) && kpTop->pRefIndex[2] == 0 && kpTop->sMv[12].iMvX == 0 &&
         kpTop->sMv[12].iMvY == 0;
}

static void CopyBlock (uint8_t* pDst, int32_t iDstStride, const uint8_t* pSrc, int32_t iSrcStride, int32_t iSize) {
  for (int32_t i = 0; i < iSize; ++i) {
    memcpy (pDst, pSrc, iSize);
    pDst += iDstStride;
    pSrc += iSrcStride;
  }
}

static void EncodeUnchangedMacroblock (sWelsEncCtx* pCtx, SWelsMD* pMd, SSlice* pSlice, SMB* pCurMb) {
  SDqLayer* pLayer = pCtx->pCurDqLayer;
  SPicture* pReference = pLayer->pRefPic;
  const int32_t kiIndex = pCurMb->iMbXY;
  SMbCache* pMbCache = &pSlice->sMbCacheInfo;

  // Copy reconstructed reference samples, never source pixels: the decoder
  // infers the same zero-motion prediction from P_SKIP. When the recycled
  // destination still holds the generation a previous zero-motion skip copied
  // from, its samples are already identical.
  const SUnchangedFrameState* kpState = &pCtx->sUnchanged;
  const bool kbResident = kpState->pReuseDestination != NULL && pLayer->pDecPic == kpState->pReuseDestination &&
                          kiIndex < kpState->iFinalZeroSkipCount &&
                          kpState->uiZeroSkipStamp[kiIndex] == kpState->uiFinalStamp;
  if (!kbResident) {
    CopyBlock (pMbCache->SPicData.pCsMb[0], pLayer->iCsStride[0], pMbCache->SPicData.pRefMb[0],
               pReference->iLineSize[0], 16);
    for (int32_t iPlane = 1; iPlane < 3; ++iPlane) {
      CopyBlock (pMbCache->SPicData.pCsMb[iPlane], pLayer->iCsStride[iPlane], pMbCache->SPicData.pRefMb[iPlane],
                 pReference->iLineSize[iPlane], 8);
    }
  }

  pCurMb->uiMbType = MB_TYPE_SKIP;
  pCurMb->uiCbp = 0;
  pCurMb->pRefIndex[0] = pCurMb->pRefIndex[1] = pCurMb->pRefIndex[2] = pCurMb->pRefIndex[3] = 0;
  const SMVUnitXY kZero = {0, 0};
  pCtx->pFuncList->pfUpdateMbMv (pCurMb->sMv, kZero);
  pCurMb->sP16x16Mv = kZero;
  pLayer->pDecPic->sMvList[kiIndex] = kZero;
  memset (pCurMb->pNonZeroCount, 0, 24);
  // Initialization leaves this MB's previous luma cost intact; keep that
  // heuristic metadata as the full mode decision would see it.
  pMd->iCostLuma = *pCurMb->pSadCost;
  pMd->iCostSkipMb = pReference->pMbSkipSad[kiIndex];
  WelsMdInterUpdatePskip (pLayer, pSlice, pCurMb, pMbCache);
}

bool TryUnchangedMacroblockEarly (sWelsEncCtx* pCtx, SWelsMD* pMd, SSlice* pSlice, SMB* pCurMb) {
  if (!UnchangedMacroblockEligible (pCtx, pCurMb) || !UnchangedSkipPredictorIsZero (pCurMb, pCtx->pCurDqLayer->iMbWidth))
    return false;
  EncodeUnchangedMacroblock (pCtx, pMd, pSlice, pCurMb);
  return true;
}

// Not declared in a header; the run path requires fixed-QP rate control.
void WelsRcMbInitDisable (sWelsEncCtx* pEncCtx, SMB* pCurMb, SSlice* pSlice);
void WelsRcMbInfoUpdateDisable (sWelsEncCtx* pEncCtx, SMB* pCurMb, int32_t iCostLuma, SSlice* pSlice);

// The picture-wide half of UnchangedMacroblockEligible, plus the per-macroblock
// steps of WelsMdInterMbLoop the run path replaces with their known effects:
// fixed-QP rate control (WelsRcMbInitDisable sets this QP), background update
// without detection, and CAVLC, whose skips write no bits.
uint32_t* UnchangedZeroSkipStamps (sWelsEncCtx* pCtx, uint32_t* pStamp) {
  SUnchangedFrameState* pState = &pCtx->sUnchanged;
  const SDqLayer* kpLayer = pCtx->pCurDqLayer;
  *pStamp = pState->uiCodingStamp;
  if (pState->uiCodingStamp == 0 || kpLayer->iMbWidth * kpLayer->iMbHeight > UNCHANGED_MAX_MB_COUNT)
    return NULL;
  return pState->uiZeroSkipStamp;
}

bool UnchangedRunsEnabled (sWelsEncCtx* pCtx) {
  const SUnchangedFrameState* kpState = &pCtx->sUnchanged;
  const SSliceCtx* kpSliceCtx = &pCtx->pCurDqLayer->sSliceEncCtx;
  if (kpState->pMacroblocks == NULL || !UnchangedConfiguration (pCtx->pSvcParam) ||
      (kpSliceCtx->uiSliceMode != SM_SINGLE_SLICE && kpSliceCtx->uiSliceMode != SM_FIXEDSLCNUM_SLICE))
    return false;
  const SWelsFuncPtrList* kpFunc = pCtx->pFuncList;
  if (kpFunc->pfRc.pfWelsRcMbInit != WelsRcMbInitDisable ||
      kpFunc->pfRc.pfWelsRcMbInfoUpdate != WelsRcMbInfoUpdateDisable ||
      (kpFunc->pfMdBackgroundInfoUpdate != WelsMdUpdateBGDInfo &&
       kpFunc->pfMdBackgroundInfoUpdate != WelsMdUpdateBGDInfoNULL) ||
      pCtx->pSvcParam->iEntropyCodingModeFlag != 0 || pCtx->pSvcParam->bEnableAdaptiveQuant)
    return false;
  const SDqLayer* kpLayer = pCtx->pCurDqLayer;
  const SPicture* kpReference = kpLayer->pRefPic;
  const bool kbValidReference = pCtx->eSliceType == P_SLICE && pCtx->iNumRef0 == 1 &&
                                kpReference != NULL && kpReference == kpState->pReference && kpReference == pCtx->pRefList0[0] &&
                                kpReference->iFrameNum == kpState->iFrameNum && kpReference->iFramePoc == kpState->iPoc;
  return kbValidReference && kpLayer->iLoopFilterDisableIdc == 1 && kpReference->iPictureType == P_SLICE &&
         pCtx->pRefPic != NULL && WELS_CLIP3 (pCtx->iGlobalQp, 0, 51) == kpState->iQp &&
         kpState->iMacroblockCount == kpLayer->iMbWidth * kpLayer->iMbHeight;
}

int32_t EncodeUnchangedRun (sWelsEncCtx* pCtx, SWelsMD* pMd, SSlice* pSlice, int32_t iMbIdx, int32_t iMaxCount,
                            int32_t* pNextMbIdx) {
  const SUnchangedFrameState* kpState = &pCtx->sUnchanged;
  SDqLayer* pLayer = pCtx->pCurDqLayer;
  SPicture* pReference = pLayer->pRefPic;
  SPicture* pDecPic = pLayer->pDecPic;
  SMB* pMbList = pLayer->sMbDataP;
  const int32_t kiTotal = pLayer->iMbWidth * pLayer->iMbHeight;
  const int32_t kiSliceIdx = pSlice->iSliceIdx;
  // Skips take the slice's last QP (WelsMdInterUpdatePskip, WelsSpatialWriteMbSyn).
  const uint8_t kuiLumaQp = pSlice->uiLastMbQp;
  const uint8_t kuiChromaQp = g_kuiChromaQpTable[CLIP3_QP_0_51 (kuiLumaQp +
                              pLayer->sLayerInfo.pPpsP->uiChromaQpIndexOffset)];
  // WelsMdUpdateBGDInfo with zero CBP and zero motion keeps the reference QP
  // unless the encoder's reference picture is intra.
  const bool kbIntraReference = pCtx->pRefPic->iPictureType == I_SLICE;
  const bool kbReuse = kpState->pReuseDestination != NULL && pDecPic == kpState->pReuseDestination &&
                       iMbIdx >= 0 && kiTotal <= kpState->iFinalZeroSkipCount;
  const uint32_t kuiFinalStamp = kpState->uiFinalStamp;
  uint32_t* pStamps = pCtx->sUnchanged.uiCodingStamp != 0 ? pCtx->sUnchanged.uiZeroSkipStamp : NULL;
  const uint32_t kuiCodingStamp = pCtx->sUnchanged.uiCodingStamp;
  // WelsGetNextMbOfSlice, unrolled for the fixed slice modes.
  const SSliceCtx* kpSliceCtx = &pLayer->sSliceEncCtx;
  const bool kbSingleSlice = kpSliceCtx->uiSliceMode == SM_SINGLE_SLICE;
  const uint16_t* kpMbMap = kpSliceCtx->pOverallMbMap;
  const int32_t kiMbNum = kpSliceCtx->iMbNumInFrame;
  const int32_t kiCsStrideY = pLayer->iCsStride[0], kiCsStrideUV = pLayer->iCsStride[1];
  const int32_t kiRefStrideY = pReference->iLineSize[0], kiRefStrideUV = pReference->iLineSize[1];

  int32_t iCount = 0, iLast = -1;
  while (iCount < iMaxCount && iMbIdx >= 0 && iMbIdx < kiTotal) {
    SMB* pMb = &pMbList[iMbIdx];
    if (!kpState->pMacroblocks[iMbIdx] || pReference->uiRefMbType[iMbIdx] != MB_TYPE_SKIP ||
        pReference->sMvList[iMbIdx].iMvX != 0 || pReference->sMvList[iMbIdx].iMvY != 0 ||
        !UnchangedSkipPredictorIsZero (pMb, pLayer->iMbWidth))
      break;
    // EncodeUnchangedMacroblock
    if (! (kbReuse && kpState->uiZeroSkipStamp[iMbIdx] == kuiFinalStamp)) {
      const int32_t kiX = pMb->iMbX, kiY = pMb->iMbY;
      CopyBlock (pLayer->pCsData[0] + ((kiX + kiY * kiCsStrideY) << 4), kiCsStrideY,
                 pReference->pData[0] + ((kiX + kiY * kiRefStrideY) << 4), pReference->iLineSize[0], 16);
      for (int32_t iPlane = 1; iPlane < 3; ++iPlane) {
        CopyBlock (pLayer->pCsData[iPlane] + ((kiX + kiY * kiCsStrideUV) << 3), pLayer->iCsStride[iPlane],
                   pReference->pData[iPlane] + ((kiX + kiY * kiRefStrideUV) << 3), pReference->iLineSize[iPlane], 8);
      }
    }
    pMb->uiMbType = MB_TYPE_SKIP;
    pMb->uiCbp = 0;
    pMb->pRefIndex[0] = pMb->pRefIndex[1] = pMb->pRefIndex[2] = pMb->pRefIndex[3] = 0;
    memset (pMb->sMv, 0, 16 * sizeof (SMVUnitXY));
    pMb->sP16x16Mv.iMvX = pMb->sP16x16Mv.iMvY = 0;
    pDecPic->sMvList[iMbIdx].iMvX = pDecPic->sMvList[iMbIdx].iMvY = 0;
    memset (pMb->pNonZeroCount, 0, 24);
    pMb->uiLumaQp = kuiLumaQp;
    pMb->uiChromaQp = kuiChromaQp;
    // WelsMdInterSaveSadAndRefMbType and WelsMdUpdateBGDInfo
    pDecPic->pMbSkipSad[iMbIdx] = pReference->pMbSkipSad[iMbIdx];
    pDecPic->uiRefMbType[iMbIdx] = MB_TYPE_SKIP;
    pDecPic->pRefMbQp[iMbIdx] = kbIntraReference ? kuiLumaQp : pReference->pRefMbQp[iMbIdx];
    // WelsSpatialWriteMbSyn and WelsMdInterMbLoop
    ++pSlice->iMbSkipRun;
    pMb->uiSliceIdc = kiSliceIdx;
    if (pStamps != NULL)
      pStamps[iMbIdx] = kuiCodingStamp;
    ++iCount;
    iLast = iMbIdx;
    const int32_t kiNext = iMbIdx + 1;
    if (kiNext >= kiMbNum || (!kbSingleSlice && (kpMbMap == NULL || kpMbMap[kiNext] != kpMbMap[iMbIdx])))
      iMbIdx = -1;
    else
      iMbIdx = kiNext;
  }
  if (iCount == 0)
    return 0;

  // Leave the per-slice cache and mode decision state as the last skipped
  // macroblock would have: later macroblocks advance its picture pointers.
  const SMB* kpLast = &pMbList[iLast];
  SMbCache* pMbCache = &pSlice->sMbCacheInfo;
  const int32_t kiX = kpLast->iMbX, kiY = kpLast->iMbY;
  for (int32_t iPlane = 0; iPlane < 3; ++iPlane) {
    const int32_t kiShift = iPlane == 0 ? 4 : 3;
    const int32_t kiStride = iPlane == 0 ? 0 : 1;
    pMbCache->SPicData.pEncMb[iPlane] = pLayer->pEncData[iPlane] + ((kiX + kiY * pLayer->iEncStride[kiStride]) << kiShift);
    pMbCache->SPicData.pCsMb[iPlane] = pLayer->pCsData[iPlane] + ((kiX + kiY * pLayer->iCsStride[kiStride]) << kiShift);
    pMbCache->SPicData.pDecMb[iPlane] = pDecPic->pData[iPlane] + ((kiX + kiY * pDecPic->iLineSize[kiStride]) << kiShift);
    pMbCache->SPicData.pRefMb[iPlane] = pReference->pData[iPlane] + ((kiX + kiY * pReference->iLineSize[kiStride]) <<
                                        kiShift);
  }
  pMbCache->pEncSad = &pDecPic->pMbSkipSad[iLast];
  pMbCache->uiRefMbType = pReference->uiRefMbType[iLast];
  pMbCache->bCollocatedPredFlag = true;
  UpdateNonZeroCountCache (const_cast<SMB*> (kpLast), pMbCache);
  pMd->iCostLuma = *kpLast->pSadCost;
  pMd->iCostSkipMb = pReference->pMbSkipSad[iLast];
  *pNextMbIdx = iMbIdx;
  return iCount;
}

bool UnchangedPreferCallerThread (sWelsEncCtx* pCtx) {
  const SUnchangedFrameState* kpState = &pCtx->sUnchanged;
  // Thread pool dispatch costs more than coding a few macroblocks outside
  // unchanged runs. Only certified blocks over zero-motion skips can join one.
  const int32_t kiLimit = kpState->iMacroblockCount / 32;
  if (kpState->iChangedMacroblockCount > kiLimit || !UnchangedRunsEnabled (pCtx))
    return false;
  const SPicture* kpReference = pCtx->pCurDqLayer->pRefPic;
  int32_t iFull = 0;
  for (int32_t i = 0; i < kpState->iMacroblockCount; ++i) {
    iFull += !kpState->pMacroblocks[i] || kpReference->uiRefMbType[i] != MB_TYPE_SKIP ||
             kpReference->sMvList[i].iMvX != 0 || kpReference->sMvList[i].iMvY != 0;
    if (iFull > kiLimit)
      return false;
  }
  return true;
}

bool TryUnchangedMacroblock (sWelsEncCtx* pCtx, SWelsMD* pMd, SSlice* pSlice, SMB* pCurMb) {
  if (!UnchangedMacroblockEligible (pCtx, pCurMb))
    return false;
  SMVUnitXY sPredicted;
  PredSkipMv (&pSlice->sMbCacheInfo, &sPredicted);
  if (sPredicted.iMvX != 0 || sPredicted.iMvY != 0)
    return false;
  EncodeUnchangedMacroblock (pCtx, pMd, pSlice, pCurMb);
  return true;
}

}

namespace WelsEnc {

// Mirrors the 16x16 path of WelsMdBackgroundMbEnc with the hinted vector in
// place of the zero vector and of the motion search.
bool TryMotionHintMacroblock (sWelsEncCtx* pEncCtx, SWelsMD* pWelsMd, SSlice* pSlice, SMB* pCurMb) {
  const SUnchangedFrameState* kpState = &pEncCtx->sUnchanged;
  const int32_t kiIndex = pCurMb->iMbXY;
  if (kpState->pMotion == NULL || kiIndex < 0 || kiIndex >= kpState->iMacroblockCount)
    return false;
  const SMVUnitXY kFull = {kpState->pMotion[kiIndex].iX, kpState->pMotion[kiIndex].iY};
  if ((kFull.iMvX == 0 && kFull.iMvY == 0) || !HintReferenceValid (pEncCtx) ||
      !CheckMvInRange (kFull, pSlice->sMvStartMin, pSlice->sMvStartMax))
    return false;

  SDqLayer* pCurDqLayer = pEncCtx->pCurDqLayer;
  SWelsFuncPtrList* pFunc = pEncCtx->pFuncList;
  SMbCache* pMbCache = &pSlice->sMbCacheInfo;
  SMVUnitXY sMv;
  sMv.iMvX = (int16_t) (kFull.iMvX * 4);
  sMv.iMvY = (int16_t) (kFull.iMvY * 4);
  const int32_t kiLineSizeY = pCurDqLayer->pRefPic->iLineSize[0];
  const int32_t kiLineSizeUV = pCurDqLayer->pRefPic->iLineSize[1];
  uint8_t* pRefLuma = pMbCache->SPicData.pRefMb[0] + kFull.iMvY * kiLineSizeY + kFull.iMvX;
  const int32_t kiChromaOffset = (sMv.iMvY >> 3) * kiLineSizeUV + (sMv.iMvX >> 3);
  uint8_t* pRefCb = pMbCache->SPicData.pRefMb[1] + kiChromaOffset;
  uint8_t* pRefCr = pMbCache->SPicData.pRefMb[2] + kiChromaOffset;
  uint8_t* pDstLuma = pMbCache->pMemPredLuma;
  uint8_t* pDstCb = pMbCache->pMemPredChroma;
  uint8_t* pDstCr = pMbCache->pMemPredChroma + 64;

  //MC
  pFunc->sMcFuncs.pMcLumaFunc (pRefLuma, kiLineSizeY, pDstLuma, 16, 0, 0, 16, 16);
  pFunc->sMcFuncs.pMcChromaFunc (pRefCb, kiLineSizeUV, pDstCb, 8, sMv.iMvX, sMv.iMvY, 8, 8); //Cb
  pFunc->sMcFuncs.pMcChromaFunc (pRefCr, kiLineSizeUV, pDstCr, 8, sMv.iMvX, sMv.iMvY, 8, 8); //Cr

  pCurMb->pSadCost[0] = pFunc->sSampleDealingFuncs.pfSampleSad[BLOCK_16x16] (pMbCache->SPicData.pEncMb[0],
                        pCurDqLayer->iEncStride[0], pRefLuma, kiLineSizeY);
  if (pWelsMd->bMdUsingSad)
    pWelsMd->iCostLuma = pCurMb->pSadCost[0];
  else
    pWelsMd->iCostLuma = pFunc->sSampleDealingFuncs.pfSampleSatd[BLOCK_16x16] (pMbCache->SPicData.pEncMb[0],
                         pCurDqLayer->iEncStride[0], pRefLuma, kiLineSizeY);
  pWelsMd->iCostSkipMb = pFunc->sSampleDealingFuncs.pfSampleSad[BLOCK_16x16] (pMbCache->SPicData.pEncMb[0],
                         pCurDqLayer->iEncStride[0], pDstLuma, 16);
  pWelsMd->iCostSkipMb += pFunc->sSampleDealingFuncs.pfSampleSad[BLOCK_8x8] (pMbCache->SPicData.pEncMb[1],
                          pCurDqLayer->iEncStride[1], pDstCb, 8);
  pWelsMd->iCostSkipMb += pFunc->sSampleDealingFuncs.pfSampleSad[BLOCK_8x8] (pMbCache->SPicData.pEncMb[2],
                          pCurDqLayer->iEncStride[2], pDstCr, 8);
  pCurMb->sP16x16Mv = sMv;
  pCurDqLayer->pDecPic->sMvList[kiIndex] = sMv;

  pCurMb->uiMbType = MB_TYPE_16x16;
  pWelsMd->sMe.sMe16x16.sMv = sMv;
  PredMv (&pMbCache->sMvComponents, 0, 4, pWelsMd->uiRef, &pWelsMd->sMe.sMe16x16.sMvp);
  pMbCache->sMbMvp[0] = pWelsMd->sMe.sMe16x16.sMvp;
  UpdateP16x16MotionInfo (pMbCache, pCurMb, pWelsMd->uiRef, &sMv);

  WelsMdInterEncode (pEncCtx, pSlice, pCurMb, pMbCache);
  WelsMdInterDoubleCheckPskip (pCurMb, pMbCache);
  return true;
}

}
