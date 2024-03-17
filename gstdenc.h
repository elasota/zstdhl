/*
Copyright (c) 2023 Eric Lasota

This software is available under the terms of the MIT license
or the Apache License, Version 2.0.  For more information, see
the included LICENSE.txt file.
*/

#pragma once

#ifndef __GSTD_ENC_H__
#define __GSTD_ENC_H__

#include <stdint.h>
#include <stddef.h>

#include "zstdhl.h"

struct gstd_EncoderState;
typedef struct gstd_EncoderState gstd_EncoderState_t;


enum gstd_Tweak
{
	GSTD_TWEAK_NO_FSE_TABLE_SHUFFLE = (1 << 0),

	GSTD_TWEAK_FIRST_PRIVATE_TWEAK = (1 << 1),
};

#ifdef __cplusplus
extern "C"
{
#endif

zstdhl_ResultCode_t gstd_Encoder_Create(const zstdhl_EncoderOutputObject_t *output, size_t numLanes, uint8_t maxOffsetExtraBits, uint32_t tweaks, const zstdhl_MemoryAllocatorObject_t *alloc, gstd_EncoderState_t **outEncState);
zstdhl_ResultCode_t gstd_Encoder_AddBlock(gstd_EncoderState_t *encState, const zstdhl_EncBlockDesc_t *blockDesc);
zstdhl_ResultCode_t gstd_Encoder_Finish(gstd_EncoderState_t *encState);
void gstd_Encoder_Destroy(gstd_EncoderState_t *encState);

uint8_t gstd_ComputeMaxOffsetExtraBits(uint32_t maxFrameSize);

zstdhl_ResultCode_t gstd_Encoder_Transcode(gstd_EncoderState_t *encState, const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_MemoryAllocatorObject_t *alloc);


#ifdef __cplusplus
}
#endif

#endif

