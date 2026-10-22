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
  // bFinalZeroSkip is read only below iFinalZeroSkipCount; leave it as is.
  memset (&pCtx->sUnchanged, 0, offsetof (SUnchangedFrameState, bFinalZeroSkip));
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
  if (!bValidSource || kiCount < 1 || kiCount > UNCHANGED_MAX_MB_COUNT) {
    pState->iFinalZeroSkipCount = 0;
    return;
  }
  // Called after a successful frame, after every slice thread finished.
  for (int32_t i = 0; i < kiCount; ++i) {
    const SMB* kpMb = &kpLayer->sMbDataP[i];
    pState->bFinalZeroSkip[i] = kpMb->uiMbType == MB_TYPE_SKIP && kpMb->sMv[0].iMvX == 0 && kpMb->sMv[0].iMvY == 0;
  }
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
  pState->pReuseDestination = NULL;
  if (pSrc == NULL || !UnchangedConfiguration (pCtx->pSvcParam))
    return;
  const SWelsSvcCodingParam* kpParam = pCtx->pSvcParam;
  const bool kbMatchingGeometry = pSrc->iPicWidth == kpParam->iPicWidth && pSrc->iPicHeight == kpParam->iPicHeight &&
                                  pSrc->iPicWidth == kpParam->sDependencyLayers[0].iActualWidth &&
                                  pSrc->iPicHeight == kpParam->sDependencyLayers[0].iActualHeight;
  if (!kbMatchingGeometry || pSrc->iPicWidth < 16 || pSrc->iPicHeight < 16)
    return;
  const int64_t kiCount = ((int64_t)pSrc->iPicWidth + 15) / 16 * (((int64_t)pSrc->iPicHeight + 15) / 16);
  const bool kbValidPrevious = pState->pReference != NULL && pState->iWidth == pSrc->iPicWidth &&
                               pState->iHeight == pSrc->iPicHeight && pState->iQp == kpParam->sSpatialLayers[0].iDLayerQp;
  if (kbValidPrevious && pSrc->pUnchangedMacroblocks != NULL && (int64_t)pSrc->iUnchangedMacroblockCount == kiCount) {
    pState->pMacroblocks = pSrc->pUnchangedMacroblocks;
    pState->iMacroblockCount = pSrc->iUnchangedMacroblockCount;
    pState->pReuseDestination = ReusableSource (pState);
  }
}

void UnchangedFrameFinish (sWelsEncCtx* pCtx, const SSourcePicture* pSrc, const SFrameBSInfo* pBsInfo, bool bSuccess) {
  SUnchangedFrameState* pState = &pCtx->sUnchanged;
  pState->pMacroblocks = NULL;
  pState->iMacroblockCount = 0;
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

static bool UnchangedMacroblockEligible (sWelsEncCtx* pCtx, const SMB* pCurMb) {
  const SUnchangedFrameState* kpState = &pCtx->sUnchanged;
  const int32_t kiIndex = pCurMb->iMbXY;
  if (kiIndex < 0 || kiIndex >= kpState->iMacroblockCount || kpState->pMacroblocks == NULL ||
      !kpState->pMacroblocks[kiIndex])
    return false;
  const SDqLayer* kpLayer = pCtx->pCurDqLayer;
  const SPicture* kpReference = kpLayer->pRefPic;
  const bool kbValidReference = pCtx->eSliceType == P_SLICE && pCtx->iNumRef0 == 1 &&
                                kpReference != NULL && kpReference == kpState->pReference && kpReference == pCtx->pRefList0[0] &&
                                kpReference->iFrameNum == kpState->iFrameNum && kpReference->iFramePoc == kpState->iPoc;
  if (!kbValidReference || kpLayer->iLoopFilterDisableIdc != 1 || (int32_t)pCurMb->uiLumaQp != kpState->iQp)
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
                          kiIndex < kpState->iFinalZeroSkipCount && kpState->bFinalZeroSkip[kiIndex];
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
