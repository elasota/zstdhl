#pragma once

#ifndef __GSTD_ENC_H__
#define __GSTD_ENC_H__

#include <stdint.h>
#include <stddef.h>

#include "zstdhl.h"

struct gstd_EncoderState;
typedef struct gstd_EncoderState gstd_EncoderState_t;

#ifdef __cplusplus
extern "C"
{
#endif

zstdhl_ResultCode_t gstd_Encoder_Create(const zstdhl_EncoderOutputObject_t *output, size_t numLanes, const zstdhl_MemoryAllocatorObject_t *alloc, gstd_EncoderState_t **outEncState);
zstdhl_ResultCode_t gstd_Encoder_AddBlock(gstd_EncoderState_t *encState, const zstdhl_EncBlockDesc_t *blockDesc);
zstdhl_ResultCode_t gstd_Encoder_Finish(gstd_EncoderState_t *encState);
void gstd_Encoder_Destroy(gstd_EncoderState_t *encState);

zstdhl_ResultCode_t gstd_Encoder_Transcode(gstd_EncoderState_t *encState, const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_MemoryAllocatorObject_t *alloc);

#ifdef __cplusplus
}
#endif

#endif

