/*!
 * \copy
 *     Copyright (c)  2009-2013, Cisco Systems
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
 * \file    cabac_decoder.h
 *
 * \brief   Interfaces introduced for cabac decoder
 *
 * \date    10/10/2014 Created
 *
 *************************************************************************************
 */
#ifndef WELS_CABAC_DECODER_H__
#define WELS_CABAC_DECODER_H__

#include "decoder_context.h"
#include "error_code.h"
#include "wels_common_defs.h"
namespace WelsDec {
static const uint8_t g_kRenormTable256[256] = {
  6, 6, 6, 6, 6, 6, 6, 6,
  5, 5, 5, 5, 5, 5, 5, 5,
  4, 4, 4, 4, 4, 4, 4, 4,
  4, 4, 4, 4, 4, 4, 4, 4,
  3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3,
  3, 3, 3, 3, 3, 3, 3, 3,
  2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 2, 2, 2, 2,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1
};


//1. CABAC context initialization
void WelsCabacGlobalInit (PWelsDecoderContext pCabacCtx);
void WelsCabacContextInit (PWelsDecoderContext  pCtx, uint8_t eSliceType, int32_t iCabacInitIdc, int32_t iQp);

//2. decoding Engine initialization
int32_t InitCabacDecEngineFromBS (PWelsCabacDecEngine pDecEngine, SBitStringAux* pBsAux);
void RestoreCabacDecEngineToBS (PWelsCabacDecEngine pDecEngine, SBitStringAux* pBsAux);
//3. actual decoding
static inline int32_t Read32BitsCabac (PWelsCabacDecEngine pDecEngine, uint32_t& uiValue, int32_t& iNumBitsRead);
int32_t DecodeBinCabacRefill (PWelsCabacDecEngine pDecEngine, uint64_t uiOffset, int32_t iBitsLeft);
int32_t DecodeBypassCabacRefill (PWelsCabacDecEngine pDecEngine);
static inline int32_t DecodeBinCabac (PWelsCabacDecEngine pDecEngine, PWelsCabacCtx pBinCtx, uint32_t& uiBit);
static inline int32_t DecodeBinCabacLocal (PWelsCabacDecEngine pDecEngine, PWelsCabacCtx pBinCtx, uint32_t& uiBit);
static inline int32_t DecodeBypassCabac (PWelsCabacDecEngine pDecEngine, uint32_t& uiBinVal);
static inline int32_t DecodeBypassCabacLocal (PWelsCabacDecEngine pDecEngine, uint32_t& uiBinVal);
int32_t  DecodeTerminateCabac (PWelsCabacDecEngine pDecEngine, uint32_t& uiBinVal);

//4. unary parsing
int32_t DecodeUnaryBinCabac (PWelsCabacDecEngine pDecEngine, PWelsCabacCtx pBinCtx, int32_t iCtxOffset,
                             uint32_t& uiSymVal);

//5. EXGk parsing
int32_t DecodeExpBypassCabac (PWelsCabacDecEngine pDecEngine, int32_t iCount, uint32_t& uiSymVal);
uint32_t DecodeUEGLevelCabac (PWelsCabacDecEngine pDecEngine, PWelsCabacCtx pBinCtx, uint32_t& uiBinVal);
int32_t DecodeUEGMvCabac (PWelsCabacDecEngine pDecEngine, PWelsCabacCtx pBinCtx, uint32_t iMaxC,  uint32_t& uiCode);

#define WELS_CABAC_HALF    0x01FE
#define WELS_CABAC_QUARTER 0x0100
#define WELS_CABAC_FALSE_RETURN(iErrorInfo) \
if(iErrorInfo) { \
  return iErrorInfo; \
}

static inline int32_t Read32BitsCabac (PWelsCabacDecEngine pDecEngine, uint32_t& uiValue, int32_t& iNumBitsRead) {
  intX_t iLeftBytes = pDecEngine->pBuffEnd - pDecEngine->pBuffCurr;
  const uint8_t* pCurr = pDecEngine->pBuffCurr;
  iNumBitsRead = 0;
  uiValue = 0;
  if (iLeftBytes <= 0) {
    return GENERATE_ERROR_NO (ERR_LEVEL_MB_DATA, ERR_CABAC_NO_BS_TO_READ);
  }
  switch (iLeftBytes) {
  case 3:
    uiValue = ((pCurr[0]) << 16 | (pCurr[1]) << 8 | (pCurr[2]));
    pDecEngine->pBuffCurr += 3;
    iNumBitsRead = 24;
    break;
  case 2:
    uiValue = ((pCurr[0]) << 8 | (pCurr[1]));
    pDecEngine->pBuffCurr += 2;
    iNumBitsRead = 16;
    break;
  case 1:
    uiValue = pCurr[0];
    pDecEngine->pBuffCurr += 1;
    iNumBitsRead = 8;
    break;
  default:
    uiValue = ((uint32_t)pCurr[0] << 24) | ((uint32_t)pCurr[1] << 16) | ((uint32_t)pCurr[2] << 8) | pCurr[3];
    pDecEngine->pBuffCurr += 4;
    iNumBitsRead = 32;
    break;
  }
  return ERR_NONE;
}

static inline int32_t DecodeBinCabacRefillInline (PWelsCabacDecEngine pDecEngine, uint64_t uiOffset, int32_t iBitsLeft) {
  uint32_t uiVal = 0;
  int32_t iNumBitsRead = 0;
  int32_t iErrorInfo = Read32BitsCabac (pDecEngine, uiVal, iNumBitsRead);
  pDecEngine->uiOffset = (uiOffset << iNumBitsRead) | uiVal;
  pDecEngine->iBitsLeft = iBitsLeft + iNumBitsRead;
  if (iErrorInfo && pDecEngine->iBitsLeft < 0) {
    return iErrorInfo;
  }
  return ERR_NONE;
}

static inline int32_t DecodeBypassCabacRefillInline (PWelsCabacDecEngine pDecEngine) {
  uint32_t uiVal = 0;
  int32_t iNumBitsRead = 0;
  WELS_CABAC_FALSE_RETURN (Read32BitsCabac (pDecEngine, uiVal, iNumBitsRead));
  pDecEngine->uiOffset = (pDecEngine->uiOffset << iNumBitsRead) | uiVal;
  pDecEngine->iBitsLeft = iNumBitsRead;
  return ERR_NONE;
}

// Hot path of the arithmetic decoder, inlined into the syntax parsers. kbInlineRefill selects whether the (rare)
// bitstream refill is inlined too: callers that work on a local copy of the engine use it so that the copy never
// escapes and can be kept in registers.
static inline int32_t DecodeBinCabacImpl (PWelsCabacDecEngine pDecEngine, PWelsCabacCtx pBinCtx, uint32_t& uiBinVal,
    const bool kbInlineRefill) {
  const uint32_t uiState = pBinCtx->uiState;
  uint64_t uiOffset = pDecEngine->uiOffset;
  uint64_t uiRange = pDecEngine->uiRange;
  int32_t iBitsLeft = pDecEngine->iBitsLeft;
  int32_t iRenorm = 1;
  const uint32_t uiRangeLPS = g_kuiCabacRangeLps[uiState][ (uiRange >> 6) & 0x03];
  uiBinVal = pBinCtx->uiMPS;
  uiRange -= uiRangeLPS;
  const uint64_t uiScaledRange = uiRange << iBitsLeft;
  if (uiOffset >= uiScaledRange) { //LPS
    uiOffset -= uiScaledRange;
    uiBinVal ^= 0x0001;
    if (!uiState)
      pBinCtx->uiMPS ^= 0x01;
    pBinCtx->uiState = g_kuiStateTransTable[uiState][0];
    iRenorm = g_kRenormTable256[uiRangeLPS];
    uiRange = (uiRangeLPS << iRenorm);
  } else {  //MPS
    pBinCtx->uiState = g_kuiStateTransTable[uiState][1];
    if (uiRange >= WELS_CABAC_QUARTER) {
      pDecEngine->uiRange = uiRange;
      return ERR_NONE;
    }
    uiRange <<= 1;
  }
  //Renorm
  pDecEngine->uiRange = uiRange;
  iBitsLeft -= iRenorm;
  if (iBitsLeft > 0) {
    pDecEngine->iBitsLeft = iBitsLeft;
    pDecEngine->uiOffset = uiOffset;
    return ERR_NONE;
  }
  if (kbInlineRefill)
    return DecodeBinCabacRefillInline (pDecEngine, uiOffset, iBitsLeft);
  return DecodeBinCabacRefill (pDecEngine, uiOffset, iBitsLeft);
}

static inline int32_t DecodeBypassCabacImpl (PWelsCabacDecEngine pDecEngine, uint32_t& uiBinVal,
    const bool kbInlineRefill) {
  if (pDecEngine->iBitsLeft <= 0) {
    WELS_CABAC_FALSE_RETURN (kbInlineRefill ? DecodeBypassCabacRefillInline (pDecEngine) : DecodeBypassCabacRefill (
                               pDecEngine));
  }
  const int32_t iBitsLeft = pDecEngine->iBitsLeft - 1;
  const uint64_t uiRangeValue = (pDecEngine->uiRange << iBitsLeft);
  uint64_t uiOffset = pDecEngine->uiOffset;
  pDecEngine->iBitsLeft = iBitsLeft;
  if (uiOffset >= uiRangeValue) {
    pDecEngine->uiOffset = uiOffset - uiRangeValue;
    uiBinVal = 1;
    return ERR_NONE;
  }
  uiBinVal = 0;
  return ERR_NONE;
}

static inline int32_t DecodeBinCabac (PWelsCabacDecEngine pDecEngine, PWelsCabacCtx pBinCtx, uint32_t& uiBinVal) {
  return DecodeBinCabacImpl (pDecEngine, pBinCtx, uiBinVal, false);
}

// Same as DecodeBinCabac but fully inlined; meant for a local (stack) copy of the engine.
static inline int32_t DecodeBinCabacLocal (PWelsCabacDecEngine pDecEngine, PWelsCabacCtx pBinCtx, uint32_t& uiBinVal) {
  return DecodeBinCabacImpl (pDecEngine, pBinCtx, uiBinVal, true);
}

static inline int32_t DecodeBypassCabac (PWelsCabacDecEngine pDecEngine, uint32_t& uiBinVal) {
  return DecodeBypassCabacImpl (pDecEngine, uiBinVal, false);
}

// Same as DecodeBypassCabac but fully inlined; meant for a local (stack) copy of the engine.
static inline int32_t DecodeBypassCabacLocal (PWelsCabacDecEngine pDecEngine, uint32_t& uiBinVal) {
  return DecodeBypassCabacImpl (pDecEngine, uiBinVal, true);
}
}
#endif
