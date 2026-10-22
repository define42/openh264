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

}

#endif // WELS_UNCHANGED_H__
