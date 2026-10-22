/*!
 * \copy
 *     Copyright (c)  2009-2013, Cisco Systems
 *     All rights reserved. BSD-2-Clause; see LICENSE.
 *
 * \file    unchanged.h
 *
 * \brief   Optional unchanged-macroblock hints (DevBox-Gateway-kvm extension).
 *
 *          A caller may certify, per macroblock, that every source sample is
 *          unchanged since the preceding successfully encoded picture. In the
 *          fixed-QP, single-reference real-time configuration, such a block
 *          whose collocated reference block was a zero-motion P_SKIP is coded
 *          as P_SKIP without mode decision. This mirrors the pure-Go port's
 *          UnchangedMacroblocks hint and produces the same bitstream.
 */
#ifndef WELS_UNCHANGED_H__
#define WELS_UNCHANGED_H__

#include "typedefs.h"
#include "codec_app_def.h"

namespace WelsEnc {

struct TagWelsEncCtx;
struct TagWelsSvcCodingParam;
struct TagWelsMD;
struct TagSlice;
struct TagMB;

bool UnchangedConfiguration (const TagWelsSvcCodingParam* pParam);
void UnchangedFrameBegin (TagWelsEncCtx* pCtx, const SSourcePicture* pSrc);
void UnchangedFrameFinish (TagWelsEncCtx* pCtx, const SSourcePicture* pSrc, const SFrameBSInfo* pBsInfo, bool bSuccess);
void UnchangedFrameReset (TagWelsEncCtx* pCtx);

// Before neighbor caches are loaded: only the cases whose skip predictor is
// provably zero from the left and top neighbors.
bool TryUnchangedMacroblockEarly (TagWelsEncCtx* pCtx, TagWelsMD* pMd, TagSlice* pSlice, TagMB* pCurMb);
// Inside inter mode decision, with the full median skip predictor.
bool TryUnchangedMacroblock (TagWelsEncCtx* pCtx, TagWelsMD* pMd, TagSlice* pSlice, TagMB* pCurMb);
// Inside inter mode decision: codes a macroblock with a motion hint as P16x16
// with the hinted full-pel vector instead of searching.
bool TryMotionHintMacroblock (TagWelsEncCtx* pCtx, TagWelsMD* pMd, TagSlice* pSlice, TagMB* pCurMb);

// Where WelsMdInterMbLoop records each finished macroblock: *pStamp when it
// is a zero-motion P_SKIP, else 0. NULL when this picture records nothing.
uint32_t* UnchangedZeroSkipStamps (TagWelsEncCtx* pCtx, uint32_t* pStamp);
// Whether WelsMdInterMbLoop may code runs of early-skipped macroblocks with
// EncodeUnchangedRun; constant for the current picture.
bool UnchangedRunsEnabled (TagWelsEncCtx* pCtx);
// Codes the run of macroblocks from iMbIdx that TryUnchangedMacroblockEarly
// would skip, at most iMaxCount, leaving the same macroblock, picture, slice
// and cache state as the per-macroblock path. Returns the run length; when it
// is positive, *pNextMbIdx receives the slice's next macroblock or -1.
int32_t EncodeUnchangedRun (TagWelsEncCtx* pCtx, TagWelsMD* pMd, TagSlice* pSlice, int32_t iMbIdx,
                            int32_t iMaxCount, int32_t* pNextMbIdx);
// Whether this picture has so few changed macroblocks that its slices are
// cheaper to code on the calling thread than to hand to the thread pool.
bool UnchangedPreferCallerThread (TagWelsEncCtx* pCtx);

}

#endif // WELS_UNCHANGED_H__
