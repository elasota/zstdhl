/*
Copyright (c) 2023 Eric Lasota

This software is available under the terms of the MIT license
or the Apache License, Version 2.0.  For more information, see
the included LICENSE.txt file.
*/

#include "zstdhl.h"
#include "zstdhl_util.h"
#include "zstdhl_internal.h"

#ifdef __cplusplus
#define ZSTDHL_EXTERN extern "C"
#else
#define ZSTDHL_EXTERN
#endif

#define ZSTDHL_ALLOW_DECL_AFTER_STATEMENT

#ifdef ZSTDHL_ALLOW_DECL_AFTER_STATEMENT
#define ZSTDHL_DECL(d) d
#else
#define ZSTDHL_DECL(d)
#endif

#define ZSTDHL_LESS_THAN_ONE_VALUE ((uint32_t)0xffffffffu)

int zstdhl_Log2_8(uint8_t value)
{
	int result = 0;

	if (value & 0xf0)
	{
		value >>= 4;
		result += 4;
	}

	if (value & 0xc)
	{
		value >>= 2;
		result += 2;
	}

	if (value & 0x2)
	{
		value >>= 1;
		result += 1;
	}

	return result;
}

int zstdhl_Log2_16(uint16_t value)
{
	if (value & 0xff00u)
		return zstdhl_Log2_8((value >> 8) & 0xffu) + 8;
	return zstdhl_Log2_8((uint8_t)value);
}

int zstdhl_Log2_32(uint32_t value)
{
	if (value & 0xffff0000u)
		return zstdhl_Log2_16((value >> 16) & 0xffffu) + 16;
	return zstdhl_Log2_16((uint16_t)value);
}

uint32_t zstdhl_ReverseBits32(uint32_t value)
{
	value = ((value << 16) & 0xffff0000u) | ((value >> 16) & 0x0000ffffu);
	value = ((value << 8)  & 0xff00ff00u) | ((value >> 8)  & 0x00ff00ffu);
	value = ((value << 4)  & 0xf0f0f0f0u) | ((value >> 4)  & 0x0f0f0f0fu);
	value = ((value << 2)  & 0xccccccccu) | ((value >> 2)  & 0x33333333u);
	value = ((value << 1)  & 0xaaaaaaaau) | ((value >> 1)  & 0x55555555u);

	return value;
}

int zstdhl_IsPowerOf2(uint32_t value)
{
	if (value & (value - 1u))
		return 0;

	return 1;
}

size_t zstdhl_BigNum_CountBits(const uint32_t *parts)
{
	size_t numBits = 0;
	while ((*parts) == 0)
	{
		parts++;
		numBits += 32;
	}

	return numBits + zstdhl_Log2_32(*parts) + 1u;
}

zstdhl_ResultCode_t zstdhl_BigNum_SubtractU32(uint32_t *parts, size_t *numBitsPtr, uint32_t v)
{
	size_t numBits = *numBitsPtr;
	size_t numDWords = (numBits + 31u) / 32u;

	if (parts[0] >= v)
		parts[0] -= v;
	else
	{
		size_t borrowOffset = 0;
		size_t numDWordsRemaining = numDWords;

		if (numDWords == 1)
			return ZSTDHL_RESULT_INTEGER_OVERFLOW;

		int borrow = 1;

		parts[0] -= v;
		while (borrow)
		{
			borrowOffset++;

			borrow = (parts[borrowOffset] > 0) ? 1 : 0;
			parts[borrowOffset]--;
		}
	}

	*numBitsPtr = zstdhl_BigNum_CountBits(parts);
	return ZSTDHL_RESULT_OK;
}

void zstdhl_ReportErrorCode(zstdhl_ResultCode_t errorCode)
{
	if (errorCode < ZSTDHL_RESULT_SOFT_FAULT)
	{
		int n = 0;
	}
}

zstdhl_ResultCode_t zstdhl_ReadChecked(const zstdhl_StreamSourceObject_t *streamSource, void *dest, size_t numBytes, zstdhl_ResultCode_t failureResult)
{
	if (streamSource->m_readBytesFunc(streamSource->m_userdata, dest, numBytes) != numBytes)
		return failureResult;

	return ZSTDHL_RESULT_OK;
}

enum zstdhl_BufferID
{
	ZSTDHL_BUFFER_FSE_BITSTREAM,
	ZSTDHL_BUFFER_HUFFMAN_WEIGHT_FSE_TABLE,
	ZSTDHL_BUFFER_LITERALS,
	ZSTDHL_BUFFER_HUFFMAN_BITSTREAM,

	ZSTDHL_BUFFER_LIT_LENGTH_FSE_TABLE,
	ZSTDHL_BUFFER_OFFSET_FSE_TABLE,
	ZSTDHL_BUFFER_MATCH_LENGTH_FSE_TABLE,

	ZSTDHL_BUFFER_OFFSET_PROBS_1,
	ZSTDHL_BUFFER_OFFSET_PROBS_2,
	ZSTDHL_BUFFER_OFFSET_BIGNUM,

	ZSTDHL_BUFFER_SEQ_TEMPS,

	ZSTDHL_BUFFER_COUNT,
};

typedef struct zstdhl_Buffers
{
	void *m_buffers[ZSTDHL_BUFFER_COUNT];

	zstdhl_MemoryAllocatorObject_t m_alloc;
} zstdhl_Buffers_t;

void zstdhl_Buffers_Init(zstdhl_Buffers_t *buffers, const zstdhl_MemoryAllocatorObject_t *alloc)
{
	int i = 0;

	for (i = 0; i < ZSTDHL_BUFFER_COUNT; i++)
		buffers->m_buffers[i] = NULL;

	buffers->m_alloc.m_reallocFunc = alloc->m_reallocFunc;
	buffers->m_alloc.m_userdata = alloc->m_userdata;
}

zstdhl_ResultCode_t zstdhl_Buffers_Alloc(zstdhl_Buffers_t *buffers, int bufferID, size_t size, void **outPtr)
{
	void *ptr = NULL;

	if (buffers->m_buffers[bufferID])
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	ptr = buffers->m_alloc.m_reallocFunc(buffers->m_alloc.m_userdata, NULL, size);
	if (!ptr)
		return ZSTDHL_RESULT_OUT_OF_MEMORY;

	buffers->m_buffers[bufferID] = ptr;

	if (outPtr)
		*outPtr = ptr;

	return ZSTDHL_RESULT_OK;
}

void zstdhl_Buffers_Dealloc(zstdhl_Buffers_t *buffers, int bufferID)
{
	if (buffers->m_buffers[bufferID])
	{
		buffers->m_alloc.m_reallocFunc(buffers->m_alloc.m_userdata, buffers->m_buffers[bufferID], 0);
		buffers->m_buffers[bufferID] = NULL;
	}
}

void zstdhl_Buffers_DeallocAll(zstdhl_Buffers_t *buffers)
{
	int i = 0;

	for (i = 0; i < ZSTDHL_BUFFER_COUNT; i++)
		zstdhl_Buffers_Dealloc(buffers, i);
}


typedef struct zstdhl_SequencesSubstreamCompressionDef
{
	int m_isDefined;
	zstdhl_FSETableDef_t m_fseTableDef;
} zstdhl_SequencesSubstreamCompressionDef_t;

typedef struct zstdhl_FramePersistentState
{
	int m_haveHuffmanTable;
	zstdhl_HuffmanTableDec_t m_huffmanTable;
	zstdhl_SequencesSubstreamCompressionDef_t m_literalLengthsCDef;
	zstdhl_SequencesSubstreamCompressionDef_t m_offsetsCDef;
	zstdhl_SequencesSubstreamCompressionDef_t m_matchLengthsCDef;

	uint32_t m_litLengthProbs[36];
	uint32_t m_offsetProbs[29];
	uint32_t m_matchLengthProbs[53];
} zstdhl_FramePersistentState_t;

void zstdhl_FramePersistentState_Init(zstdhl_FramePersistentState_t *pstate)
{
	pstate->m_haveHuffmanTable = 0;

	pstate->m_literalLengthsCDef.m_isDefined = 0;
	pstate->m_matchLengthsCDef.m_isDefined = 0;
	pstate->m_offsetsCDef.m_isDefined = 0;
}

typedef struct zstdhl_ForwardBitstream
{
	zstdhl_StreamSourceObject_t m_streamSource;
	uint32_t m_bits;
	uint8_t m_numBits;
} zstdhl_ForwardBitstream_t;

static zstdhl_ResultCode_t zstdhl_ForwardBitstream_Init(zstdhl_ForwardBitstream_t *bitstream, const zstdhl_StreamSourceObject_t *streamSource)
{
	bitstream->m_streamSource.m_readBytesFunc = streamSource->m_readBytesFunc;
	bitstream->m_streamSource.m_userdata = streamSource->m_userdata;
	bitstream->m_numBits = 0;
	bitstream->m_bits = 0;

	return ZSTDHL_RESULT_OK;
}

static void zstdhl_SwapToLittleEndian32(uint32_t *bits)
{
	uint8_t *bytes = (uint8_t *)bits;
	uint32_t swapped = ((uint32_t)bytes[0]) | (((uint32_t)bytes[1]) << 8) | (((uint32_t)bytes[2]) << 16) | (((uint32_t)bytes[3]) << 24);

	*bits = swapped;
}

static zstdhl_ResultCode_t zstdhl_ForwardBitstream_ReadBits(zstdhl_ForwardBitstream_t *bitstream, uint8_t numBitsToRead, uint32_t *outBits)
{
	int bitPos = 0;
	uint32_t bits = 0;
	uint32_t moreBits = 0;

	if (numBitsToRead == 0)
	{
		*outBits = 0;
		return ZSTDHL_RESULT_OK;
	}

	if (bitstream->m_numBits < numBitsToRead)
	{
		bitPos = bitstream->m_numBits;
		bits = bitstream->m_bits;

		bitPos = bitstream->m_numBits;
		numBitsToRead -= bitstream->m_numBits;

		bitstream->m_numBits = 0;
		bitstream->m_bits = 0;

		ZSTDHL_DECL(uint8_t) bytesToRead = (numBitsToRead + 7) / 8;

		ZSTDHL_CHECKED(zstdhl_ReadChecked(&bitstream->m_streamSource, &bitstream->m_bits, bytesToRead, ZSTDHL_RESULT_FORWARD_BITSTREAM_TRUNCATED));
		bitstream->m_numBits = bytesToRead * 8;
		zstdhl_SwapToLittleEndian32(&bitstream->m_bits);
	}

	ZSTDHL_DECL(uint32_t) mask = ((uint32_t)1) << (numBitsToRead - 1);
	mask--;
	mask <<= 1;
	mask++;

	bits |= (bitstream->m_bits & mask) << bitPos;
	bitstream->m_bits >>= numBitsToRead;
	bitstream->m_numBits -= numBitsToRead;

	*outBits = bits;

	return ZSTDHL_RESULT_OK;
}

typedef struct zstdhl_ReverseBitstream
{
	const uint8_t *m_bytes;
	uint32_t m_bytesAvailable;
	uint32_t m_bits;
	uint8_t m_numBits;
} zstdhl_ReverseBitstream_t;

static zstdhl_ResultCode_t zstdhl_ReverseBitstream_Init(zstdhl_ReverseBitstream_t *bitstream, const uint8_t *bytes, uint32_t bytesAvailable)
{
	if (bytesAvailable == 0)
		return ZSTDHL_RESULT_REVERSE_BITSTREAM_EMPTY;

	bytesAvailable--;

	ZSTDHL_DECL(uint32_t) bits = bytes[bytesAvailable];

	if (bits == 0)
		return ZSTDHL_RESULT_REVERSE_BITSTREAM_MISSING_PAD_BIT;

	// Drop padding bit
	ZSTDHL_DECL(uint8_t) numBits = zstdhl_Log2_8((uint8_t)bits);
	bits -= (1 << numBits);

	while (numBits < 25)
	{
		if (bytesAvailable == 0)
			break;

		bytesAvailable--;
		bits = (bits << 8) | bytes[bytesAvailable];
		numBits += 8;
	}

	bitstream->m_bytes = bytes;
	bitstream->m_bytesAvailable = bytesAvailable;
	bitstream->m_numBits = numBits;
	bitstream->m_bits = bits;

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_ReverseBitstream_PeekBits(zstdhl_ReverseBitstream_t *bitstream, uint8_t numBitsToRead, uint32_t *outBits, zstdhl_ResultCode_t shortfallResult)
{
	const uint8_t *bytes = bitstream->m_bytes;
	uint32_t bytesAvailable = bitstream->m_bytesAvailable;
	uint32_t bits = bitstream->m_bits;
	uint8_t numBits = bitstream->m_numBits;

	if (numBitsToRead == 0)
	{
		*outBits = 0;
		return ZSTDHL_RESULT_OK;
	}

	if (numBits < numBitsToRead)
	{
		while (numBits < 25)
		{
			if (bytesAvailable == 0)
				break;

			bytesAvailable--;
			bits = (bits << 8) | bytes[bytesAvailable];
			numBits += 8;
		}

		bitstream->m_bits = bits;
		bitstream->m_bytesAvailable = bytesAvailable;
		bitstream->m_numBits = numBits;
	}

	if (numBitsToRead <= numBits)
	{
		ZSTDHL_DECL(uint8_t) droppedBits = numBits - numBitsToRead;

		*outBits = (bits >> droppedBits);
		return ZSTDHL_RESULT_OK;
	}
	else
	{
		ZSTDHL_DECL(uint8_t) addedBits = numBitsToRead - numBits;

		*outBits = (bits << addedBits);
		return shortfallResult;
	}

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_ReverseBitstream_ConsumeBits(zstdhl_ReverseBitstream_t *bitstream, uint8_t numBitsToRead)
{
	if (numBitsToRead == 0)
		return ZSTDHL_RESULT_OK;

	if (bitstream->m_numBits < numBitsToRead)
		return ZSTDHL_RESULT_REVERSE_BITSTREAM_TRUNCATED;

	bitstream->m_numBits -= numBitsToRead;
	bitstream->m_bits &= (1u << bitstream->m_numBits) - 1;

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_ReverseBitstream_ReadBitsComplete(zstdhl_ReverseBitstream_t *bitstream, uint8_t numBitsToRead, uint32_t *outBits)
{
	ZSTDHL_CHECKED(zstdhl_ReverseBitstream_PeekBits(bitstream, numBitsToRead, outBits, ZSTDHL_RESULT_REVERSE_BITSTREAM_TRUNCATED));
	ZSTDHL_CHECKED(zstdhl_ReverseBitstream_ConsumeBits(bitstream, numBitsToRead));

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_ReverseBitstream_ReadBitsCompleteSoftFault(zstdhl_ReverseBitstream_t *bitstream, uint8_t numBitsToRead, uint32_t *outBits)
{
	ZSTDHL_CHECKED(zstdhl_ReverseBitstream_PeekBits(bitstream, numBitsToRead, outBits, ZSTDHL_RESULT_REVERSE_BITSTREAM_TRUNCATED_SOFT_FAULT));
	ZSTDHL_CHECKED(zstdhl_ReverseBitstream_ConsumeBits(bitstream, numBitsToRead));

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_ReverseBitstream_ReadBitsPartial(zstdhl_ReverseBitstream_t *bitstream, uint8_t numBitsToRead, uint8_t *outNumBitsRead, uint32_t *outBits)
{
	if (bitstream->m_numBits < numBitsToRead)
	{
		uint8_t extraBytesRequired = ((numBitsToRead - bitstream->m_numBits) + 7u) / 8u;
		if (bitstream->m_bytesAvailable < extraBytesRequired)
			numBitsToRead = bitstream->m_bytesAvailable * 8u + bitstream->m_numBits;
	}

	*outNumBitsRead = numBitsToRead;

	return zstdhl_ReverseBitstream_ReadBitsComplete(bitstream, numBitsToRead, outBits);
}

typedef struct zstdhl_SliceStreamSource
{
	zstdhl_StreamSourceObject_t m_streamSource;
	size_t m_sizeRemaining;
} zstdhl_SliceStreamSource_t;

static size_t zstdhl_SliceStreamSource_ReadBytes(void *userdata, void *dest, size_t numBytes)
{
	zstdhl_SliceStreamSource_t *streamSource = (zstdhl_SliceStreamSource_t *)userdata;

	if (numBytes > streamSource->m_sizeRemaining)
		numBytes = streamSource->m_sizeRemaining;

	numBytes = streamSource->m_streamSource.m_readBytesFunc(streamSource->m_streamSource.m_userdata, dest, numBytes);
	streamSource->m_sizeRemaining -= numBytes;

	return numBytes;
}

static zstdhl_ResultCode_t zstdhl_SliceStreamSource_FlushRemainder(zstdhl_SliceStreamSource_t *slice, zstdhl_ResultCode_t failureResult)
{
#ifndef ZSTDHL_ALLOW_DECL_AFTER_STATEMENT
	size_t bytesToRead = 0;
	size_t bytesRead = 0;
#endif
	uint8_t buffer[1024];

	while (slice->m_sizeRemaining > 0)
	{
		ZSTDHL_DECL(size_t) bytesToRead = slice->m_sizeRemaining;
		if (bytesToRead > sizeof(buffer))
			bytesToRead = sizeof(buffer);

		ZSTDHL_DECL(size_t) bytesRead = zstdhl_SliceStreamSource_ReadBytes(slice, buffer, bytesToRead);

		if (bytesRead < bytesToRead)
			return failureResult;
	}

	return ZSTDHL_RESULT_OK;
}

void zstdhl_MemBufferStreamSource_Init(zstdhl_MemBufferStreamSource_t *streamSource, const void *data, size_t size)
{
	streamSource->m_data = data;
	streamSource->m_sizeRemaining = size;
}

size_t zstdhl_MemBufferStreamSource_ReadBytes(void *userdata, void *dest, size_t numBytes)
{
	zstdhl_MemBufferStreamSource_t *streamSource = (zstdhl_MemBufferStreamSource_t *)userdata;
	const uint8_t *srcBytes = (const uint8_t *)streamSource->m_data;
	uint8_t *destBytes = (uint8_t *)dest;
	size_t i = 0;

	if (numBytes > streamSource->m_sizeRemaining)
		numBytes = streamSource->m_sizeRemaining;

	streamSource->m_sizeRemaining -= numBytes;
	streamSource->m_data = srcBytes + numBytes;

	for (i = 0; i < numBytes; i++)
		destBytes[i] = srcBytes[i];

	return numBytes;
}

static zstdhl_ResultCode_t zstdhl_ParseFrameHeader(const zstdhl_StreamSourceObject_t *streamSource, zstdhl_FrameHeaderDesc_t *outFrameHeader)
{
#ifndef ZSTDHL_ALLOW_DECL_AFTER_STATEMENT
	uint8_t frameHeaderDescriptor = 0;
	uint8_t dictionaryIDFlag = 0;
	uint8_t contentChecksumFlag = 0;
	uint8_t frameHeaderReservedBit = 0;
	uint8_t frameHeaderUnusedBit = 0;
	uint8_t singleSegmentFlag = 0;
	uint8_t frameContentSizeFlag = 0;
	uint8_t fcsSize = 0;
	uint8_t windowDescriptorSize = 0;

	uint8_t dictionaryIDSize = 0;
	uint8_t extraBytesNeeded = 0;
	uint8_t readOffset = 0;
	uint8_t i = 0;
	uint8_t windowDescriptor = 0;
	uint8_t windowDescriptorMantissa = 0;
	uint8_t windowDescriptorExponent = 0;
	zstdhl_ResultCode_t result = ZSTDHL_RESULT_OK;
#endif

	uint8_t initialFrameHeader[6];
	uint8_t frameHeader[14];

	ZSTDHL_CHECKED(zstdhl_ReadChecked(streamSource, initialFrameHeader, 6, ZSTDHL_RESULT_FRAME_HEADER_TRUNCATED));

	if (initialFrameHeader[0] != 0x28 || initialFrameHeader[1] != 0xb5 || initialFrameHeader[2] != 0x2f || initialFrameHeader[3] != 0xfd)
		return ZSTDHL_RESULT_MAGIC_NUMBER_MISMATCH;

	// Parse frame header
	frameHeader[0] = initialFrameHeader[4];
	frameHeader[1] = initialFrameHeader[5];

	ZSTDHL_DECL(uint8_t) frameHeaderDescriptor = frameHeader[0];

	ZSTDHL_DECL(uint8_t) dictionaryIDFlag = (frameHeaderDescriptor & 3);
	ZSTDHL_DECL(uint8_t) contentChecksumFlag = ((frameHeaderDescriptor >> 2) & 1);
	ZSTDHL_DECL(uint8_t) frameHeaderReservedBit = ((frameHeaderDescriptor >> 3) & 1);
	ZSTDHL_DECL(uint8_t) frameHeaderUnusedBit = ((frameHeaderDescriptor >> 4) & 1);
	ZSTDHL_DECL(uint8_t) singleSegmentFlag = ((frameHeaderDescriptor >> 5) & 1);
	ZSTDHL_DECL(uint8_t) frameContentSizeFlag = ((frameHeaderDescriptor >> 6) & 3);

	if (frameHeaderReservedBit)
		return ZSTDHL_RESULT_FRAME_HEADER_RESERVED_BIT_WAS_SET;

	ZSTDHL_DECL(uint8_t) fcsSize = 0;
	if (frameContentSizeFlag == 0 && !singleSegmentFlag)
		fcsSize = 0;
	else
		fcsSize = (1 << frameContentSizeFlag);

	ZSTDHL_DECL(uint8_t) windowDescriptorSize = 1;
	if (singleSegmentFlag)
		windowDescriptorSize = 0;

	ZSTDHL_DECL(uint8_t) dictionaryIDSize = ((1 << dictionaryIDFlag) >> 1);

	ZSTDHL_DECL(uint8_t) extraBytesNeeded = (fcsSize + windowDescriptorSize + dictionaryIDSize - 1);
	if (extraBytesNeeded > 0)
	{
		ZSTDHL_CHECKED(zstdhl_ReadChecked(streamSource, frameHeader + 2, extraBytesNeeded, ZSTDHL_RESULT_FRAME_HEADER_TRUNCATED));
	}

	ZSTDHL_DECL(uint8_t) readOffset = 1;

	outFrameHeader->m_dictionaryID = 0;
	outFrameHeader->m_frameContentSize = 0;

	if (windowDescriptorSize > 0)
	{
		ZSTDHL_DECL(uint8_t) windowDescriptor = frameHeader[readOffset++];
		ZSTDHL_DECL(uint8_t) windowDescriptorMantissa = (windowDescriptor & 7);
		ZSTDHL_DECL(uint8_t) windowDescriptorExponent = ((windowDescriptor >> 3) & 0x1f);

		outFrameHeader->m_windowSize = ((uint64_t)(windowDescriptor + 8)) << (7 + windowDescriptorExponent);
	}

	outFrameHeader->m_dictionaryID = 0;
	if (dictionaryIDSize > 0)
	{
		outFrameHeader->m_haveDictionaryID = 1;
		for (ZSTDHL_DECL(uint8_t) i = 0; i < dictionaryIDSize; i++)
			outFrameHeader->m_dictionaryID |= ((uint64_t)frameHeader[readOffset++]) << (i * 8);
	}
	else
		outFrameHeader->m_haveDictionaryID = 0;

	outFrameHeader->m_frameContentSize = 0;
	if (fcsSize > 0)
	{
		outFrameHeader->m_haveFrameContentSize = 1;
		for (ZSTDHL_DECL(uint8_t) i = 0; i < fcsSize; i++)
			outFrameHeader->m_frameContentSize |= ((uint64_t)frameHeader[readOffset++]) << (i * 8);
	}
	else
		outFrameHeader->m_haveFrameContentSize = 0;

	if (windowDescriptorSize == 0)
	{
		outFrameHeader->m_windowSize = outFrameHeader->m_frameContentSize;
		outFrameHeader->m_haveWindowSize = 0;
	}
	else
		outFrameHeader->m_haveWindowSize = 1;

	outFrameHeader->m_haveContentChecksum = contentChecksumFlag;
	outFrameHeader->m_isSingleSegment = singleSegmentFlag;

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_ParseRLEBlock(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint32_t blockSize)
{
	zstdhl_ResultCode_t result = ZSTDHL_RESULT_OK;
	zstdhl_BlockRLEDesc_t rleDesc;

	ZSTDHL_CHECKED(zstdhl_ReadChecked(streamSource, &rleDesc.m_value, 1, ZSTDHL_RESULT_BLOCK_TRUNCATED));

	rleDesc.m_count = blockSize;

	ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_BLOCK_RLE_DATA, &rleDesc));

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_ParseRawBlock(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint32_t blockSize, zstdhl_ElementType_t elementType)
{
	zstdhl_BlockUncompressedDesc_t uncompressedDesc;
	uint8_t bytes[1024];

	uncompressedDesc.m_data = bytes;

	while (blockSize > 0)
	{
		uncompressedDesc.m_size = sizeof(bytes);
		if (uncompressedDesc.m_size > blockSize)
			uncompressedDesc.m_size = blockSize;

		uncompressedDesc.m_data = bytes;

		ZSTDHL_CHECKED(zstdhl_ReadChecked(streamSource, bytes, uncompressedDesc.m_size, ZSTDHL_RESULT_BLOCK_TRUNCATED));
		ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, elementType, &uncompressedDesc));

		blockSize -= (uint32_t) uncompressedDesc.m_size;
	}

	return ZSTDHL_RESULT_OK;
}


zstdhl_ResultCode_t zstdhl_ParseRawLiteralsSection(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint32_t regeneratedSize)
{
	zstdhl_SliceStreamSource_t sliceSource;
	zstdhl_StreamSourceObject_t litStreamObj;
	zstdhl_LiteralsSectionDesc_t litSectionDesc;

	sliceSource.m_streamSource = *streamSource;
	sliceSource.m_sizeRemaining = regeneratedSize;

	litStreamObj.m_readBytesFunc = zstdhl_SliceStreamSource_ReadBytes;
	litStreamObj.m_userdata = &sliceSource;

	litSectionDesc.m_huffmanStreamMode = ZSTDHL_HUFFMAN_STREAM_MODE_NONE;
	litSectionDesc.m_huffmanStreamSizes[0] = regeneratedSize;
	litSectionDesc.m_huffmanStreamSizes[1] = 0;
	litSectionDesc.m_huffmanStreamSizes[2] = 0;
	litSectionDesc.m_huffmanStreamSizes[3] = 0;
	litSectionDesc.m_decompressedLiteralsStream = &litStreamObj;
	litSectionDesc.m_numValues = regeneratedSize;

	ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_LITERALS_SECTION, &litSectionDesc));
	ZSTDHL_CHECKED(zstdhl_SliceStreamSource_FlushRemainder(&sliceSource, ZSTDHL_RESULT_LITERALS_SECTION_TRUNCATED));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_ParseRLELiteralsSection(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint32_t regeneratedSize)
{
	zstdhl_SliceStreamSource_t sliceSource;
	zstdhl_StreamSourceObject_t litStreamObj;
	zstdhl_LiteralsSectionDesc_t litSectionDesc;

	sliceSource.m_streamSource = *streamSource;
	sliceSource.m_sizeRemaining = 1;

	litStreamObj.m_readBytesFunc = zstdhl_SliceStreamSource_ReadBytes;
	litStreamObj.m_userdata = &sliceSource;

	litSectionDesc.m_huffmanStreamMode = ZSTDHL_HUFFMAN_STREAM_MODE_NONE;
	litSectionDesc.m_huffmanStreamSizes[0] = 1;
	litSectionDesc.m_huffmanStreamSizes[1] = 0;
	litSectionDesc.m_huffmanStreamSizes[2] = 0;
	litSectionDesc.m_huffmanStreamSizes[3] = 0;
	litSectionDesc.m_decompressedLiteralsStream = &litStreamObj;
	litSectionDesc.m_numValues = 1;

	ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_LITERALS_SECTION, &litSectionDesc));
	ZSTDHL_CHECKED(zstdhl_SliceStreamSource_FlushRemainder(&sliceSource, ZSTDHL_RESULT_LITERALS_SECTION_TRUNCATED));

	return ZSTDHL_RESULT_OK;
}

typedef zstdhl_ResultCode_t (*zstdhl_BufferResizeFunc_t)(void *userData, uint32_t **outBuffer, size_t *outCapacity);
typedef void (*zstdhl_BufferClearFunc_t)(void *userData);

zstdhl_ResultCode_t zstdhl_DecodeFSEDescription(zstdhl_ForwardBitstream_t *bitstream, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint8_t maxAccuracyLog, zstdhl_BufferResizeFunc_t increaseCapacityFunc, void *increaseCapacityUserData, uint32_t **outProbs, size_t *outNumProbs, int *outAccuracyLog)
{
	uint32_t bits = 0;
	size_t maxProbs = 0;
	size_t numProbs = 0;
	size_t numProbsDecoded = 0;
	uint32_t *probs = NULL;
	int accuracyLog = 0;
	zstdhl_FSETableStartDesc_t tableStart;

	ZSTDHL_CHECKED(zstdhl_ForwardBitstream_ReadBits(bitstream, 4, &bits));
	accuracyLog = (int)bits + 5;

	if (accuracyLog > maxAccuracyLog)
		return ZSTDHL_RESULT_ACCURACY_LOG_TOO_LARGE;

	ZSTDHL_DECL(uint32_t) targetTotalProbs = (1 << accuracyLog);

	ZSTDHL_DECL(uint32_t) cumulativeProb = 0;

	tableStart.m_accuracyLog = accuracyLog;
	ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_FSE_TABLE_START, &tableStart));

	do
	{
		uint32_t maxProbValue = targetTotalProbs - cumulativeProb + 1;

		uint8_t minProbBits = zstdhl_Log2_16(maxProbValue);
		uint32_t largeProbRange = maxProbValue - (1 << minProbBits) + 1;
		uint32_t largeProbStart = (1 << minProbBits) - largeProbRange;

		uint32_t probValue = 0;
		zstdhl_ProbabilityDesc_t probDesc;

		if (numProbs == maxProbs)
		{
			ZSTDHL_CHECKED(increaseCapacityFunc(increaseCapacityUserData, &probs, &maxProbs));
		}
			
		ZSTDHL_CHECKED(zstdhl_ForwardBitstream_ReadBits(bitstream, minProbBits, &probValue));

		if (probValue >= largeProbStart)
		{
			uint32_t extraPrecBit = 0;

			ZSTDHL_CHECKED(zstdhl_ForwardBitstream_ReadBits(bitstream, 1, &extraPrecBit));

			if (extraPrecBit)
				probValue += largeProbRange;
		}

		if (probValue == 0)
		{
			probs[numProbs] = ZSTDHL_LESS_THAN_ONE_VALUE;
			numProbs++;

			cumulativeProb++;

			probDesc.m_prob = ZSTDHL_LESS_THAN_ONE_VALUE;
			probDesc.m_repeatCount = 0;

			ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_FSE_PROBABILITY, &probDesc));
		}
		else
		{
			uint32_t prob = probValue - 1;

			if (prob > 0)
			{
				probs[numProbs] = prob;
				numProbs++;

				cumulativeProb += prob;

				probDesc.m_prob = prob;
				probDesc.m_repeatCount = 0;

				ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_FSE_PROBABILITY, &probDesc));
			}
			else
			{
				size_t numZeroProbs = 1;
				size_t availProbs = maxProbs - numProbs;

				for (;;)
				{
					uint32_t repeatBits = 0;

					ZSTDHL_CHECKED(zstdhl_ForwardBitstream_ReadBits(bitstream, 2, &repeatBits));

					numZeroProbs += repeatBits;

					while (numZeroProbs > availProbs)
					{
						ZSTDHL_CHECKED(increaseCapacityFunc(increaseCapacityUserData, &probs, &maxProbs));

						availProbs = maxProbs - numProbs;
					}

					if (repeatBits < 3)
						break;
				}

				probDesc.m_prob = 0;
				probDesc.m_repeatCount = numZeroProbs - 1;

				ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_FSE_PROBABILITY, &probDesc));

				while (numZeroProbs > 0)
				{
					numZeroProbs--;
					probs[numProbs] = 0;
					numProbs++;
				}
			}
		}
	} while (cumulativeProb < targetTotalProbs);

	*outNumProbs = numProbs;
	*outProbs = probs;
	*outAccuracyLog = accuracyLog;

	while (numProbs < maxProbs)
	{
		probs[numProbs] = 0;
		numProbs++;
	}

	ZSTDHL_DECL(uint32_t) wasteBits = 0;

	if (bitstream->m_numBits)
	{
		zstdhl_WasteBitsDesc_t wasteBitsDesc;
		uint32_t bits = 0;

		wasteBitsDesc.m_numBits = bitstream->m_numBits;
		ZSTDHL_CHECKED(zstdhl_ForwardBitstream_ReadBits(bitstream, bitstream->m_numBits, &bits));

		wasteBitsDesc.m_bits = (uint8_t)bits;

		ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_WASTE_BITS, &wasteBitsDesc));
	}

	ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_FSE_TABLE_END, NULL));


	return ZSTDHL_RESULT_OK;
}

typedef struct zstdhl_SimpleProbDecodeState
{
	size_t m_maxProbs;
	uint32_t *m_probs;
	uint8_t m_isFirstRequest;
	zstdhl_ResultCode_t m_failureCode;
} zstdhl_SimpleProbDecodeState_t;

void zstdhl_SimpleProbDecodeState_Init(zstdhl_SimpleProbDecodeState_t *decState, uint32_t *probs, size_t maxProbs, zstdhl_ResultCode_t failureCode)
{
	decState->m_isFirstRequest = 1;
	decState->m_maxProbs = maxProbs;
	decState->m_probs = probs;
	decState->m_failureCode = failureCode;
}

static zstdhl_ResultCode_t zstdhl_SimpleProbRequestMoreCapacity(void *userdata, uint32_t **outProbs, size_t *outNumProbs)
{
	zstdhl_SimpleProbDecodeState_t *decState = (zstdhl_SimpleProbDecodeState_t *)userdata;

	if (decState->m_isFirstRequest)
	{
		decState->m_isFirstRequest = 0;
		*outProbs = decState->m_probs;
		*outNumProbs = decState->m_maxProbs;
		return ZSTDHL_RESULT_OK;
	}

	return decState->m_failureCode;
}

static void zstdhl_SimpleProbClear(void *userdata)
{
}

zstdhl_ResultCode_t zstdhl_DecodeFSEDescriptionSimple(zstdhl_ForwardBitstream_t *bitstream, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint8_t maxAccuracyLog, uint32_t *probs, size_t maxProbs, size_t *outNumProbs, int *outAccuracyLog)
{
	zstdhl_SimpleProbDecodeState_t simpleDecodeState;
	uint32_t *scratchProbs = NULL;

	zstdhl_SimpleProbDecodeState_Init(&simpleDecodeState, probs, maxProbs, ZSTDHL_RESULT_TOO_MANY_PROBS);

	ZSTDHL_CHECKED(zstdhl_DecodeFSEDescription(bitstream, disassemblyOutput, maxAccuracyLog, zstdhl_SimpleProbRequestMoreCapacity, &simpleDecodeState, &scratchProbs, outNumProbs, outAccuracyLog));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_ParseFSEStream(zstdhl_ReverseBitstream_t *bitstream, const zstdhl_FSETable_t *fseTable, uint8_t numStates, uint8_t *outBuffer, uint32_t outCapacity, uint32_t *outNumBytesRead)
{
	uint16_t states[2];
	uint8_t activeState = 0;
	uint32_t numBytesRead = 0;

	if (numStates != 1 && numStates != 2)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	while (activeState < numStates)
	{
		uint32_t state = 0;

		ZSTDHL_CHECKED(zstdhl_ReverseBitstream_ReadBitsComplete(bitstream, fseTable->m_accuracyLog, &state));

		states[activeState++] = (uint16_t)state;
	}

	activeState = 0;

	for (;;)
	{
		uint16_t state = states[activeState];

		const zstdhl_FSETableCell_t *cell = fseTable->m_cells + state;

		size_t sym = cell->m_sym;

		if (numBytesRead == outCapacity)
			return ZSTDHL_RESULT_FSE_OUTPUT_CAPACITY_EXCEEDED;

		outBuffer[numBytesRead++] = (uint8_t)sym;

		ZSTDHL_DECL(uint32_t) refillBits = 0;
		ZSTDHL_DECL(zstdhl_ResultCode_t) moreBitsResult = zstdhl_ReverseBitstream_ReadBitsCompleteSoftFault(bitstream, cell->m_numBits, &refillBits);
		
		if (moreBitsResult == ZSTDHL_RESULT_REVERSE_BITSTREAM_TRUNCATED_SOFT_FAULT)
		{
			uint8_t statesToFlush = numStates - 1;

			while (statesToFlush > 0)
			{
				if (numBytesRead == outCapacity)
					return ZSTDHL_RESULT_FSE_OUTPUT_CAPACITY_EXCEEDED;

				activeState++;
				if (activeState == numStates)
					activeState = 0;

				outBuffer[numBytesRead++] = (uint8_t)fseTable->m_cells[states[activeState]].m_sym;

				statesToFlush--;
			}

			break;
		}
		else if (moreBitsResult == ZSTDHL_RESULT_OK)
		{
			states[activeState] = cell->m_baseline + refillBits;

			activeState++;
			if (activeState == numStates)
				activeState = 0;
		}
		else
			return moreBitsResult;
	}

	*outNumBytesRead = numBytesRead;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DecodeFSEHuffmanTableWeights(zstdhl_ReverseBitstream_t *bitstream, const zstdhl_FSETable_t *fseTable, zstdhl_HuffmanTreePartialWeightDesc_t *treePartialDesc)
{
	uint32_t numSpecifiedWeights = 0;

	ZSTDHL_CHECKED(zstdhl_ParseFSEStream(bitstream, fseTable, 2, treePartialDesc->m_specifiedWeights, 255, &numSpecifiedWeights));

	treePartialDesc->m_numSpecifiedWeights = (uint8_t)numSpecifiedWeights;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_ExpandHuffmanWeightTable(const zstdhl_HuffmanTreePartialWeightDesc_t *partialDesc, zstdhl_HuffmanTreeWeightDesc_t *fullDesc)
{
	uint32_t weightIterator = 0;
	uint16_t i = 0;
	int has1Weight = 0;

	uint8_t numSpecifiedWeights = partialDesc->m_numSpecifiedWeights;

	for (i = 0; i < numSpecifiedWeights; i++)
	{
		uint8_t weight = partialDesc->m_specifiedWeights[i];

		if (weight > ZSTDHL_MAX_HUFFMAN_CODE_LENGTH)
			return ZSTDHL_RESULT_HUFFMAN_CODE_TOO_LONG;

		if (weight == 0)
			continue;

		if (weight == 1)
			has1Weight = 1;

		weightIterator += (1u << (weight - 1));
	}

	if (!has1Weight)
		return ZSTDHL_RESULT_HUFFMAN_TABLE_MISSING_1_WEIGHT;

	if (weightIterator == 0)
		return ZSTDHL_RESULT_HUFFMAN_TABLE_EMPTY;
	else
	{
		int nextExp = zstdhl_Log2_32(weightIterator) + 1;
		uint32_t nextPowerOf2 = (1u << nextExp);
		uint32_t delta = nextPowerOf2 - weightIterator;

		if (!zstdhl_IsPowerOf2(delta))
			return ZSTDHL_RESULT_HUFFMAN_TABLE_IMPLICIT_WEIGHT_UNRESOLVABLE;

		fullDesc->m_weights[numSpecifiedWeights] = (uint8_t)(zstdhl_Log2_32(delta) + 1);
	}

	for (i = 0; i < numSpecifiedWeights; i++)
		fullDesc->m_weights[i] = partialDesc->m_specifiedWeights[i];

	for (i = numSpecifiedWeights + 1; i < 256; i++)
		fullDesc->m_weights[i] = 0;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_ParseFSEHuffmanWeights(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_HuffmanTreeDesc_t *huffTreeDesc, zstdhl_Buffers_t *buffers, uint8_t weightsCompressedSize)
{
	int accuracyLog = 0;
	size_t numHuffmanProbs = 0;

	ZSTDHL_DECL(zstdhl_SliceStreamSource_t) sliceSource;
	sliceSource.m_sizeRemaining = weightsCompressedSize;
	sliceSource.m_streamSource.m_readBytesFunc = streamSource->m_readBytesFunc;
	sliceSource.m_streamSource.m_userdata = streamSource->m_userdata;

	ZSTDHL_DECL(zstdhl_StreamSourceObject_t) sliceSourceObj;
	sliceSourceObj.m_userdata = &sliceSource;
	sliceSourceObj.m_readBytesFunc = zstdhl_SliceStreamSource_ReadBytes;

	ZSTDHL_DECL(zstdhl_ForwardBitstream_t) bitstream;

	ZSTDHL_CHECKED(zstdhl_ForwardBitstream_Init(&bitstream, &sliceSourceObj));

	ZSTDHL_CHECKED(zstdhl_DecodeFSEDescriptionSimple(&bitstream, disassemblyOutput, 6, huffTreeDesc->m_weightTableProbabilities, 256, &numHuffmanProbs, &accuracyLog));

	huffTreeDesc->m_huffmanWeightFormat = ZSTDHL_HUFFMAN_WEIGHT_ENCODING_FSE;
	huffTreeDesc->m_weightTable.m_accuracyLog = accuracyLog;
	huffTreeDesc->m_weightTable.m_probabilities = huffTreeDesc->m_weightTableProbabilities;
	huffTreeDesc->m_weightTable.m_numProbabilities = numHuffmanProbs;

	ZSTDHL_DECL(size_t) weightFSETableSize = (size_t)(1 << accuracyLog);

	ZSTDHL_DECL(void *) fseCellsBufferPtr = NULL;
	ZSTDHL_CHECKED(zstdhl_Buffers_Alloc(buffers, ZSTDHL_BUFFER_HUFFMAN_WEIGHT_FSE_TABLE, sizeof(zstdhl_FSETableCell_t) * weightFSETableSize, &fseCellsBufferPtr));

	ZSTDHL_DECL(zstdhl_FSETableCell_t *) huffFSECells = (zstdhl_FSETableCell_t *)fseCellsBufferPtr;

	ZSTDHL_DECL(zstdhl_FSETable_t) huffWeightTable;
	huffWeightTable.m_cells = huffFSECells;
	huffWeightTable.m_numCells = (uint32_t)weightFSETableSize;

	{
		zstdhl_FSESymbolTemp_t symTemps[256];
		ZSTDHL_CHECKED(zstdhl_BuildFSEDistributionTable_ZStd(&huffWeightTable, &huffTreeDesc->m_weightTable, symTemps));
	}

	{
		void *weightBytesPtr = NULL;
		const uint8_t *weightBytes = NULL;
		uint32_t numWeightBytes = (uint32_t)sliceSource.m_sizeRemaining;

		if (numWeightBytes == 0)
			return ZSTDHL_RESULT_REVERSE_BITSTREAM_EMPTY;

		ZSTDHL_CHECKED(zstdhl_Buffers_Alloc(buffers, ZSTDHL_BUFFER_FSE_BITSTREAM, numWeightBytes, &weightBytesPtr));
		weightBytes = (const uint8_t *)weightBytesPtr;

		ZSTDHL_CHECKED(zstdhl_ReadChecked(&sliceSourceObj, weightBytesPtr, numWeightBytes, ZSTDHL_RESULT_REVERSE_BITSTREAM_TOO_SMALL));

		ZSTDHL_DECL(zstdhl_ReverseBitstream_t) weightBitstream;
		ZSTDHL_CHECKED(zstdhl_ReverseBitstream_Init(&weightBitstream, weightBytes, numWeightBytes));

		ZSTDHL_CHECKED(zstdhl_DecodeFSEHuffmanTableWeights(&weightBitstream, &huffWeightTable, &huffTreeDesc->m_partialWeightDesc));
	}

	zstdhl_Buffers_Dealloc(buffers, ZSTDHL_BUFFER_HUFFMAN_WEIGHT_FSE_TABLE);
	zstdhl_Buffers_Dealloc(buffers, ZSTDHL_BUFFER_FSE_BITSTREAM);

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_ParseDirectHuffmanWeights(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_HuffmanTreeDesc_t *treeDesc, uint8_t *outWasteBits, uint8_t numSpecifiedWeights)
{
	uint32_t i = 0;
	uint16_t weightBufSize = (numSpecifiedWeights + 1) / 2;
	uint8_t weightBytes[128];
	
	treeDesc->m_huffmanWeightFormat = ZSTDHL_HUFFMAN_WEIGHT_ENCODING_UNCOMPRESSED;
	treeDesc->m_partialWeightDesc.m_numSpecifiedWeights = numSpecifiedWeights;

	*outWasteBits = 0;

	ZSTDHL_CHECKED(zstdhl_ReadChecked(streamSource, weightBytes, weightBufSize, ZSTDHL_RESULT_INPUT_FAILED));

	for (i = 0; i < numSpecifiedWeights; i++)
	{
		if (i & 1)
			treeDesc->m_partialWeightDesc.m_specifiedWeights[i] = (weightBytes[i / 2] & 0xf);
		else
			treeDesc->m_partialWeightDesc.m_specifiedWeights[i] = ((weightBytes[i / 2] >> 4) & 0xf);
	}

	if (numSpecifiedWeights & 1)
		*outWasteBits = (weightBytes[weightBufSize - 1] & 0xf);

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_ParseHuffmanTreeDescription(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers, zstdhl_HuffmanTreeDesc_t *treeDesc)
{
	uint8_t headerByte = 0;
	uint8_t directWasteBits = 0;
	uint8_t haveWasteBits = 0;

	ZSTDHL_CHECKED(zstdhl_ReadChecked(streamSource, &headerByte, 1, ZSTDHL_RESULT_HUFFMAN_TREE_DESC_TRUNCATED));
	
	ZSTDHL_DECL(uint8_t) maxNumberOfBits = 0;

	if (headerByte < 128)
	{
		ZSTDHL_CHECKED(zstdhl_ParseFSEHuffmanWeights(streamSource, disassemblyOutput, treeDesc, buffers, headerByte));
	}
	else
	{
		ZSTDHL_CHECKED(zstdhl_ParseDirectHuffmanWeights(streamSource, disassemblyOutput, treeDesc, &directWasteBits, headerByte - 127));
		haveWasteBits = ((headerByte - 127) & 1);
	}

	ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_HUFFMAN_TREE, treeDesc));

	if (haveWasteBits)
	{
		zstdhl_WasteBitsDesc_t wasteBitsDesc;
		wasteBitsDesc.m_bits = directWasteBits;
		wasteBitsDesc.m_numBits = 4;

		ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_WASTE_BITS, &wasteBitsDesc));
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_GenerateHuffmanDecodeTable(const zstdhl_HuffmanTreePartialWeightDesc_t *partialWeightDesc, zstdhl_HuffmanTableDec_t *decTable)
{
	uint32_t weightIterator = 0;
	uint32_t i = 0;
	uint8_t maxBits = 0;
	uint8_t upshiftBits = 0;
	zstdhl_HuffmanTreeWeightDesc_t weightDesc;

	ZSTDHL_CHECKED(zstdhl_ExpandHuffmanWeightTable(partialWeightDesc, &weightDesc));

	for (i = 0; i < 256; i++)
	{
		uint8_t weight = weightDesc.m_weights[i];

		if (weight > 0)
			weightIterator += (1u << (weight - 1));
	}

	maxBits = zstdhl_Log2_32(weightIterator);

	weightIterator = 0;

	if (maxBits > ZSTDHL_MAX_HUFFMAN_CODE_LENGTH)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	for (i = 0; i < 2048; i++)
	{
		decTable->m_dec[i].m_numBits = 0;
		decTable->m_dec[i].m_symbol = 0;
	}

	decTable->m_maxBits = maxBits;

	weightIterator = 0;
	for (i = 0; i <= ZSTDHL_MAX_HUFFMAN_CODE_LENGTH; i++)
	{
		uint8_t expectedWeight = i + 1;
		uint32_t stepping = (1u << i);
		uint32_t sym = 0;

		for (sym = 0; sym < 256; sym++)
		{
			if (weightDesc.m_weights[sym] == expectedWeight)
			{
				uint32_t j = 0;
				for (j = 0; j < stepping; j++)
				{
					zstdhl_HuffmanTableDecEntry_t *decEntry = decTable->m_dec + weightIterator;
					weightIterator++;

					decEntry->m_numBits = maxBits - i;
					decEntry->m_symbol = (uint8_t)sym;
				}
			}
		}
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_GenerateHuffmanEncodeTable(const zstdhl_HuffmanTreePartialWeightDesc_t *partialWeightDesc, zstdhl_HuffmanTableEnc_t *encTable)
{
	uint32_t weightIterator = 0;
	uint32_t i = 0;
	uint8_t maxBits = 0;
	uint8_t upshiftBits = 0;
	zstdhl_HuffmanTreeWeightDesc_t weightDesc;

	ZSTDHL_CHECKED(zstdhl_ExpandHuffmanWeightTable(partialWeightDesc, &weightDesc));

	for (i = 0; i < 256; i++)
	{
		uint8_t weight = weightDesc.m_weights[i];

		if (weight > 0)
			weightIterator += (1u << (weight - 1));
	}

	maxBits = zstdhl_Log2_32(weightIterator);

	weightIterator = 0;

	if (maxBits > ZSTDHL_MAX_HUFFMAN_CODE_LENGTH)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	for (i = 0; i < 256; i++)
	{
		encTable->m_entries[i].m_numBits = 0;
		encTable->m_entries[i].m_bits = 0;
	}

	weightIterator = 0;
	for (i = 0; i <= ZSTDHL_MAX_HUFFMAN_CODE_LENGTH; i++)
	{
		uint8_t expectedWeight = i + 1;
		uint32_t stepping = (1u << i);
		uint32_t sym = 0;

		for (sym = 0; sym < 256; sym++)
		{
			if (weightDesc.m_weights[sym] == expectedWeight)
			{
				uint8_t numBits = maxBits - i;

				if (((weightIterator >> i) << i) != weightIterator)
					return ZSTDHL_RESULT_INTERNAL_ERROR;

				encTable->m_entries[sym].m_bits = (weightIterator >> i);
				encTable->m_entries[sym].m_numBits = maxBits - i;

				weightIterator += stepping;
			}
		}
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DecodeHuffmanStream1(const uint8_t *huffmanBytes, uint8_t *decodedBytes, uint32_t streamSize, uint32_t decompressedSize, const zstdhl_HuffmanTableDec_t *decTable)
{
	zstdhl_ReverseBitstream_t revStream;

	ZSTDHL_CHECKED(zstdhl_ReverseBitstream_Init(&revStream, huffmanBytes, streamSize));

	while (decompressedSize > 0)
	{
		uint32_t bits = 0;

		ZSTDHL_CHECKED(zstdhl_ReverseBitstream_PeekBits(&revStream, decTable->m_maxBits, &bits, ZSTDHL_RESULT_OK));
		ZSTDHL_CHECKED(zstdhl_ReverseBitstream_ConsumeBits(&revStream, decTable->m_dec[bits].m_numBits));

		*decodedBytes = decTable->m_dec[bits].m_symbol;

		decodedBytes++;
		decompressedSize--;
	}

	if (revStream.m_numBits > 0 || revStream.m_bytesAvailable > 0)
		return ZSTDHL_RESULT_HUFFMAN_STREAM_INCOMPLETELY_CONSUMED;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DecodeHuffmanStream4(const uint8_t *huffmanBytes, uint8_t *decodedBytes, const uint32_t *streamSizes, uint32_t decompressedSize, const zstdhl_HuffmanTableDec_t *decTable)
{
	uint32_t firstStreamsDecodedSize = (decompressedSize + 3u) / 4u;
	uint32_t lastStreamDecodedSize = 0;
	uint32_t i = 0;

	// TODO: Check this and specify it if it's true
	if (decompressedSize < 3)
		return ZSTDHL_RESULT_HUFFMAN_4_STREAM_REGENERATED_SIZE_TOO_SMALL;

	lastStreamDecodedSize = decompressedSize - (firstStreamsDecodedSize * 3u);

	for (i = 0; i < 3; i++)
	{
		ZSTDHL_CHECKED(zstdhl_DecodeHuffmanStream1(huffmanBytes, decodedBytes, streamSizes[i], firstStreamsDecodedSize, decTable));
		huffmanBytes += streamSizes[i];
		decodedBytes += firstStreamsDecodedSize;
	}

	ZSTDHL_CHECKED(zstdhl_DecodeHuffmanStream1(huffmanBytes, decodedBytes, streamSizes[3], lastStreamDecodedSize, decTable));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DecodeHuffmanLiterals(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers, uint32_t streamSize, uint32_t regeneratedSize, int is4Stream, const zstdhl_HuffmanTableDec_t *decTable)
{
	void *literalsPtr = NULL;
	void *huffmanDataPtr = NULL;
	const uint8_t *huffmanBytes = NULL;
	const uint8_t *literalsEnd = NULL;
	zstdhl_LiteralsSectionDesc_t litSectionDesc;
	size_t combinedSize = 0;
	zstdhl_MemBufferStreamSource_t litStream;
	zstdhl_StreamSourceObject_t litStreamObj;

	ZSTDHL_CHECKED(zstdhl_Buffers_Alloc(buffers, ZSTDHL_BUFFER_LITERALS, regeneratedSize, &literalsPtr));
	ZSTDHL_CHECKED(zstdhl_Buffers_Alloc(buffers, ZSTDHL_BUFFER_HUFFMAN_BITSTREAM, streamSize, &huffmanDataPtr));

	ZSTDHL_CHECKED(zstdhl_ReadChecked(streamSource, huffmanDataPtr, streamSize, ZSTDHL_RESULT_HUFFMAN_BITSTREAM_TOO_SMALL));

	huffmanBytes = (const uint8_t *)huffmanDataPtr;

	literalsEnd = ((const uint8_t *)literalsPtr) + regeneratedSize;

	if (is4Stream)
	{
		uint32_t substreamSizes[4];
		uint32_t substreamTotal = 0;

		if (streamSize < 6)
			return ZSTDHL_RESULT_JUMP_TABLE_TRUNCATED;

		// Parse jump table
		substreamSizes[0] = huffmanBytes[0] + (huffmanBytes[1] << 8);
		substreamSizes[1] = huffmanBytes[2] + (huffmanBytes[3] << 8);
		substreamSizes[2] = huffmanBytes[4] + (huffmanBytes[5] << 8);

		huffmanBytes += 6;

		substreamTotal = substreamSizes[0] + substreamSizes[1] + substreamSizes[2];

		if (substreamTotal > streamSize)
			return ZSTDHL_RESULT_JUMP_TABLE_INVALID;

		substreamSizes[3] = streamSize - 6 - substreamTotal;

		ZSTDHL_CHECKED(zstdhl_DecodeHuffmanStream4(huffmanBytes, (uint8_t *)literalsPtr, substreamSizes, regeneratedSize, decTable));

		litSectionDesc.m_huffmanStreamSizes[0] = substreamSizes[0];
		litSectionDesc.m_huffmanStreamSizes[1] = substreamSizes[1];
		litSectionDesc.m_huffmanStreamSizes[2] = substreamSizes[2];
		litSectionDesc.m_huffmanStreamSizes[3] = substreamSizes[3];

		litSectionDesc.m_huffmanStreamMode = ZSTDHL_HUFFMAN_STREAM_MODE_4_STREAMS;
	}
	else
	{
		ZSTDHL_CHECKED(zstdhl_DecodeHuffmanStream1(huffmanBytes, (uint8_t *)literalsPtr, streamSize, regeneratedSize, decTable));

		litSectionDesc.m_huffmanStreamSizes[0] = streamSize;
		litSectionDesc.m_huffmanStreamSizes[1] = 0;
		litSectionDesc.m_huffmanStreamSizes[2] = 0;
		litSectionDesc.m_huffmanStreamSizes[3] = 0;

		litSectionDesc.m_huffmanStreamMode = ZSTDHL_HUFFMAN_STREAM_MODE_1_STREAM;
	}

	zstdhl_MemBufferStreamSource_Init(&litStream, buffers->m_buffers[ZSTDHL_BUFFER_LITERALS], regeneratedSize);

	litStreamObj.m_readBytesFunc = zstdhl_MemBufferStreamSource_ReadBytes;
	litStreamObj.m_userdata = &litStream;

	litSectionDesc.m_decompressedLiteralsStream = &litStreamObj;
	litSectionDesc.m_numValues = regeneratedSize;

	ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_LITERALS_SECTION, &litSectionDesc));

	zstdhl_Buffers_Dealloc(buffers, ZSTDHL_BUFFER_HUFFMAN_BITSTREAM);
	zstdhl_Buffers_Dealloc(buffers, ZSTDHL_BUFFER_LITERALS);

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_ParseHuffmanLiteralsSection(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers, uint32_t compressedSize, uint32_t regeneratedSize, int haveNewTree, int is4Stream, zstdhl_FramePersistentState_t *pstate)
{
	zstdhl_SliceStreamSource_t sliceSource;
	zstdhl_StreamSourceObject_t sliceSourceObj;

	sliceSource.m_streamSource.m_readBytesFunc = streamSource->m_readBytesFunc;
	sliceSource.m_streamSource.m_userdata = streamSource->m_userdata;
	sliceSource.m_sizeRemaining = compressedSize;

	sliceSourceObj.m_readBytesFunc = zstdhl_SliceStreamSource_ReadBytes;
	sliceSourceObj.m_userdata = &sliceSource;

	if (haveNewTree)
	{
		zstdhl_HuffmanTreeDesc_t treeDesc;
		ZSTDHL_CHECKED(zstdhl_ParseHuffmanTreeDescription(&sliceSourceObj, disassemblyOutput, buffers, &treeDesc));
		ZSTDHL_CHECKED(zstdhl_GenerateHuffmanDecodeTable(&treeDesc.m_partialWeightDesc, &pstate->m_huffmanTable));
		pstate->m_haveHuffmanTable = 1;
	}

	if (!pstate->m_haveHuffmanTable)
		return ZSTDHL_RESULT_HUFFMAN_TABLE_NOT_SET;

	return zstdhl_DecodeHuffmanLiterals(&sliceSourceObj, disassemblyOutput, buffers, (uint32_t)sliceSource.m_sizeRemaining, regeneratedSize, is4Stream, &pstate->m_huffmanTable);
}

zstdhl_ResultCode_t zstdhl_ParseLiteralsSection(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers, uint32_t *inOutBlockSize, zstdhl_FramePersistentState_t *pstate)
{
	zstdhl_SliceStreamSource_t sliceSource;
	zstdhl_StreamSourceObject_t sliceSourceObj;
	uint8_t moreHeaderBytes[4];
	zstdhl_LiteralsSectionHeader_t litHeader;

	ZSTDHL_DECL(uint32_t) blockSize = *inOutBlockSize;

	sliceSource.m_streamSource.m_readBytesFunc = streamSource->m_readBytesFunc;
	sliceSource.m_streamSource.m_userdata = streamSource->m_userdata;
	sliceSource.m_sizeRemaining = blockSize;

	sliceSourceObj.m_readBytesFunc = zstdhl_SliceStreamSource_ReadBytes;
	sliceSourceObj.m_userdata = &sliceSource;

	ZSTDHL_DECL(uint8_t) headerByte = 0;

	ZSTDHL_CHECKED(zstdhl_ReadChecked(&sliceSourceObj, &headerByte, 1, ZSTDHL_RESULT_LITERALS_SECTION_HEADER_TRUNCATED));

	ZSTDHL_DECL(zstdhl_LiteralsSectionType_t) litSectionType = (zstdhl_LiteralsSectionType_t)(headerByte & 3);

	ZSTDHL_DECL(uint8_t) sizeFormat = ((headerByte >> 2) & 3);

	ZSTDHL_DECL(uint32_t) regeneratedSize = 0;
	ZSTDHL_DECL(uint32_t) compressedSize = 0;
	ZSTDHL_DECL(uint8_t) is4Stream = 0;

	switch (litSectionType)
	{
	case ZSTDHL_LITERALS_SECTION_TYPE_RAW:
	case ZSTDHL_LITERALS_SECTION_TYPE_RLE:
		if (sizeFormat == 0 || sizeFormat == 2)
			regeneratedSize = (headerByte >> 3);
		else if (sizeFormat == 1)
		{
			ZSTDHL_CHECKED(zstdhl_ReadChecked(&sliceSourceObj, moreHeaderBytes, 1, ZSTDHL_RESULT_LITERALS_SECTION_HEADER_TRUNCATED));
			regeneratedSize = (headerByte >> 4) + (moreHeaderBytes[0] << 4);
		}
		else if (sizeFormat == 3)
		{
			ZSTDHL_CHECKED(zstdhl_ReadChecked(&sliceSourceObj, moreHeaderBytes, 2, ZSTDHL_RESULT_LITERALS_SECTION_HEADER_TRUNCATED));
			regeneratedSize = (headerByte >> 4) + (moreHeaderBytes[0] << 4) + (moreHeaderBytes[1] << 12);
		}
		compressedSize = regeneratedSize;
		break;
	case ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN:
	case ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN_REUSE:
		{
			ZSTDHL_DECL(uint8_t) extraSizeBytes = sizeFormat + 1;

			if (sizeFormat == 0)
			{
				is4Stream = 0;
				extraSizeBytes = 2;
			}
			else
				is4Stream = 1;

			ZSTDHL_CHECKED(zstdhl_ReadChecked(&sliceSourceObj, moreHeaderBytes, extraSizeBytes, ZSTDHL_RESULT_LITERALS_SECTION_HEADER_TRUNCATED));

			ZSTDHL_DECL(uint32_t) sizeBits = 0;
			ZSTDHL_DECL(int) i = 0;
			for (i = 0; i < extraSizeBytes; i++)
				sizeBits |= (((uint32_t)moreHeaderBytes[i]) << (i * 8));

			ZSTDHL_DECL(int) sizePrecision = (extraSizeBytes * 4) + 2;
			ZSTDHL_DECL(uint32_t) sizeMask = (1 << sizePrecision) - 1;

			regeneratedSize = (((headerByte >> 4) + (sizeBits << 4)) & sizeMask);
			compressedSize = (sizeBits >> (sizePrecision - 4)) & sizeMask;
		}
		break;
	default:
		return ZSTDHL_RESULT_INTERNAL_ERROR;
	}

	litHeader.m_sectionType = litSectionType;
	litHeader.m_compressedSize = compressedSize;
	litHeader.m_regeneratedSize = regeneratedSize;

	ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_LITERALS_SECTION_HEADER, &litHeader));

	switch (litSectionType)
	{
	case ZSTDHL_LITERALS_SECTION_TYPE_RAW:
		ZSTDHL_CHECKED(zstdhl_ParseRawLiteralsSection(&sliceSourceObj, disassemblyOutput, regeneratedSize));
		break;
	case ZSTDHL_LITERALS_SECTION_TYPE_RLE:
		ZSTDHL_CHECKED(zstdhl_ParseRLELiteralsSection(&sliceSourceObj, disassemblyOutput, regeneratedSize));
		break;
	case ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN:
	case ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN_REUSE:
		ZSTDHL_CHECKED(zstdhl_ParseHuffmanLiteralsSection(&sliceSourceObj, disassemblyOutput, buffers, compressedSize, regeneratedSize, (litSectionType == ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN) ? 1 : 0, is4Stream, pstate));
		break;
	default:
		return ZSTDHL_RESULT_INTERNAL_ERROR;
	}

	*inOutBlockSize = (uint32_t)sliceSource.m_sizeRemaining;
	return ZSTDHL_RESULT_OK;
}

static uint32_t zstdhl_LitLenDefaultProbs[] =
{
	4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1,
	ZSTDHL_LESS_THAN_ONE_VALUE, ZSTDHL_LESS_THAN_ONE_VALUE, ZSTDHL_LESS_THAN_ONE_VALUE, ZSTDHL_LESS_THAN_ONE_VALUE
};

static zstdhl_SubstreamCompressionStructureDef_t zstdhl_litLenSDef =
{
	9,
	6,
	36,
	zstdhl_LitLenDefaultProbs
};

static uint32_t zstdhl_MatchLenDefaultProbs[] =
{
	1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, ZSTDHL_LESS_THAN_ONE_VALUE, ZSTDHL_LESS_THAN_ONE_VALUE,
	ZSTDHL_LESS_THAN_ONE_VALUE, ZSTDHL_LESS_THAN_ONE_VALUE, ZSTDHL_LESS_THAN_ONE_VALUE, ZSTDHL_LESS_THAN_ONE_VALUE, ZSTDHL_LESS_THAN_ONE_VALUE
};

static zstdhl_SubstreamCompressionStructureDef_t zstdhl_matchLenSDef =
{
	9,
	6,
	53,
	zstdhl_MatchLenDefaultProbs
};

static uint32_t zstdhl_OffsetCodeProbs[] =
{
	1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, ZSTDHL_LESS_THAN_ONE_VALUE,
	ZSTDHL_LESS_THAN_ONE_VALUE, ZSTDHL_LESS_THAN_ONE_VALUE, ZSTDHL_LESS_THAN_ONE_VALUE, ZSTDHL_LESS_THAN_ONE_VALUE
};

static zstdhl_SubstreamCompressionStructureDef_t zstdhl_offsetCodeSDef =
{
	8,
	5,
	29,
	zstdhl_OffsetCodeProbs
};

zstdhl_ResultCode_t zstdhl_ParseCompressionDef(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint8_t defByte, int defBitOffset, const zstdhl_SubstreamCompressionStructureDef_t *sdef, zstdhl_SequencesSubstreamCompressionDef_t *cdef, zstdhl_BufferResizeFunc_t resizeFunc, zstdhl_BufferClearFunc_t clearFunc, void *resizeUserData)
{
	zstdhl_SequencesCompressionMode_t compressionMode = (zstdhl_SequencesCompressionMode_t)((defByte >> defBitOffset) & 3);
	uint32_t i = 0;

	switch (compressionMode)
	{
	case ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED:
		{
			clearFunc(resizeUserData);

			cdef->m_isDefined = 1;
			cdef->m_fseTableDef.m_accuracyLog = sdef->m_defaultAccuracyLog;
			cdef->m_fseTableDef.m_numProbabilities = sdef->m_numProbs;
			cdef->m_fseTableDef.m_probabilities = sdef->m_defaultProbs;
		}
		return ZSTDHL_RESULT_OK;

	case ZSTDHL_SEQ_COMPRESSION_MODE_RLE:
		{
			uint8_t rleByte = 0;
			uint32_t *probs = NULL;
			size_t i = 0;
			size_t maxProbs = 0;

			clearFunc(resizeUserData);

			ZSTDHL_CHECKED(zstdhl_ReadChecked(streamSource, &rleByte, 1, ZSTDHL_RESULT_SEQUENCE_COMPRESSION_DEF_TRUNCATED));

			while (rleByte >= maxProbs)
			{
				ZSTDHL_CHECKED(resizeFunc(resizeUserData, &probs, &maxProbs));
			}

			cdef->m_isDefined = 1;
			cdef->m_fseTableDef.m_accuracyLog = 0;
			cdef->m_fseTableDef.m_numProbabilities = rleByte + 1u;
			cdef->m_fseTableDef.m_probabilities = probs;

			for (i = 0; i < maxProbs; i++)
				probs[i] = 0;

			probs[rleByte] = 1;

			ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_SEQUENCE_RLE_BYTE, &rleByte));
		}
		return ZSTDHL_RESULT_OK;

	case ZSTDHL_SEQ_COMPRESSION_MODE_FSE:
		{
			int accuracyLog = 0;
			uint32_t *probs = NULL;
			size_t numProbs = 0;
			zstdhl_ForwardBitstream_t bitstream;

			clearFunc(resizeUserData);

			zstdhl_ForwardBitstream_Init(&bitstream, streamSource);

			ZSTDHL_CHECKED(zstdhl_DecodeFSEDescription(&bitstream, disassemblyOutput, sdef->m_maxAccuracyLog, resizeFunc, resizeUserData, &probs, &numProbs, &accuracyLog));

			cdef->m_isDefined = 1;
			cdef->m_fseTableDef.m_accuracyLog = accuracyLog;
			cdef->m_fseTableDef.m_numProbabilities = numProbs;
			cdef->m_fseTableDef.m_probabilities = probs;
		}
		return ZSTDHL_RESULT_OK;

	case ZSTDHL_SEQ_COMPRESSION_MODE_REUSE:
		if (!cdef->m_isDefined)
			return ZSTDHL_RESULT_SEQUENCE_COMPRESSION_MODE_REUSE_WITHOUT_PRIOR_BLOCK;

		return ZSTDHL_RESULT_OK;
	default:
		return ZSTDHL_RESULT_INTERNAL_ERROR;
	}

	return ZSTDHL_RESULT_INTERNAL_ERROR;
}

zstdhl_ResultCode_t zstdhl_InitSequenceDecoding(zstdhl_ReverseBitstream_t *bitstream, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers, const zstdhl_FSETableDef_t *tableDef, int bufferID, zstdhl_FSETable_t *table, uint32_t *outInitialState)
{
	void *cellPtr = NULL;
	zstdhl_FSESymbolTemp_t *symbolTemps = NULL;
	void *symbolTempsBufferPtr = NULL;

	if (SIZE_MAX / sizeof(zstdhl_FSESymbolTemp_t) < tableDef->m_numProbabilities)
		return ZSTDHL_RESULT_OUT_OF_MEMORY;

	ZSTDHL_CHECKED(zstdhl_Buffers_Alloc(buffers, ZSTDHL_BUFFER_SEQ_TEMPS, sizeof(zstdhl_FSESymbolTemp_t) * tableDef->m_numProbabilities, &symbolTempsBufferPtr));
	symbolTemps = (zstdhl_FSESymbolTemp_t *)symbolTempsBufferPtr;

	zstdhl_Buffers_Dealloc(buffers, bufferID);
	ZSTDHL_CHECKED(zstdhl_Buffers_Alloc(buffers, bufferID, sizeof(zstdhl_FSETableCell_t) << tableDef->m_accuracyLog, &cellPtr));

	table->m_cells = (zstdhl_FSETableCell_t *)cellPtr;

	ZSTDHL_CHECKED(zstdhl_BuildFSEDistributionTable_ZStd(table, tableDef, symbolTemps));

	ZSTDHL_CHECKED(zstdhl_ReverseBitstream_ReadBitsComplete(bitstream, table->m_accuracyLog, outInitialState));

	zstdhl_Buffers_Dealloc(buffers, ZSTDHL_BUFFER_SEQ_TEMPS);

	return ZSTDHL_RESULT_OK;
}

// Match length data for codes 32..42
uint32_t zstdhl_MatchLengthBaselines[] = 
{
	35, 37, 39, 41, 43, 47, 51, 59, 67, 83, 99
};

uint8_t zstdhl_MatchLengthBits[] =
{
	1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5
};

// Lit length data for 16..24
uint32_t zstdhl_LitLengthBaselines[] =
{
	16, 18, 20, 22, 24, 28, 32, 40, 48
};

uint8_t zstdhl_LitLengthBits[] =
{
	1, 1, 1, 1, 2, 2, 3, 3, 4
};

zstdhl_ResultCode_t zstdhl_DecodeSequences(zstdhl_ReverseBitstream_t *bitstream, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers, const zstdhl_FSETableDef_t *litLengthTableDef, const zstdhl_FSETableDef_t *offsetTableDef, const zstdhl_FSETableDef_t *matchLengthTableDef, uint32_t numSequences)
{
	uint32_t litLengthState = 0;
	uint32_t offsetState = 0;
	uint32_t matchLengthState = 0;
	zstdhl_FSETable_t litLengthTable;
	zstdhl_FSETable_t matchLengthTable;
	zstdhl_FSETable_t offsetTable;
	uint32_t *offsetBigNum = (uint32_t *)buffers->m_buffers[ZSTDHL_BUFFER_OFFSET_BIGNUM];

	ZSTDHL_CHECKED(zstdhl_InitSequenceDecoding(bitstream, disassemblyOutput, buffers, litLengthTableDef, ZSTDHL_BUFFER_LIT_LENGTH_FSE_TABLE, &litLengthTable, &litLengthState));
	ZSTDHL_CHECKED(zstdhl_InitSequenceDecoding(bitstream, disassemblyOutput, buffers, offsetTableDef, ZSTDHL_BUFFER_OFFSET_FSE_TABLE, &offsetTable, &offsetState));
	ZSTDHL_CHECKED(zstdhl_InitSequenceDecoding(bitstream, disassemblyOutput, buffers, matchLengthTableDef, ZSTDHL_BUFFER_MATCH_LENGTH_FSE_TABLE, &matchLengthTable, &matchLengthState));

	while (numSequences > 0)
	{
		const zstdhl_FSETableCell_t *litLengthCell = litLengthTable.m_cells + litLengthState;
		const zstdhl_FSETableCell_t *matchLengthCell = matchLengthTable.m_cells + matchLengthState;
		const zstdhl_FSETableCell_t *offsetTableCell = offsetTable.m_cells + offsetState;
		size_t litLengthSym = litLengthCell->m_sym;
		size_t matchLengthSym = matchLengthCell->m_sym;
		size_t offsetSym = offsetTableCell->m_sym;
		uint32_t litLengthBaseline = 0;
		uint8_t litLengthNumBits = 0;
		uint32_t matchLengthBaseline = 0;
		uint8_t matchLengthNumBits = 0;
		uint32_t litLength = 0;
		uint32_t matchLength = 0;
		uint32_t stateOffset = 0;
		zstdhl_SequenceDesc_t seq;

		if (litLengthSym < 16)
			litLengthBaseline = (uint32_t)litLengthSym;
		else if (litLengthSym < 25)
		{
			litLengthBaseline = zstdhl_LitLengthBaselines[litLengthSym - 16];
			litLengthNumBits = zstdhl_LitLengthBits[litLengthSym - 16];
		}
		else
		{
			litLengthBaseline = 1 << (litLengthSym - 19);
			litLengthNumBits = (uint8_t)(litLengthSym - 19);
		}

		if (matchLengthSym < 32)
			matchLengthBaseline = (uint32_t)matchLengthSym + 3;
		else if (matchLengthSym < 43)
		{
			matchLengthBaseline = zstdhl_MatchLengthBaselines[matchLengthSym - 32];
			matchLengthNumBits = zstdhl_MatchLengthBits[matchLengthSym - 32];
		}
		else
		{
			matchLengthBaseline = (1 << (matchLengthSym - 36)) + 3;
			matchLengthNumBits = (uint8_t)(matchLengthSym - 36);
		}

		{
			size_t numOffsetDWords = (offsetSym / 32u) + 1u;
			size_t bitsRemaining = offsetSym;
			size_t i = 0;
			uint32_t bits = 0;

			for (i = 0; i < numOffsetDWords; i++)
				offsetBigNum[i] = 0;

			while (bitsRemaining > 0)
			{
				size_t bitsToRead = 16;
				if (bitsRemaining % 16u != 0)
					bitsToRead = (bitsRemaining % 16u);

				ZSTDHL_CHECKED(zstdhl_ReverseBitstream_ReadBitsComplete(bitstream, (uint8_t)bitsToRead, &bits));
				bitsRemaining -= bitsToRead;

				offsetBigNum[bitsRemaining / 32u] |= bits << (bitsRemaining % 32u);
			}

			offsetBigNum[offsetSym / 32u] |= 1 << (offsetSym % 32u);
		}

		ZSTDHL_CHECKED(zstdhl_ReverseBitstream_ReadBitsComplete(bitstream, (uint8_t)matchLengthNumBits, &matchLength));
		ZSTDHL_CHECKED(zstdhl_ReverseBitstream_ReadBitsComplete(bitstream, (uint8_t)litLengthNumBits, &litLength));

		matchLength += matchLengthBaseline;
		litLength += litLengthBaseline;

		seq.m_litLength = litLength;
		seq.m_matchLength = matchLength;
		seq.m_offsetValueBigNum = offsetBigNum;
		seq.m_offsetValueNumBits = offsetSym + 1;

		if (seq.m_offsetValueNumBits <= 2)
		{
			seq.m_offsetType = (seq.m_offsetValueBigNum[0] - 1) + ZSTDHL_OFFSET_TYPE_REPEAT_1;

			if (seq.m_litLength == 0)
			{
				if (seq.m_offsetType == ZSTDHL_OFFSET_TYPE_REPEAT_3)
					seq.m_offsetType = ZSTDHL_OFFSET_TYPE_REPEAT_1_MINUS_1;
				else
					seq.m_offsetType++;
			}

			seq.m_offsetValueBigNum[0] = 0;
			seq.m_offsetValueNumBits = 0;
		}
		else
		{
			seq.m_offsetType = ZSTDHL_OFFSET_TYPE_SPECIFIED;
			ZSTDHL_CHECKED(zstdhl_BigNum_SubtractU32(seq.m_offsetValueBigNum, &seq.m_offsetValueNumBits, 3));
		}

		ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_SEQUENCE, &seq));

		numSequences--;

		if (numSequences >= 1)
		{
			ZSTDHL_CHECKED(zstdhl_ReverseBitstream_ReadBitsComplete(bitstream, litLengthCell->m_numBits, &stateOffset));
			litLengthState = litLengthCell->m_baseline + stateOffset;

			ZSTDHL_CHECKED(zstdhl_ReverseBitstream_ReadBitsComplete(bitstream, matchLengthCell->m_numBits, &stateOffset));
			matchLengthState = matchLengthCell->m_baseline + stateOffset;

			ZSTDHL_CHECKED(zstdhl_ReverseBitstream_ReadBitsComplete(bitstream, offsetTableCell->m_numBits, &stateOffset));
			offsetState = offsetTableCell->m_baseline + stateOffset;
		}
	}

	zstdhl_Buffers_Dealloc(buffers, ZSTDHL_BUFFER_LIT_LENGTH_FSE_TABLE);
	zstdhl_Buffers_Dealloc(buffers, ZSTDHL_BUFFER_OFFSET_FSE_TABLE);
	zstdhl_Buffers_Dealloc(buffers, ZSTDHL_BUFFER_MATCH_LENGTH_FSE_TABLE);

	if (bitstream->m_numBits > 0 || bitstream->m_bytesAvailable > 0)
		return ZSTDHL_RESULT_SEQUENCE_BITSTREAM_INCOMPLETELY_CONSUMED;

	return ZSTDHL_RESULT_OK;
}

typedef struct zstdhl_OffsetProbDecodeState
{
	zstdhl_Buffers_t *m_buffers;
	uint8_t m_alternator;
	size_t m_currentCapacity;
} zstdhl_OffsetProbDecodeState_t;

static zstdhl_ResultCode_t zstdhl_OffsetsRequestMoreCapacity(void *userdata, uint32_t **outProbs, size_t *outNumProbs)
{
	zstdhl_OffsetProbDecodeState_t *state = (zstdhl_OffsetProbDecodeState_t *)userdata;
	zstdhl_Buffers_t *buffers = state->m_buffers;
	size_t oldCapacity = state->m_currentCapacity;
	size_t newCapacity = 0;
	size_t i = 0;
	uint32_t *currentProbs = buffers->m_buffers[ZSTDHL_BUFFER_OFFSET_PROBS_1 + state->m_alternator];
	uint32_t *newProbs = NULL;
	void *newBufferPtr = NULL;

	if ((SIZE_MAX / 2u) < state->m_currentCapacity)
		return ZSTDHL_RESULT_OUT_OF_MEMORY;
	
	newCapacity = state->m_currentCapacity * 2u;

	if (newCapacity < 8)
		newCapacity = 8;

	if ((SIZE_MAX / sizeof(uint32_t)) < newCapacity)
		return ZSTDHL_RESULT_OUT_OF_MEMORY;
	
	ZSTDHL_CHECKED(zstdhl_Buffers_Alloc(buffers, ZSTDHL_BUFFER_OFFSET_PROBS_2 - state->m_alternator, newCapacity * sizeof(uint32_t), &newBufferPtr));
	newProbs = (uint32_t *)newBufferPtr;
	
	for (i = 0; i < oldCapacity; i++)
		newProbs[i] = currentProbs[i];

	zstdhl_Buffers_Dealloc(state->m_buffers, ZSTDHL_BUFFER_OFFSET_PROBS_1 + state->m_alternator);

	state->m_alternator = 1 - state->m_alternator;
	state->m_currentCapacity = newCapacity;
	
	*outProbs = newProbs;
	*outNumProbs = newCapacity;

	return ZSTDHL_RESULT_OK;
}

static void zstdhl_OffsetsClear(void *userdata)
{
	zstdhl_OffsetProbDecodeState_t *state = (zstdhl_OffsetProbDecodeState_t *)userdata;
	zstdhl_Buffers_t *buffers = state->m_buffers;

	zstdhl_Buffers_Dealloc(buffers, ZSTDHL_BUFFER_OFFSET_PROBS_1);
	zstdhl_Buffers_Dealloc(buffers, ZSTDHL_BUFFER_OFFSET_PROBS_2);
}

zstdhl_ResultCode_t zstdhl_ParseSequencesSection(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers, uint32_t blockSize, zstdhl_FramePersistentState_t *pstate)
{
	zstdhl_SliceStreamSource_t slice;
	zstdhl_StreamSourceObject_t sliceStream;
	uint8_t headerByte = 0;
	uint32_t numSequences = 0;
	uint32_t i = 0;
	void *sequencesBufferPtr = NULL;

	slice.m_streamSource.m_readBytesFunc = streamSource->m_readBytesFunc;
	slice.m_streamSource.m_userdata = streamSource->m_userdata;
	slice.m_sizeRemaining = blockSize;

	sliceStream.m_readBytesFunc = zstdhl_SliceStreamSource_ReadBytes;
	sliceStream.m_userdata = &slice;

	ZSTDHL_CHECKED(zstdhl_ReadChecked(&sliceStream, &headerByte, 1, ZSTDHL_RESULT_SEQUENCES_HEADER_TRUNCATED));

	if (headerByte < 128)
		numSequences = headerByte;
	else
	{
		uint8_t nbSeqByte2 = 0;
		ZSTDHL_CHECKED(zstdhl_ReadChecked(&sliceStream, &nbSeqByte2, 1, ZSTDHL_RESULT_SEQUENCES_HEADER_TRUNCATED));

		if (headerByte < 255)
			numSequences = ((headerByte - 0x80u) << 8) + nbSeqByte2;
		else
		{
			uint8_t nbSeqByte3 = 0;
			ZSTDHL_CHECKED(zstdhl_ReadChecked(&sliceStream, &nbSeqByte3, 1, ZSTDHL_RESULT_SEQUENCES_HEADER_TRUNCATED));

			numSequences = (nbSeqByte2 << 8) + nbSeqByte3 + 0x7f00u;
		}
	}

	if (numSequences == 0)
	{
		zstdhl_SequencesSectionDesc_t seqSectionDesc;

		seqSectionDesc.m_literalLengthsMode = ZSTDHL_SEQ_COMPRESSION_MODE_REUSE;
		seqSectionDesc.m_offsetsMode = ZSTDHL_SEQ_COMPRESSION_MODE_REUSE;
		seqSectionDesc.m_matchLengthsMode = ZSTDHL_SEQ_COMPRESSION_MODE_REUSE;
		seqSectionDesc.m_numSequences = 0;

		ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_SEQUENCES_SECTION, &seqSectionDesc));

		return ZSTDHL_RESULT_OK;
	}

	ZSTDHL_CHECKED(zstdhl_ReadChecked(&sliceStream, &headerByte, 1, ZSTDHL_RESULT_SEQUENCES_HEADER_TRUNCATED));

	if (headerByte & 3)
		return ZSTDHL_RESULT_SEQUENCES_COMPRESSION_MODE_RESERVED_BITS_INVALID;

	{
		zstdhl_SequencesSectionDesc_t seqSectionDesc;

		seqSectionDesc.m_literalLengthsMode = (zstdhl_SequencesCompressionMode_t)((headerByte >> 6) & 3);
		seqSectionDesc.m_offsetsMode = (zstdhl_SequencesCompressionMode_t)((headerByte >> 4) & 3);
		seqSectionDesc.m_matchLengthsMode = (zstdhl_SequencesCompressionMode_t)((headerByte >> 2) & 3);
		seqSectionDesc.m_numSequences = numSequences;

		ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_SEQUENCES_SECTION, &seqSectionDesc));
	}

	{
		zstdhl_SimpleProbDecodeState_t litLenProbHandler;
		zstdhl_SimpleProbDecodeState_t matchLenProbHandler;
		zstdhl_OffsetProbDecodeState_t offsetProbHandler;

		offsetProbHandler.m_alternator = 0;
		offsetProbHandler.m_buffers = buffers;
		offsetProbHandler.m_currentCapacity = 0;

		zstdhl_SimpleProbDecodeState_Init(&litLenProbHandler, pstate->m_litLengthProbs, zstdhl_litLenSDef.m_numProbs, ZSTDHL_RESULT_TOO_MANY_PROBS);
		zstdhl_SimpleProbDecodeState_Init(&matchLenProbHandler, pstate->m_matchLengthProbs, zstdhl_matchLenSDef.m_numProbs, ZSTDHL_RESULT_TOO_MANY_PROBS);

		ZSTDHL_CHECKED(zstdhl_ParseCompressionDef(&sliceStream, disassemblyOutput, headerByte, 6, &zstdhl_litLenSDef, &pstate->m_literalLengthsCDef, zstdhl_SimpleProbRequestMoreCapacity, zstdhl_SimpleProbClear, &litLenProbHandler));
		ZSTDHL_CHECKED(zstdhl_ParseCompressionDef(&sliceStream, disassemblyOutput, headerByte, 4, &zstdhl_offsetCodeSDef, &pstate->m_offsetsCDef, zstdhl_OffsetsRequestMoreCapacity, zstdhl_OffsetsClear, &offsetProbHandler));
		ZSTDHL_CHECKED(zstdhl_ParseCompressionDef(&sliceStream, disassemblyOutput, headerByte, 2, &zstdhl_matchLenSDef, &pstate->m_matchLengthsCDef, zstdhl_SimpleProbRequestMoreCapacity, zstdhl_SimpleProbClear, &matchLenProbHandler));
	}

	// Construct offset bignum
	{
		size_t sizeCalc = pstate->m_offsetsCDef.m_fseTableDef.m_numProbabilities;
		void *offsetBigNumBuffer = NULL;

		// We only have to add 1 instead of rounding up with +31 because there is an additional +1 to size
		// due to the implicit 1 bit.
		sizeCalc /= 32u;
		sizeCalc += 1u;

		ZSTDHL_CHECKED(zstdhl_Buffers_Alloc(buffers, ZSTDHL_BUFFER_OFFSET_BIGNUM, sizeCalc * sizeof(uint32_t), &offsetBigNumBuffer));
	}

	{
		uint32_t bitstreamSize = (uint32_t)slice.m_sizeRemaining;
		zstdhl_ReverseBitstream_t revStream;

		ZSTDHL_CHECKED(zstdhl_Buffers_Alloc(buffers, ZSTDHL_BUFFER_FSE_BITSTREAM, bitstreamSize, &sequencesBufferPtr));

		ZSTDHL_CHECKED(zstdhl_ReadChecked(&sliceStream, sequencesBufferPtr, bitstreamSize, ZSTDHL_RESULT_SEQUENCE_BITSTREAM_TOO_SMALL));

		ZSTDHL_CHECKED(zstdhl_ReverseBitstream_Init(&revStream, (const uint8_t *)sequencesBufferPtr, bitstreamSize));

		ZSTDHL_CHECKED(zstdhl_DecodeSequences(&revStream, disassemblyOutput, buffers, &pstate->m_literalLengthsCDef.m_fseTableDef, &pstate->m_offsetsCDef.m_fseTableDef, &pstate->m_matchLengthsCDef.m_fseTableDef, numSequences));

		zstdhl_Buffers_Dealloc(buffers, ZSTDHL_BUFFER_FSE_BITSTREAM);
	}

	zstdhl_Buffers_Dealloc(buffers, ZSTDHL_BUFFER_OFFSET_BIGNUM);

	// Don't deallocate offset probs since they may be reused

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_ParseCompressedBlock(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers, uint32_t blockSize, zstdhl_FramePersistentState_t *pstate)
{
	ZSTDHL_CHECKED(zstdhl_ParseLiteralsSection(streamSource, disassemblyOutput, buffers, &blockSize, pstate));
	ZSTDHL_CHECKED(zstdhl_ParseSequencesSection(streamSource, disassemblyOutput, buffers, blockSize, pstate));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DisassembleImpl(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers, zstdhl_FramePersistentState_t *pstate)
{
	zstdhl_FrameHeaderDesc_t frameHeader;
	int disassembledBlockCount = 0;

	ZSTDHL_CHECKED(zstdhl_ParseFrameHeader(streamSource, &frameHeader));

	disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_FRAME_HEADER, &frameHeader);

	for (;;)
	{
		uint8_t blockHeaderBytes[3];
		zstdhl_BlockHeaderDesc_t blockHeader;

		ZSTDHL_CHECKED(zstdhl_ReadChecked(streamSource, blockHeaderBytes, 3, ZSTDHL_RESULT_BLOCK_HEADER_TRUNCATED));

		blockHeader.m_isLastBlock = (blockHeaderBytes[0] & 1);
		blockHeader.m_blockType = (zstdhl_BlockType_t)((blockHeaderBytes[0] >> 1) & 3);
		blockHeader.m_blockSize = ((blockHeaderBytes[0] >> 3) & 0x1f);
		blockHeader.m_blockSize |= (blockHeaderBytes[1] << 5);
		blockHeader.m_blockSize |= (blockHeaderBytes[2] << 13);

		if (blockHeader.m_blockType == ZSTDHL_BLOCK_TYPE_INVALID)
			return ZSTDHL_RESULT_BLOCK_TYPE_INVALID;

		disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_BLOCK_HEADER, &blockHeader);

		switch (blockHeader.m_blockType)
		{
		case ZSTDHL_BLOCK_TYPE_RLE:
			ZSTDHL_CHECKED(zstdhl_ParseRLEBlock(streamSource, disassemblyOutput, blockHeader.m_blockSize));
			break;
		case ZSTDHL_BLOCK_TYPE_RAW:
			ZSTDHL_CHECKED(zstdhl_ParseRawBlock(streamSource, disassemblyOutput, blockHeader.m_blockSize, ZSTDHL_ELEMENT_TYPE_BLOCK_UNCOMPRESSED_DATA));
			break;
		case ZSTDHL_BLOCK_TYPE_COMPRESSED:
			ZSTDHL_CHECKED(zstdhl_ParseCompressedBlock(streamSource, disassemblyOutput, buffers, blockHeader.m_blockSize, pstate));
			break;
		default:
			// Shouldn't get here
			return ZSTDHL_RESULT_INTERNAL_ERROR;
		}

		ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_BLOCK_END, NULL));

		disassembledBlockCount++;

		if (blockHeader.m_isLastBlock)
			break;
	}

	ZSTDHL_CHECKED(disassemblyOutput->m_reportDisassembledElementFunc(disassemblyOutput->m_userdata, ZSTDHL_ELEMENT_TYPE_FRAME_END, NULL));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_BuildFSEDistributionTable_ZStd(zstdhl_FSETable_t *fseTable, const zstdhl_FSETableDef_t *fseTableDef, zstdhl_FSESymbolTemp_t *symbolTemps)
{
	uint8_t accuracyLog = fseTableDef->m_accuracyLog;
	uint32_t numCells = (1 << accuracyLog);
	zstdhl_FSETableCell_t *cells = fseTable->m_cells;
	uint32_t numNotLowProbCells = numCells;
	size_t i = 0;
	size_t numProbs = fseTableDef->m_numProbabilities;
	const uint32_t *probs = fseTableDef->m_probabilities;

	uint32_t advanceStep = (numCells >> 1) + (numCells >> 3) + 3;
	uint32_t cellMask = numCells - 1;

	uint32_t insertPos = 0;

	fseTable->m_numCells = numCells;
	fseTable->m_accuracyLog = accuracyLog;

	for (i = 0; i < numProbs; i++)
	{
		zstdhl_FSESymbolTemp_t *sym = symbolTemps + i;
		uint32_t effProb = (probs[i] == ZSTDHL_LESS_THAN_ONE_VALUE) ? 1 : probs[i];

		if (effProb > 0)
		{
			int probDivisionBits = zstdhl_Log2_32((effProb - 1) * 2 + 1);	// Should be 5 for 21 through 32

			sym->m_smallSize = accuracyLog - probDivisionBits;
			sym->m_numLargeSteppingRemaining = (uint32_t)(1 << probDivisionBits) - effProb;
			if (sym->m_numLargeSteppingRemaining > 0)
				sym->m_baseline = (1 << accuracyLog) - (sym->m_numLargeSteppingRemaining << (sym->m_smallSize + 1));
			else
				sym->m_baseline = 0;
		}
	}

	for (i = 0; i < numProbs; i++)
	{
		if (probs[i] == ZSTDHL_LESS_THAN_ONE_VALUE)
		{
			numNotLowProbCells--;
			cells[numNotLowProbCells].m_sym = i;
		}
	}

	for (i = 0; i < numProbs; i++)
	{
		uint32_t prob = probs[i];

		if (prob != ZSTDHL_LESS_THAN_ONE_VALUE && prob > 0)
		{
			while (prob > 0)
			{
				while (insertPos >= numNotLowProbCells)
					insertPos = ((insertPos + advanceStep) & cellMask);

				cells[insertPos].m_sym = i;
				prob--;

				insertPos = ((insertPos + advanceStep) & cellMask);
			}
		}
	}

	for (i = 0; i < numCells; i++)
	{
		zstdhl_FSETableCell_t *cell = cells + i;
		size_t symbol = cell->m_sym;
		zstdhl_FSESymbolTemp_t *symTemp = symbolTemps + symbol;

		cell->m_baseline = symTemp->m_baseline;

		if (symTemp->m_numLargeSteppingRemaining)
		{
			symTemp->m_numLargeSteppingRemaining--;
			cell->m_numBits = symTemp->m_smallSize + 1;

			if (symTemp->m_numLargeSteppingRemaining == 0)
				symTemp->m_baseline = 0;
			else
				symTemp->m_baseline += (uint32_t)(1 << (symTemp->m_smallSize + 1));
		}
		else
		{
			cell->m_numBits = symTemp->m_smallSize;
			symTemp->m_baseline += (uint32_t)(1 << symTemp->m_smallSize);
		}
	}

	return ZSTDHL_RESULT_OK;
}

void zstdhl_BuildFSEEncodeTable(zstdhl_FSETableEnc_t *encTable, const zstdhl_FSETable_t *table, size_t numSymbols)
{
	size_t sym = 0;
	size_t addlBits = 0;
	size_t cellIndex = 0;
	size_t numProbs = 0;
	size_t numEncCells = numSymbols << table->m_accuracyLog;

	for (cellIndex = 0; cellIndex < numEncCells; cellIndex++)
		encTable->m_nextStates[cellIndex] = 0xffff;

	for (cellIndex = 0; cellIndex < table->m_numCells; cellIndex++)
	{
		const zstdhl_FSETableCell_t *tableCell = table->m_cells + cellIndex;
		size_t numAddlBitStates = ((size_t)1 << tableCell->m_numBits);

		for (addlBits = 0; addlBits < numAddlBitStates; addlBits++)
			encTable->m_nextStates[(tableCell->m_sym << table->m_accuracyLog) + tableCell->m_baseline + addlBits] = (uint16_t)cellIndex;
	}
}

zstdhl_ResultCode_t zstdhl_FindInitialFSEState(const zstdhl_FSETable_t *table, uint16_t symbol, uint16_t *outInitialState)
{
	size_t i = 0;
	const zstdhl_FSETableCell_t *bestCell = NULL;

	// Initial state, find the smallest value that still drains bits
	for (i = 0; i < table->m_numCells; i++)
	{
		const zstdhl_FSETableCell_t *cell = table->m_cells + i;

		if (cell->m_sym == symbol && cell->m_numBits > 0)
		{
			bestCell = cell;
			break;
		}
	}

	if (!bestCell)
		return ZSTDHL_RESULT_FSE_TABLE_MISSING_SYMBOL;

	*outInitialState = (uint16_t)(bestCell - table->m_cells);

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_EncodeFSEValue(zstdhl_FSEEncStack_t *stack, const zstdhl_FSETableEnc_t *encTable, const zstdhl_FSETable_t *table, uint16_t value)
{
	uint16_t state = 0;
	uint16_t stateMask = (1 << table->m_accuracyLog) - 1;
	uint16_t nextState = 0;

	if (stack->m_statesStackVector.m_count == 0)
	{
		size_t i = 0;
		ZSTDHL_CHECKED(zstdhl_FindInitialFSEState(table, value, &state));

		ZSTDHL_CHECKED(zstdhl_Vector_Append(&stack->m_statesStackVector, &state, 1));

		return ZSTDHL_RESULT_OK;
	}

	state = ((const uint16_t *)stack->m_statesStackVector.m_data)[stack->m_statesStackVector.m_count - 1];

	nextState = encTable->m_nextStates[(value << table->m_accuracyLog) + (state & stateMask)];

	if (nextState == 0xffff)
		return ZSTDHL_RESULT_FSE_TABLE_MISSING_SYMBOL;

	nextState += (state - (state & stateMask));

	ZSTDHL_CHECKED(zstdhl_Vector_Append(&stack->m_statesStackVector, &nextState, 1));

	return ZSTDHL_RESULT_OK;
}

void zstdhl_FSEEncStack_Init(zstdhl_FSEEncStack_t *stack, const zstdhl_MemoryAllocatorObject_t *alloc)
{
	zstdhl_Vector_Init(&stack->m_statesStackVector, sizeof(uint16_t), alloc);
}

zstdhl_ResultCode_t zstdhl_FSEEncStack_Reset(zstdhl_FSEEncStack_t *stack, uint16_t bottomState)
{
	zstdhl_Vector_Clear(&stack->m_statesStackVector);
	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_FSEEncStack_Pop(zstdhl_FSEEncStack_t *stack, uint16_t *outState)
{
	if (stack->m_statesStackVector.m_count == 0)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	*outState = ((const uint16_t *)stack->m_statesStackVector.m_data)[stack->m_statesStackVector.m_count - 1];
	zstdhl_Vector_Shrink(&stack->m_statesStackVector, stack->m_statesStackVector.m_count - 1);

	return ZSTDHL_RESULT_OK;
}

void zstdhl_FSEEncStack_Destroy(zstdhl_FSEEncStack_t *stack)
{
	zstdhl_Vector_Destroy(&stack->m_statesStackVector);
}


const zstdhl_SubstreamCompressionStructureDef_t *zstdhl_GetDefaultLitLengthFSEProperties(void)
{
	return &zstdhl_litLenSDef;
}

const zstdhl_SubstreamCompressionStructureDef_t *zstdhl_GetDefaultMatchLengthFSEProperties(void)
{
	return &zstdhl_matchLenSDef;
}

const zstdhl_SubstreamCompressionStructureDef_t *zstdhl_GetDefaultOffsetFSEProperties(void)
{
	return &zstdhl_offsetCodeSDef;
}

zstdhl_ResultCode_t zstdhl_EncodeOffsetCode(uint32_t value, uint32_t *outFSEValue, uint32_t *outExtraValue, uint8_t *outExtraBits)
{
	uint8_t bitLog2 = 0;

	if (value == 0)
		return ZSTDHL_RESULT_INVALID_VALUE;

	bitLog2 = zstdhl_Log2_32(value);
	*outFSEValue = bitLog2;
	*outExtraValue = value - (((uint32_t)1) << bitLog2);
	*outExtraBits = bitLog2;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_EncodeMatchLength(uint32_t value, uint32_t *outFSEValue, uint32_t *outExtraValue, uint8_t *outExtraBits)
{
	uint8_t bitLog2 = 0;

	if (value < 3)
		return ZSTDHL_RESULT_INVALID_VALUE;

	if (value < 35)
	{
		*outFSEValue = value - 3;
		*outExtraValue = 0;
		*outExtraBits = 0;
		return ZSTDHL_RESULT_OK;
	}

	if (value < 131)
	{
		int baselineIndex = 1;
		for (baselineIndex = 1; baselineIndex < 11; baselineIndex++)
		{
			if (zstdhl_MatchLengthBaselines[baselineIndex] > value)
				break;
		}

		baselineIndex--;

		*outFSEValue = baselineIndex + 32;
		*outExtraValue = value - zstdhl_MatchLengthBaselines[baselineIndex];
		*outExtraBits = zstdhl_MatchLengthBits[baselineIndex];

		return ZSTDHL_RESULT_OK;
	}

	value -= 3;

	bitLog2 = zstdhl_Log2_32(value);
	*outFSEValue = bitLog2 + 43 - 7;
	*outExtraValue = value - (1 << bitLog2);
	*outExtraBits = bitLog2;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_EncodeLitLength(uint32_t value, uint32_t *outFSEValue, uint32_t *outExtraValue, uint8_t *outExtraBits)
{
	uint8_t bitLog2 = 0;

	if (value < 16)
	{
		*outFSEValue = value;
		*outExtraValue = 0;
		*outExtraBits = 0;
		return ZSTDHL_RESULT_OK;
	}

	if (value < 64)
	{
		int baselineIndex = 1;
		for (baselineIndex = 1; baselineIndex < 9; baselineIndex++)
		{
			if (zstdhl_LitLengthBaselines[baselineIndex] > value)
				break;
		}

		baselineIndex--;

		*outFSEValue = baselineIndex + 16;
		*outExtraValue = value - zstdhl_LitLengthBaselines[baselineIndex];
		*outExtraBits = zstdhl_LitLengthBits[baselineIndex];

		return ZSTDHL_RESULT_OK;
	}

	bitLog2 = zstdhl_Log2_32(value);
	*outFSEValue = bitLog2 + 25 - 6;
	*outExtraValue = value - (1 << bitLog2);
	*outExtraBits = bitLog2;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_ResolveOffsetCode32(zstdhl_OffsetType_t offsetType, uint32_t litLength, uint32_t offsetValue, uint32_t *outOffsetCode)
{
	size_t offsetValueDWords = 0;
	size_t i = 0;

	switch (offsetType)
	{
	case ZSTDHL_OFFSET_TYPE_REPEAT_1_MINUS_1:
		if (litLength != 0)
			return ZSTDHL_RESULT_INVALID_VALUE;
		*outOffsetCode = 3;
		return ZSTDHL_RESULT_OK;

	case ZSTDHL_OFFSET_TYPE_REPEAT_1:
		if (litLength == 0)
			return ZSTDHL_RESULT_INVALID_VALUE;

		*outOffsetCode = 1;
		return ZSTDHL_RESULT_OK;

	case ZSTDHL_OFFSET_TYPE_REPEAT_2:
		if (litLength == 0)
			*outOffsetCode = 1;
		else
			*outOffsetCode = 2;
		return ZSTDHL_RESULT_OK;

	case ZSTDHL_OFFSET_TYPE_REPEAT_3:
		if (litLength == 0)
			*outOffsetCode = 2;
		else
			*outOffsetCode = 3;
		return ZSTDHL_RESULT_OK;

	case ZSTDHL_OFFSET_TYPE_SPECIFIED:
		if ((0xffffffffu - 3u) < offsetValue || offsetValue == 0)
			return ZSTDHL_RESULT_INTEGER_OVERFLOW;

		*outOffsetCode = offsetValue + 3;
		return ZSTDHL_RESULT_OK;

	default:
		return ZSTDHL_RESULT_INTERNAL_ERROR;
	}
}

ZSTDHL_EXTERN zstdhl_ResultCode_t zstdhl_Disassemble(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, const zstdhl_MemoryAllocatorObject_t *alloc)
{
	zstdhl_ResultCode_t result = ZSTDHL_RESULT_OK;
	zstdhl_Buffers_t buffers;
	zstdhl_FramePersistentState_t pstate;

	zstdhl_Buffers_Init(&buffers, alloc);
	zstdhl_FramePersistentState_Init(&pstate);

	result = zstdhl_DisassembleImpl(streamSource, disassemblyOutput, &buffers, &pstate);

	zstdhl_Buffers_DeallocAll(&buffers);

	return result;
}

uint32_t zstdhl_GetLessThanOneConstant(void)
{
	return ZSTDHL_LESS_THAN_ONE_VALUE;
}

void zstdhl_Vector_Init(zstdhl_Vector_t *vec, size_t elementSize, const zstdhl_MemoryAllocatorObject_t *alloc)
{
	vec->m_alloc.m_reallocFunc = alloc->m_reallocFunc;
	vec->m_alloc.m_userdata = alloc->m_userdata;
	vec->m_capacity = 0;
	vec->m_data = NULL;
	vec->m_dataEnd = NULL;
	vec->m_count = 0;
	vec->m_elementSize = elementSize;
	vec->m_maxCapacity = SIZE_MAX / elementSize;
}

zstdhl_ResultCode_t zstdhl_Vector_Append(zstdhl_Vector_t *vec, const void *data, size_t count)
{
	size_t numericallyAvailable = vec->m_maxCapacity - vec->m_count;
	size_t requiredCapacity = 0;
	uint8_t *destBytes = NULL;
	size_t i = 0;
	size_t bytesToCopy = 0;
	int wasResized = 0;

	if (count == 0)
		return ZSTDHL_RESULT_OK;

	if (numericallyAvailable < count)
		return ZSTDHL_RESULT_OUT_OF_MEMORY;

	requiredCapacity = vec->m_count + count;

	if (requiredCapacity > vec->m_capacity)
	{
		size_t newCapacityTarget = vec->m_capacity;
		void *newPtr = NULL;

		if (newCapacityTarget == 0)
			newCapacityTarget = 16;

		while (newCapacityTarget < requiredCapacity)
		{
			if (vec->m_maxCapacity / 2u < newCapacityTarget)
				return ZSTDHL_RESULT_OUT_OF_MEMORY;

			newCapacityTarget *= 2u;
		}

		newPtr = vec->m_alloc.m_reallocFunc(vec->m_alloc.m_userdata, vec->m_data, newCapacityTarget * vec->m_elementSize);
		if (!newPtr)
			return ZSTDHL_RESULT_OUT_OF_MEMORY;

		vec->m_data = newPtr;
		vec->m_dataEnd = ((uint8_t *)newPtr) + vec->m_elementSize * vec->m_count;
		vec->m_capacity = newCapacityTarget;
		vec->m_capacityBytes = newCapacityTarget * vec->m_elementSize;

		wasResized = 1;
	}

	destBytes = (uint8_t *)vec->m_dataEnd;

	if (data)
	{
		const uint8_t *srcBytes = (const uint8_t *)data;
		bytesToCopy = count * vec->m_elementSize;

		for (i = 0; i < bytesToCopy; i++)
			destBytes[i] = srcBytes[i];
	}

	vec->m_dataEnd = destBytes + bytesToCopy;
	vec->m_count += count;

	return ZSTDHL_RESULT_OK;
}

void zstdhl_Vector_Clear(zstdhl_Vector_t *vec)
{
	vec->m_count = 0;
	vec->m_dataEnd = vec->m_data;
}

void zstdhl_Vector_Shrink(zstdhl_Vector_t *vec, size_t newCount)
{
	vec->m_count = newCount;
	vec->m_dataEnd = (uint8_t *)vec->m_data + newCount * vec->m_elementSize;
}

void zstdhl_Vector_Reset(zstdhl_Vector_t *vec)
{
	if (vec->m_data)
	{
		vec->m_alloc.m_reallocFunc(vec->m_alloc.m_userdata, vec->m_data, 0);
		vec->m_data = NULL;
		vec->m_dataEnd = NULL;
		vec->m_capacity = 0;
	}
	vec->m_count = 0;
}

void zstdhl_Vector_Destroy(zstdhl_Vector_t *vec)
{
	zstdhl_Vector_Reset(vec);
}

static void zstdhl_AsmPersistentTableState_Init(zstdhl_AsmPersistentTableState_t *tableState, zstdhl_FSETableCell_t *cells)
{
	tableState->m_isAssigned = 0;
	tableState->m_isRLE = 0;
	tableState->m_rleByte = 0;

	tableState->m_table.m_cells = cells;
}

zstdhl_ResultCode_t zstdhl_InitAssemblerState(zstdhl_AssemblerPersistentState_t *persistentState)
{
	persistentState->m_haveHuffmanTree = 0;

	zstdhl_AsmPersistentTableState_Init(&persistentState->m_litLengthTable, persistentState->m_litLengthCells);
	zstdhl_AsmPersistentTableState_Init(&persistentState->m_matchLengthTable, persistentState->m_matchLengthCells);
	zstdhl_AsmPersistentTableState_Init(&persistentState->m_offsetTable, persistentState->m_offsetCells);

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_AssembleFrame(const zstdhl_FrameHeaderDesc_t *encFrame, const zstdhl_EncoderOutputObject_t *assemblyOutput, uint64_t optFrameContentSize)
{
	uint8_t headerData[18];
	uint8_t writeOffset = 0;
	uint8_t frameHeaderDescriptor = 0;
	uint8_t dictIDSize = 0;
	uint8_t fcsSize = 0;
	uint32_t dictID = encFrame->m_dictionaryID;
	uint64_t fcs = encFrame->m_frameContentSize;

	if (encFrame->m_haveDictionaryID && encFrame->m_dictionaryID != 0)
	{
		if (encFrame->m_dictionaryID > 0xffff)
		{
			dictIDSize = 4;
			frameHeaderDescriptor |= (3 << 0);
		}
		else if (encFrame->m_dictionaryID > 0xff)
		{
			dictIDSize = 2;
			frameHeaderDescriptor |= (2 << 0);
		}
		else
		{
			dictIDSize = 1;
			frameHeaderDescriptor |= (1 << 0);
		}
	}

	if (encFrame->m_haveContentChecksum)
		frameHeaderDescriptor |= (1 << 2);

	if (encFrame->m_isSingleSegment)
	{
		if (encFrame->m_haveWindowSize || !encFrame->m_haveFrameContentSize)
			return ZSTDHL_RESULT_INVALID_VALUE;

		frameHeaderDescriptor |= (1 << 5);
	}
	else
	{
		if (!encFrame->m_haveWindowSize)
			return ZSTDHL_RESULT_INVALID_VALUE;
	}

	if (encFrame->m_haveFrameContentSize)
	{
		if (encFrame->m_frameContentSize > 0xffffffffu)
		{
			fcsSize = 8;
			frameHeaderDescriptor |= (3 << 6);
		}
		else if (encFrame->m_frameContentSize > 0xffffu)
		{
			fcsSize = 4;
			frameHeaderDescriptor |= (2 << 6);
		}
		else if (encFrame->m_frameContentSize > 0xffu || !encFrame->m_isSingleSegment)
		{
			fcsSize = 2;
			frameHeaderDescriptor |= (1 << 6);
		}
		else
			fcsSize = 1;
	}

	headerData[writeOffset++] = 0x28;
	headerData[writeOffset++] = 0xb5;
	headerData[writeOffset++] = 0x2f;
	headerData[writeOffset++] = 0xfd;

	headerData[writeOffset++] = frameHeaderDescriptor;

	if (encFrame->m_haveWindowSize)
	{
		uint8_t exponent = 10;
		uint8_t mantissa = 0;
		uint64_t lowBitMask = 0;
		uint8_t windowDesc = 0;

		if (encFrame->m_windowSize < 1024)
			return ZSTDHL_RESULT_INVALID_VALUE;

		while ((encFrame->m_windowSize >> exponent) != 1)
		{
			exponent++;
			if (exponent == 42)
				return ZSTDHL_RESULT_INVALID_VALUE;
		}

		lowBitMask = (((uint64_t)1) << (exponent - 3)) - 1;
		if (encFrame->m_windowSize & lowBitMask)
			return ZSTDHL_RESULT_INVALID_VALUE;

		mantissa = (encFrame->m_windowSize >> (exponent - 3)) & 0x7;

		windowDesc |= (exponent - 10) << 3;
		windowDesc |= (mantissa);

		headerData[writeOffset++] = windowDesc;
	}

	while (dictIDSize > 0)
	{
		headerData[writeOffset++] = (uint8_t)(dictID & 0xff);
		dictID >>= 8;
		dictIDSize--;
	}

	while (fcsSize > 0)
	{
		headerData[writeOffset++] = (uint8_t)(fcs & 0xff);
		fcs >>= 8;
		fcsSize--;
	}

	ZSTDHL_CHECKED(assemblyOutput->m_writeBitstreamFunc(assemblyOutput->m_userdata, headerData, writeOffset));

	return ZSTDHL_RESULT_OK;
}

typedef struct zstdhl_SequenceEncStackItem
{
	size_t m_dcmp;	// TODO: Remove
	uint8_t m_litLengthCode;
	uint8_t m_matchLengthCode;
	uint8_t m_offsetCode;
	uint8_t m_numOffsetExtraBits;
	uint8_t m_numLitLengthExtraBits;
	uint8_t m_numMatchLengthExtraBits;

	uint64_t m_offsetExtraBits;
	uint32_t m_matchLengthExtraBits;
	uint32_t m_litLengthExtraBits;
} zstdhl_SequenceEncStackItem_t;

#define ZSTDHL_ASM_MAX_OFFSET_CODE 31

typedef struct zstdhl_AsmTableState
{
	uint8_t m_maxAccuracyLog;
	uint16_t m_maxSymbols;
	zstdhl_FSETableEnc_t m_encTable;
	const zstdhl_SubstreamCompressionStructureDef_t *m_sdef;
	zstdhl_AsmPersistentTableState_t *m_pstate;
} zstdhl_AsmTableState_t;

void zstdhl_AsmTableState_Init(zstdhl_AsmTableState_t *tableState, uint16_t *nextStates, uint8_t maxAccuracyLog, uint16_t maxSymbol, zstdhl_AsmPersistentTableState_t *pstate, const zstdhl_SubstreamCompressionStructureDef_t *sdef)
{
	tableState->m_maxAccuracyLog = maxAccuracyLog;
	tableState->m_maxSymbols = maxSymbol + 1;
	tableState->m_sdef = sdef;
	tableState->m_pstate = pstate;

	tableState->m_encTable.m_nextStates = nextStates;
}

typedef struct zstdhl_AsmState
{
	zstdhl_Vector_t m_dataBlockVector;
	zstdhl_Vector_t m_litDataVector;
	zstdhl_Vector_t m_huffmanTreeDescVector;
	zstdhl_Vector_t m_huffmanStreamVectors[4];

	zstdhl_Vector_t m_encStackItemVector;

	zstdhl_AssemblerPersistentState_t *m_persistentState;

	uint16_t m_offsetNextStates[(ZSTDHL_ASM_MAX_OFFSET_CODE + 1) << ZSTDHL_MAX_OFFSET_ACCURACY_LOG];
	uint16_t m_matchLengthNextStates[(ZSTDHL_MAX_MATCH_LENGTH_CODE + 1) << ZSTDHL_MAX_MATCH_LENGTH_ACCURACY_LOG];
	uint16_t m_litLengthNextStates[(ZSTDHL_MAX_LIT_LENGTH_CODE + 1) << ZSTDHL_MAX_LIT_LENGTH_ACCURACY_LOG];

	zstdhl_AsmTableState_t m_litLengthEncTable;
	zstdhl_AsmTableState_t m_matchLengthEncTable;
	zstdhl_AsmTableState_t m_offsetEncTable;
} zstdhl_AsmState_t;

static void zstdhl_AsmState_Init(zstdhl_AsmState_t *asmState, zstdhl_AssemblerPersistentState_t *persistentState, const zstdhl_MemoryAllocatorObject_t *alloc)
{
	int i = 0;

	zstdhl_Vector_Init(&asmState->m_dataBlockVector, 1, alloc);
	zstdhl_Vector_Init(&asmState->m_litDataVector, 1, alloc);
	zstdhl_Vector_Init(&asmState->m_huffmanTreeDescVector, 1, alloc);
	zstdhl_Vector_Init(&asmState->m_encStackItemVector, sizeof(zstdhl_SequenceEncStackItem_t), alloc);

	for (i = 0; i < 4; i++)
		zstdhl_Vector_Init(&asmState->m_huffmanStreamVectors[i], 1, alloc);

	zstdhl_AsmTableState_Init(&asmState->m_litLengthEncTable, asmState->m_litLengthNextStates, ZSTDHL_MAX_LIT_LENGTH_ACCURACY_LOG, ZSTDHL_MAX_LIT_LENGTH_CODE, &persistentState->m_litLengthTable, zstdhl_GetDefaultLitLengthFSEProperties());
	zstdhl_AsmTableState_Init(&asmState->m_matchLengthEncTable, asmState->m_matchLengthNextStates, ZSTDHL_MAX_MATCH_LENGTH_ACCURACY_LOG, ZSTDHL_MAX_MATCH_LENGTH_CODE, &persistentState->m_matchLengthTable, zstdhl_GetDefaultMatchLengthFSEProperties());
	zstdhl_AsmTableState_Init(&asmState->m_offsetEncTable, asmState->m_offsetNextStates, ZSTDHL_MAX_OFFSET_ACCURACY_LOG, ZSTDHL_ASM_MAX_OFFSET_CODE, &persistentState->m_offsetTable, zstdhl_GetDefaultOffsetFSEProperties());

	asmState->m_persistentState = persistentState;
}

static void zstdhl_AsmState_Destroy(zstdhl_AsmState_t *asmState)
{
	int i = 0;

	zstdhl_Vector_Destroy(&asmState->m_dataBlockVector);
	zstdhl_Vector_Destroy(&asmState->m_litDataVector);
	zstdhl_Vector_Destroy(&asmState->m_huffmanTreeDescVector);
	zstdhl_Vector_Destroy(&asmState->m_encStackItemVector);

	for (i = 0; i < 4; i++)
		zstdhl_Vector_Destroy(&asmState->m_huffmanStreamVectors[i]);
}

static zstdhl_ResultCode_t zstdhl_WriteLiteralsSectionHeader(zstdhl_AsmState_t *asmState, uint64_t litSectionHeader, uint8_t litSectionHeaderSize)
{
	uint8_t headerBytes[5];
	uint8_t i = 0;

	if (litSectionHeaderSize > 5 || litSectionHeaderSize == 0)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	while (i < litSectionHeaderSize)
	{
		headerBytes[i] = (uint8_t)(litSectionHeader & 0xff);
		litSectionHeader >>= 8;
		i++;
	}

	ZSTDHL_CHECKED(zstdhl_Vector_Append(&asmState->m_dataBlockVector, headerBytes, litSectionHeaderSize));

	return ZSTDHL_RESULT_OK;
}

typedef struct zstdhl_HuffmanEncBitstreamState
{
	uint32_t m_bits;
	uint8_t m_bitsAvailable;
	zstdhl_Vector_t *m_outVector;
} zstdhl_HuffmanEncBitstreamState_t;

static zstdhl_ResultCode_t zstdhl_HuffmanEncBitstreamState_Init(zstdhl_HuffmanEncBitstreamState_t *state, zstdhl_Vector_t *vector)
{
	state->m_outVector = vector;
	state->m_bits = 0;
	state->m_bitsAvailable = 32;

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_FlushHuffmanStream(zstdhl_HuffmanEncBitstreamState_t *encStream, uint8_t numBytesToFlush)
{
	uint32_t bits = encStream->m_bits;
	uint8_t bitsAvailable = encStream->m_bitsAvailable;
	uint8_t bytesToWrite[4];
	int i = 0;

	for (i = 0; i < numBytesToFlush; i++)
	{
		if (bitsAvailable > 24)
			return ZSTDHL_RESULT_INTERNAL_ERROR;

		bytesToWrite[i] = (uint8_t)((bits >> 24) & 0xff);
		bitsAvailable += 8;
		bits = (bits & 0x00ffffffu) << 8;
	}

	ZSTDHL_CHECKED(zstdhl_Vector_Append(encStream->m_outVector, bytesToWrite, numBytesToFlush));

	encStream->m_bits = bits;
	encStream->m_bitsAvailable = bitsAvailable;

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_WriteHuffmanBits(zstdhl_HuffmanEncBitstreamState_t *encStream, uint32_t bits, uint8_t numBits)
{
	if (encStream->m_bitsAvailable < numBits)
	{
		uint8_t bytesToFlush = (32u - encStream->m_bitsAvailable) / 8u;

		ZSTDHL_CHECKED(zstdhl_FlushHuffmanStream(encStream, bytesToFlush));
	}

	encStream->m_bitsAvailable -= numBits;
	encStream->m_bits |= bits << encStream->m_bitsAvailable;

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_WriteHuffmanValue(zstdhl_HuffmanEncBitstreamState_t *encStream, const zstdhl_HuffmanTableEnc_t *encTable, uint8_t symbol)
{
	if (encTable->m_entries[symbol].m_numBits == 0)
		return ZSTDHL_RESULT_HUFFMAN_TREE_MISSING_VALUE;

	return zstdhl_WriteHuffmanBits(encStream, encTable->m_entries[symbol].m_bits, encTable->m_entries[symbol].m_numBits);
}

static zstdhl_ResultCode_t zstdhl_AssembleHuffmanLiterals(zstdhl_AsmState_t *asmState, const zstdhl_EncBlockDesc_t *encBlock, const zstdhl_HuffmanTreePartialWeightDesc_t *partialWeightDesc, uint8_t is4Stream)
{
	zstdhl_HuffmanTableEnc_t encTable;
	uint8_t numStreams = is4Stream ? 4 : 1;
	size_t i = 0;
	zstdhl_HuffmanEncBitstreamState_t bitstreams[4];
	const zstdhl_LiteralsSectionDesc_t *litsDesc = &encBlock->m_litSectionDesc;
	size_t streamSizes[4];

	streamSizes[0] = litsDesc->m_numValues;

	if (is4Stream)
	{
		streamSizes[0] = (streamSizes[0] + 3u) / 4u;
		streamSizes[1] = streamSizes[0];
		streamSizes[2] = streamSizes[1];
		streamSizes[3] = litsDesc->m_numValues - streamSizes[0] * 3u;
	}
	else
		streamSizes[1] = streamSizes[2] = streamSizes[3] = 0;

	for (i = 0; i < numStreams; i++)
	{
		ZSTDHL_CHECKED(zstdhl_HuffmanEncBitstreamState_Init(&bitstreams[i], &asmState->m_huffmanStreamVectors[i]));

		ZSTDHL_CHECKED(zstdhl_WriteHuffmanBits(&bitstreams[i], 1, 1));
	}

	ZSTDHL_CHECKED(zstdhl_GenerateHuffmanEncodeTable(partialWeightDesc, &encTable));

	for (i = 0; i < numStreams; i++)
	{
		size_t streamSize = streamSizes[i];
		uint8_t buffer[1024];
		size_t sizeRemaining = streamSize;
		zstdhl_HuffmanEncBitstreamState_t *bitstream = &bitstreams[i];
		uint8_t numPaddingBits = 0;

		while (sizeRemaining > 0)
		{
			size_t amountToRead = sizeof(buffer);
			size_t amountRead = 0;
			size_t j = 0;

			if (amountToRead > sizeRemaining)
				amountToRead = sizeRemaining;

			amountRead = encBlock->m_litSectionDesc.m_decompressedLiteralsStream->m_readBytesFunc(encBlock->m_litSectionDesc.m_decompressedLiteralsStream->m_userdata, buffer, amountToRead);
			if (amountRead != amountToRead)
				return ZSTDHL_RESULT_LITERALS_SECTION_TRUNCATED;

			for (j = 0; j < amountRead; j++)
			{
				ZSTDHL_CHECKED(zstdhl_WriteHuffmanValue(bitstream, &encTable, buffer[j]));
			}
			sizeRemaining -= amountRead;
		}

		numPaddingBits = (bitstream->m_bitsAvailable % 8u);
		if (numPaddingBits != 0)
		{
			ZSTDHL_CHECKED(zstdhl_WriteHuffmanBits(bitstream, 0, numPaddingBits));
		}

		ZSTDHL_CHECKED(zstdhl_FlushHuffmanStream(bitstream, (32u - bitstream->m_bitsAvailable) / 8u));

		{
			// Flip the bitstream byte order
			size_t bitstreamSize = bitstream->m_outVector->m_count;
			uint8_t *bitstreamBytes = (uint8_t *)bitstream->m_outVector->m_data;
			size_t j = 0;

			for (j = 0; j < bitstreamSize / 2u; j++)
			{
				uint8_t *swapTargetA = bitstreamBytes + j;
				uint8_t *swapTargetB = bitstreamBytes + bitstreamSize - 1u - j;

				uint8_t temp = *swapTargetA;
				*swapTargetA = *swapTargetB;
				*swapTargetB = temp;
			}

			// Shift padding out padding
			if (numPaddingBits > 0)
			{
				uint8_t invPaddingBits = 8 - numPaddingBits;

				for (j = 1; j < bitstreamSize; j++)
				{
					uint8_t lowByte = bitstreamBytes[j - 1];
					uint8_t highByte = bitstreamBytes[j];

					bitstreamBytes[j - 1] = (uint8_t)((((highByte << 8) | lowByte) >> numPaddingBits) & 0xff);
				}

				bitstreamBytes[bitstreamSize - 1] >>= numPaddingBits;
			}
		}
	}

	return ZSTDHL_RESULT_OK;
}

typedef struct zstdhl_EncLittleEndianBitstreamState
{
	uint32_t m_bits;
	uint8_t m_numBits;
	zstdhl_Vector_t *m_outVector;
} zstdhl_EncLittleEndianBitstreamState_t;

static zstdhl_ResultCode_t zstdhl_EncLittleEndianBitstreamState_Init(zstdhl_EncLittleEndianBitstreamState_t *state, zstdhl_Vector_t *vector)
{
	state->m_outVector = vector;
	state->m_bits = 0;
	state->m_numBits = 0;

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_FlushLEStreamBytes(zstdhl_EncLittleEndianBitstreamState_t *state, uint8_t numBytes)
{
	size_t i = 0;
	uint8_t bytes[4];

	for (i = 0; i < numBytes; i++)
	{
		if (state->m_numBits < 8u)
			return ZSTDHL_RESULT_INTERNAL_ERROR;

		bytes[i] = (uint8_t)(state->m_bits & 0xffu);

		state->m_numBits -= 8;
		state->m_bits >>= 8;
	}

	ZSTDHL_CHECKED(zstdhl_Vector_Append(state->m_outVector, bytes, numBytes));

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_WriteLEStreamBits(zstdhl_EncLittleEndianBitstreamState_t *state, uint32_t bits, uint8_t numBits)
{
	uint32_t bitsAvailable = 32 - state->m_numBits;

	if (bitsAvailable < numBits)
	{
		uint8_t bytesToFlush = state->m_numBits / 8u;
		ZSTDHL_CHECKED(zstdhl_FlushLEStreamBytes(state, bytesToFlush));
	}

	state->m_bits |= (bits << state->m_numBits);
	state->m_numBits += numBits;

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_WriteFSETableDesc(zstdhl_EncLittleEndianBitstreamState_t *bitstream, const zstdhl_FSETableDef_t *def)
{
	uint32_t slotUsageTotal = 0;
	uint32_t cumulativeSlotUsage = 0;
	size_t i = 0;

	ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(bitstream, def->m_accuracyLog - ZSTDHL_MIN_ACCURACY_LOG, 4));

	for (i = 0; i < def->m_numProbabilities; i++)
	{
		uint32_t prob = def->m_probabilities[i];
		if (prob)
		{
			uint32_t slotUsage = (prob == zstdhl_GetLessThanOneConstant()) ? 1 : prob;
			slotUsageTotal += slotUsage;
		}
	}

	if (slotUsageTotal != (size_t)1u << def->m_accuracyLog)
		return ZSTDHL_RESULT_PROBABILITY_TABLE_INVALID;

	i = 0;
	while (cumulativeSlotUsage < slotUsageTotal)
	{
		uint32_t prob = def->m_probabilities[i++];
		uint32_t probCodedValue = (prob == zstdhl_GetLessThanOneConstant()) ? 0 : prob + 1;
		uint32_t slotUsage = (prob == zstdhl_GetLessThanOneConstant()) ? 1 : prob;
		uint32_t maxProbValue = slotUsageTotal - cumulativeSlotUsage + 1;	// 256
		int nextPO2Log = zstdhl_Log2_32(maxProbValue) + 1;		// 9
		uint32_t nextPO2 = (uint32_t)(1 << nextPO2Log);			// 512
		uint32_t lowRangeCutoff = nextPO2 - 1u - maxProbValue;	// 255
		uint32_t halfNextPO2 = nextPO2 / 2u;					// 256

		if (probCodedValue < lowRangeCutoff)
		{
			ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(bitstream, probCodedValue, nextPO2Log - 1));
		}
		else
		{
			if (probCodedValue >= halfNextPO2)
				probCodedValue += lowRangeCutoff;

			ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(bitstream, probCodedValue, nextPO2Log));
		}

		if (prob == 0)
		{
			uint32_t numRepeats = 0;
			uint8_t needToWriteMore = 1;

			while (def->m_probabilities[i] == 0)
			{
				numRepeats++;
				i++;
			}

			for (;;)
			{
				if (numRepeats < 3)
				{
					ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(bitstream, numRepeats, 2));
					break;
				}
				else
				{
					ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(bitstream, 3, 2));
					numRepeats -= 3;
				}
			}
		}

		cumulativeSlotUsage += slotUsage;
	}

	{
		uint8_t paddingBits = 8u - (bitstream->m_numBits % 8u);
		if (paddingBits != 8u)
		{
			// TODO: Write waste bits here
			ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(bitstream, 0, paddingBits));
		}
	}

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_AssembleHuffmanDesc(zstdhl_AsmState_t *asmState, const zstdhl_HuffmanTreeDesc_t *desc)
{
	zstdhl_EncLittleEndianBitstreamState_t bitstream;
	size_t i = 0;

	ZSTDHL_CHECKED(zstdhl_EncLittleEndianBitstreamState_Init(&bitstream, &asmState->m_huffmanTreeDescVector));

	if (desc->m_huffmanWeightFormat == ZSTDHL_HUFFMAN_WEIGHT_ENCODING_UNCOMPRESSED)
	{
		uint8_t headerByte = 128 + desc->m_partialWeightDesc.m_numSpecifiedWeights;

		if (desc->m_partialWeightDesc.m_numSpecifiedWeights > 128)
			return ZSTDHL_RESULT_HUFFMAN_TOO_MANY_WEIGHTS_FOR_DIRECT_ENCODING;

		ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, headerByte, 8));

		for (i = 0; i < desc->m_partialWeightDesc.m_numSpecifiedWeights; i++)
		{
			ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, desc->m_partialWeightDesc.m_specifiedWeights[i], 4));
		}
	}
	else if (desc->m_huffmanWeightFormat == ZSTDHL_HUFFMAN_WEIGHT_ENCODING_FSE)
	{
		uint8_t numSpecifiedWeights = desc->m_partialWeightDesc.m_numSpecifiedWeights;
		uint8_t headerByte = 0;
		zstdhl_FSETableEnc_t encTable;
		zstdhl_FSETable_t fseTable;
		zstdhl_FSETableCell_t huffWeightCells[1 << ZSTDHL_MAX_HUFFMAN_WEIGHT_ACCURACY_LOG];
		uint16_t huffWeightNextStates[(ZSTDHL_MAX_HUFFMAN_WEIGHT + 1) << ZSTDHL_MAX_HUFFMAN_WEIGHT_ACCURACY_LOG];
		zstdhl_FSESymbolTemp_t symTemps[256];
		uint8_t accuracyLog = desc->m_weightTable.m_accuracyLog;
		uint16_t states[2] = { 0, 0 };

		if (desc->m_weightTable.m_numProbabilities < 2)
			return ZSTDHL_RESULT_HUFFMAN_NOT_ENOUGH_WEIGHTS_FOR_FSE_MODE;

		fseTable.m_cells = huffWeightCells;

		if (desc->m_weightTable.m_accuracyLog < ZSTDHL_MIN_ACCURACY_LOG)
			return ZSTDHL_RESULT_ACCURACY_LOG_TOO_SMALL;

		if (desc->m_weightTable.m_accuracyLog > ZSTDHL_MAX_HUFFMAN_WEIGHT_ACCURACY_LOG)
			return ZSTDHL_RESULT_ACCURACY_LOG_TOO_LARGE;

		ZSTDHL_CHECKED(zstdhl_BuildFSEDistributionTable_ZStd(&fseTable, &desc->m_weightTable, symTemps));

		encTable.m_nextStates = huffWeightNextStates;

		zstdhl_BuildFSEEncodeTable(&encTable, &fseTable, ZSTDHL_MAX_HUFFMAN_WEIGHT + 1);

		ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, headerByte, 8));

		ZSTDHL_CHECKED(zstdhl_WriteFSETableDesc(&bitstream, &desc->m_weightTable));
		
		for (i = 0; i < numSpecifiedWeights; i++)
		{
			size_t ri = numSpecifiedWeights - 1 - i;
			uint16_t *statePtr = states + (ri & 1);
			uint8_t weight = desc->m_partialWeightDesc.m_specifiedWeights[ri];

			if (i < 2)
			{
				size_t j = 0;

				ZSTDHL_CHECKED(zstdhl_FindInitialFSEState(&fseTable, weight, statePtr));
			}
			else
			{
				uint16_t oldState = *statePtr;
				uint16_t newState = encTable.m_nextStates[(weight << accuracyLog) + oldState];
				const zstdhl_FSETableCell_t *cell = fseTable.m_cells + newState;

				if (oldState < cell->m_baseline || oldState - cell->m_baseline >= (1 << cell->m_numBits) || cell->m_sym != weight)
					return ZSTDHL_RESULT_INTERNAL_ERROR;

				*statePtr = newState;

				ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, oldState - cell->m_baseline, cell->m_numBits));
			}
		}

		ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, states[1], accuracyLog));
		ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, states[0], accuracyLog));

		ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, 1, 1));
	}
	else
		return ZSTDHL_RESULT_INVALID_VALUE;

	// Write padding bits
	{
		uint8_t numPaddingBits = 8u - (bitstream.m_numBits % 8u);

		if (numPaddingBits != 8u)
		{
			ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, 0, numPaddingBits));
		}
	}

	ZSTDHL_CHECKED(zstdhl_FlushLEStreamBytes(&bitstream, bitstream.m_numBits / 8u));

	// Replace header byte
	if (desc->m_huffmanWeightFormat == ZSTDHL_HUFFMAN_WEIGHT_ENCODING_FSE)
		*(uint8_t *)(bitstream.m_outVector->m_data) = (uint8_t)(bitstream.m_outVector->m_count - 1u);

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_AssembleHuffmanLiteralsSection(zstdhl_AsmState_t *asmState, const zstdhl_EncBlockDesc_t *encBlock, uint8_t isReuse)
{
	zstdhl_HuffmanTreePartialWeightDesc_t *treeDesc = &asmState->m_persistentState->m_huffmanTree;
	uint64_t litSectionHeader = encBlock->m_litSectionHeader.m_sectionType;
	uint8_t litSectionHeaderSize = 0;
	size_t compressedSize = encBlock->m_litSectionHeader.m_compressedSize;
	size_t regeneratedSize = encBlock->m_litSectionHeader.m_regeneratedSize;
	size_t i = 0;
	uint8_t is4Stream = (encBlock->m_litSectionDesc.m_huffmanStreamMode == ZSTDHL_HUFFMAN_STREAM_MODE_4_STREAMS);

	if (isReuse)
	{
		if (!asmState->m_persistentState->m_haveHuffmanTree)
			return ZSTDHL_RESULT_INVALID_VALUE;
	}
	else
	{
		const zstdhl_HuffmanTreePartialWeightDesc_t *srcDesc = &encBlock->m_huffmanTreeDesc.m_partialWeightDesc;

		for (i = 0; i < sizeof(zstdhl_HuffmanTreePartialWeightDesc_t); i++)
			((uint8_t *)treeDesc)[i] = ((const uint8_t *)srcDesc)[i];

		asmState->m_persistentState->m_haveHuffmanTree = 1;

		ZSTDHL_CHECKED(zstdhl_AssembleHuffmanDesc(asmState, &encBlock->m_huffmanTreeDesc));
	}

	ZSTDHL_CHECKED(zstdhl_AssembleHuffmanLiterals(asmState, encBlock, treeDesc, is4Stream));

	// TODO: Allow compressed and regenerated field sizes to be specified
	if (encBlock->m_autoLitRegeneratedSizeFlag)
		regeneratedSize = encBlock->m_litSectionDesc.m_numValues;

	if (encBlock->m_autoLitCompressedSizeFlag)
	{
		compressedSize = 0;
		compressedSize += asmState->m_huffmanTreeDescVector.m_count;

		if (is4Stream)
			compressedSize += 6;

		for (i = 0; i < 4; i++)
			compressedSize += asmState->m_huffmanStreamVectors[i].m_count;
	}

	if (encBlock->m_litSectionDesc.m_huffmanStreamMode == ZSTDHL_HUFFMAN_STREAM_MODE_1_STREAM)
	{
		if (regeneratedSize >= 1024 || compressedSize >= 1024)
			return ZSTDHL_RESULT_LITERALS_SECTION_TOO_MUCH_DATA_FOR_1_STREAM_MODE;

		litSectionHeader |= ((uint64_t)regeneratedSize) << 4;
		litSectionHeader |= ((uint64_t)compressedSize) << 14;
		litSectionHeaderSize = 3;
	}
	else
	{
		if (!is4Stream)
			return ZSTDHL_RESULT_HUFFMAN_STREAM_MODE_INVALID;

		if (regeneratedSize >= 262144 || compressedSize >= 262144)
			return ZSTDHL_RESULT_LITERALS_SECTION_TOO_LARGE;
		else if (regeneratedSize >= 16384 || compressedSize >= 16384)
		{
			litSectionHeader |= 3 << 2;
			litSectionHeader |= ((uint64_t)regeneratedSize) << 4;
			litSectionHeader |= ((uint64_t)compressedSize) << (18 + 4);
			litSectionHeaderSize = 5;
		}
		else if (regeneratedSize >= 1024 || compressedSize >= 1024)
		{
			litSectionHeader |= 2 << 2;
			litSectionHeader |= ((uint64_t)regeneratedSize) << 4;
			litSectionHeader |= ((uint64_t)compressedSize) << (14 + 4);
			litSectionHeaderSize = 4;
		}
		else
		{
			litSectionHeader |= 1 << 2;
			litSectionHeader |= ((uint64_t)regeneratedSize) << 4;
			litSectionHeader |= ((uint64_t)compressedSize) << (10 + 4);
			litSectionHeaderSize = 3;
		}
	}

	ZSTDHL_CHECKED(zstdhl_WriteLiteralsSectionHeader(asmState, litSectionHeader, litSectionHeaderSize));

	ZSTDHL_CHECKED(zstdhl_Vector_Append(&asmState->m_dataBlockVector, asmState->m_huffmanTreeDescVector.m_data, asmState->m_huffmanTreeDescVector.m_count));
	zstdhl_Vector_Reset(&asmState->m_huffmanTreeDescVector);

	if (is4Stream)
	{
		uint8_t jumpTableBytes[6];

		for (i = 0; i < 3; i++)
		{
			size_t partialSize = encBlock->m_litSectionDesc.m_huffmanStreamSizes[i];
			if (encBlock->m_autoHuffmanStreamSizesFlags[i])
				partialSize = asmState->m_huffmanStreamVectors[i].m_count;

			if (partialSize >= 65536)
				return ZSTDHL_RESULT_HUFFMAN_BITSTREAM_TOO_LARGE;

			jumpTableBytes[i * 2 + 0] = (uint8_t)(partialSize & 0xff);
			jumpTableBytes[i * 2 + 1] = (uint8_t)((partialSize >> 8) & 0xff);
		}

		ZSTDHL_CHECKED(zstdhl_Vector_Append(&asmState->m_dataBlockVector, jumpTableBytes, 6));
	}

	for (i = 0; i < 4; i++)
	{
		ZSTDHL_CHECKED(zstdhl_Vector_Append(&asmState->m_dataBlockVector, asmState->m_huffmanStreamVectors[i].m_data, asmState->m_huffmanStreamVectors[i].m_count));
		zstdhl_Vector_Reset(&asmState->m_huffmanStreamVectors[i]);
	}

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_AssembleLiteralsSection(zstdhl_AsmState_t *asmState, const zstdhl_EncBlockDesc_t *encBlock)
{
	{
		uint32_t litSectionHeader = encBlock->m_litSectionHeader.m_sectionType;
		uint8_t litSectionHeaderSize = 0;
		size_t numLitsExpected = 0;

		switch (encBlock->m_litSectionHeader.m_sectionType)
		{
		case ZSTDHL_LITERALS_SECTION_TYPE_RAW:
		case ZSTDHL_LITERALS_SECTION_TYPE_RLE:
			// TODO: Allow the size of regenerated size field to be specified
			if (encBlock->m_litSectionHeader.m_regeneratedSize >= 1048576)
				return ZSTDHL_RESULT_LITERALS_SECTION_REGENERATED_SIZE_INVALID;
			else if (encBlock->m_litSectionHeader.m_regeneratedSize >= 4096)
			{
				litSectionHeader |= (3 << 2);
				litSectionHeader |= (encBlock->m_litSectionHeader.m_regeneratedSize << 4);
				litSectionHeaderSize = 3;
			}
			else if (encBlock->m_litSectionHeader.m_regeneratedSize >= 32)
			{
				litSectionHeader |= (1 << 2);
				litSectionHeader |= (encBlock->m_litSectionHeader.m_regeneratedSize << 4);
				litSectionHeaderSize = 2;
			}
			else
			{
				litSectionHeader |= (encBlock->m_litSectionHeader.m_regeneratedSize << 3);
				litSectionHeaderSize = 1;
			}

			if (encBlock->m_litSectionHeader.m_sectionType == ZSTDHL_LITERALS_SECTION_TYPE_RLE)
				numLitsExpected = 1;
			else
				numLitsExpected = encBlock->m_litSectionHeader.m_regeneratedSize;

			if (numLitsExpected != encBlock->m_litSectionDesc.m_numValues)
				return ZSTDHL_RESULT_LITERALS_SECTION_VALUE_COUNT_MISMATCH;

			ZSTDHL_CHECKED(zstdhl_WriteLiteralsSectionHeader(asmState, litSectionHeader, litSectionHeaderSize));

			{
				// Write literals data
				uint8_t buffer[1024];
				size_t litsRemaining = numLitsExpected;
				while (litsRemaining > 0)
				{
					size_t amountToRead = sizeof(buffer);
					size_t amountRead = 0;

					if (amountToRead > litsRemaining)
						amountToRead = litsRemaining;

					amountRead = encBlock->m_litSectionDesc.m_decompressedLiteralsStream->m_readBytesFunc(encBlock->m_litSectionDesc.m_decompressedLiteralsStream->m_userdata, buffer, amountToRead);

					if (amountToRead != amountRead)
						return ZSTDHL_RESULT_INPUT_FAILED;

					ZSTDHL_CHECKED(zstdhl_Vector_Append(&asmState->m_dataBlockVector, buffer, amountRead));
					litsRemaining -= amountRead;
				}
			}

			break;
		case ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN:
		case ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN_REUSE:
			ZSTDHL_CHECKED(zstdhl_AssembleHuffmanLiteralsSection(asmState, encBlock, encBlock->m_litSectionHeader.m_sectionType == ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN_REUSE));
			break;
		}
	}

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_AssembleSequencesSectionTableDef(zstdhl_AsmState_t *asmState, zstdhl_AsmTableState_t *tableState, zstdhl_EncLittleEndianBitstreamState_t *bitstream, zstdhl_SequencesCompressionMode_t compMode, const zstdhl_EncSeqCompressionDesc_t *desc)
{
	switch (compMode)
	{
	case ZSTDHL_SEQ_COMPRESSION_MODE_FSE:
		{
			zstdhl_FSESymbolTemp_t symbolTemps[64];

			if (desc->m_fseProbs->m_accuracyLog > tableState->m_maxAccuracyLog)
				return ZSTDHL_RESULT_ACCURACY_LOG_TOO_LARGE;

			if (desc->m_fseProbs->m_accuracyLog < ZSTDHL_MIN_ACCURACY_LOG)
				return ZSTDHL_RESULT_ACCURACY_LOG_TOO_SMALL;

			if (desc->m_fseProbs->m_numProbabilities > tableState->m_maxSymbols)
				return ZSTDHL_RESULT_TOO_MANY_PROBS;

			tableState->m_pstate->m_isAssigned = 1;
			tableState->m_pstate->m_isRLE = 0;
			ZSTDHL_CHECKED(zstdhl_BuildFSEDistributionTable_ZStd(&tableState->m_pstate->m_table, desc->m_fseProbs, symbolTemps));
			zstdhl_BuildFSEEncodeTable(&tableState->m_encTable, &tableState->m_pstate->m_table, tableState->m_maxSymbols);

			ZSTDHL_CHECKED(zstdhl_WriteFSETableDesc(bitstream, desc->m_fseProbs));
		}
		break;
	case ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED:
		{
			zstdhl_FSETableDef_t tableDef;
			zstdhl_FSESymbolTemp_t symbolTemps[64];

			tableDef.m_accuracyLog = tableState->m_sdef->m_defaultAccuracyLog;
			tableDef.m_numProbabilities = tableState->m_sdef->m_numProbs;
			tableDef.m_probabilities = tableState->m_sdef->m_defaultProbs;

			tableState->m_pstate->m_isAssigned = 1;
			tableState->m_pstate->m_isRLE = 0;
			ZSTDHL_CHECKED(zstdhl_BuildFSEDistributionTable_ZStd(&tableState->m_pstate->m_table, &tableDef, symbolTemps));
			zstdhl_BuildFSEEncodeTable(&tableState->m_encTable, &tableState->m_pstate->m_table, tableState->m_maxSymbols);
		}
		break;
	case ZSTDHL_SEQ_COMPRESSION_MODE_REUSE:
		if (!tableState->m_pstate->m_isAssigned)
			return ZSTDHL_RESULT_REUSED_TABLE_WITHOUT_EXISTING_TABLE;

		if (!tableState->m_pstate->m_isRLE)
			zstdhl_BuildFSEEncodeTable(&tableState->m_encTable, &tableState->m_pstate->m_table, tableState->m_maxSymbols);
		break;
	case ZSTDHL_SEQ_COMPRESSION_MODE_RLE:
		tableState->m_pstate->m_isRLE = 1;
		tableState->m_pstate->m_rleByte = desc->m_rleByte;
		tableState->m_pstate->m_isAssigned = 1;
		ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(bitstream, desc->m_rleByte, 8));
		break;
	default:
		return ZSTDHL_RESULT_INTERNAL_ERROR;
	}

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_AssembleSequenceStateUpdate(size_t index, zstdhl_EncLittleEndianBitstreamState_t *bitstream, const zstdhl_AsmTableState_t *tableState, uint16_t sym, uint16_t *state)
{
	if (tableState->m_pstate->m_isRLE)
	{
		if (tableState->m_pstate->m_rleByte != sym)
			return ZSTDHL_RESULT_SYMBOL_DOES_NOT_MATCH_RLE;
	}
	else
	{
		if (index == 0)
		{
			ZSTDHL_CHECKED(zstdhl_FindInitialFSEState(&tableState->m_pstate->m_table, sym, state));
		}
		else
		{
			uint16_t oldState = *state;
			uint16_t newState = tableState->m_encTable.m_nextStates[(sym << tableState->m_pstate->m_table.m_accuracyLog) + (*state)];
			const zstdhl_FSETableCell_t *cell = NULL;

			if (newState == 0xffff)
				return ZSTDHL_RESULT_FSE_TABLE_MISSING_SYMBOL;
			
			cell = tableState->m_pstate->m_table.m_cells + newState;

			ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(bitstream, oldState - cell->m_baseline, cell->m_numBits));

			*state = newState;
		}
	}

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_AssembleSequencesSection(zstdhl_AsmState_t *asmState, const zstdhl_EncBlockDesc_t *encBlock)
{
	const zstdhl_SequencesSectionDesc_t *seqDesc = &encBlock->m_seqSectionDesc;
	const zstdhl_SequenceCollectionObject_t *seqCollection = &encBlock->m_seqCollection;
	uint32_t numSequences = seqDesc->m_numSequences;
	size_t i = 0;
	size_t amountDecompressed = 0;

	for (i = 0; i < numSequences; i++)
	{
		uint32_t litLengthFSEValue = 0;
		uint32_t litLengthExtraValue = 0;
		uint8_t litLengthExtraBits = 0;
		uint32_t matchLengthFSEValue = 0;
		uint32_t matchLengthExtraValue = 0;
		uint8_t matchLengthExtraBits = 0;
		uint32_t offsetCode = 0;
		uint32_t offsetCodeFSEValue = 0;
		uint32_t offsetCodeExtraValue = 0;
		uint8_t offsetCodeExtraBits = 0;
		zstdhl_SequenceDesc_t seq;
		zstdhl_SequenceEncStackItem_t encStackItem;

		ZSTDHL_CHECKED(seqCollection->m_getNextSequence(seqCollection->m_userdata, &seq));

		ZSTDHL_CHECKED(zstdhl_EncodeLitLength(seq.m_litLength, &litLengthFSEValue, &litLengthExtraValue, &litLengthExtraBits));
		ZSTDHL_CHECKED(zstdhl_EncodeMatchLength(seq.m_matchLength, &matchLengthFSEValue, &matchLengthExtraValue, &matchLengthExtraBits));

		// TODO: Support more bits
		if (seq.m_offsetValueNumBits > 32)
			return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;

		ZSTDHL_CHECKED(zstdhl_ResolveOffsetCode32(seq.m_offsetType, seq.m_litLength, (seq.m_offsetValueNumBits > 0) ? seq.m_offsetValueBigNum[0] : 0, &offsetCode));

		ZSTDHL_CHECKED(zstdhl_EncodeOffsetCode(offsetCode, &offsetCodeFSEValue, &offsetCodeExtraValue, &offsetCodeExtraBits));

		encStackItem.m_dcmp = amountDecompressed;
		encStackItem.m_litLengthCode = litLengthFSEValue;
		encStackItem.m_matchLengthCode = matchLengthFSEValue;
		encStackItem.m_offsetCode = offsetCodeFSEValue;
		encStackItem.m_numOffsetExtraBits = offsetCodeExtraBits;
		encStackItem.m_numLitLengthExtraBits = litLengthExtraBits;
		encStackItem.m_numMatchLengthExtraBits = matchLengthExtraBits;
		encStackItem.m_offsetExtraBits = offsetCodeExtraValue;
		encStackItem.m_matchLengthExtraBits = matchLengthExtraValue;
		encStackItem.m_litLengthExtraBits = litLengthExtraValue;

		ZSTDHL_CHECKED(zstdhl_Vector_Append(&asmState->m_encStackItemVector, &encStackItem, 1));

		amountDecompressed += seq.m_litLength + seq.m_matchLength;
	}

	{
		zstdhl_EncLittleEndianBitstreamState_t bitstream;

		ZSTDHL_CHECKED(zstdhl_EncLittleEndianBitstreamState_Init(&bitstream, &asmState->m_dataBlockVector));

		// Write sequences section header
		// TODO: Make sized
		if (numSequences < 128)
		{
			ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, numSequences, 8));
		}
		else if (numSequences < 0x7f00)
		{
			uint32_t sequencesHeader = 0;
			sequencesHeader |= (numSequences >> 8) + 0x80;
			sequencesHeader |= ((numSequences & 0xff) << 8);
			ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, sequencesHeader, 16));
		}
		else if (numSequences < 0x17F00)
		{
			uint32_t sequencesHeader = (numSequences << 8) - 0x7EFF01;
			ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, sequencesHeader, 24));
		}

		if (numSequences > 0)
		{
			// Write compression modes
			ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, 0, 2));
			ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, encBlock->m_seqSectionDesc.m_matchLengthsMode, 2));
			ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, encBlock->m_seqSectionDesc.m_offsetsMode, 2));
			ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, encBlock->m_seqSectionDesc.m_literalLengthsMode, 2));

			// Write lit length table
			ZSTDHL_CHECKED(zstdhl_AssembleSequencesSectionTableDef(asmState, &asmState->m_litLengthEncTable, &bitstream, encBlock->m_seqSectionDesc.m_literalLengthsMode, &encBlock->m_literalLengthsCompressionDesc));

			// Write offset table
			ZSTDHL_CHECKED(zstdhl_AssembleSequencesSectionTableDef(asmState, &asmState->m_offsetEncTable, &bitstream, encBlock->m_seqSectionDesc.m_offsetsMode, &encBlock->m_offsetsModeCompressionDesc));

			// Write match length table
			ZSTDHL_CHECKED(zstdhl_AssembleSequencesSectionTableDef(asmState, &asmState->m_matchLengthEncTable, &bitstream, encBlock->m_seqSectionDesc.m_matchLengthsMode, &encBlock->m_matchLengthsCompressionDesc));

			// Resolve coded sequence values
			{
				uint16_t litLengthState = 0;
				uint16_t matchLengthState = 0;
				uint16_t offsetState = 0;

				for (i = 0; i < numSequences; i++)
				{
					size_t ri = numSequences - 1 - i;
					const zstdhl_SequenceEncStackItem_t *stackItem = ((const zstdhl_SequenceEncStackItem_t *)asmState->m_encStackItemVector.m_data) + ri;

					ZSTDHL_CHECKED(zstdhl_AssembleSequenceStateUpdate(i, &bitstream, &asmState->m_offsetEncTable, stackItem->m_offsetCode, &offsetState));
					ZSTDHL_CHECKED(zstdhl_AssembleSequenceStateUpdate(i, &bitstream, &asmState->m_matchLengthEncTable, stackItem->m_matchLengthCode, &matchLengthState));
					ZSTDHL_CHECKED(zstdhl_AssembleSequenceStateUpdate(i, &bitstream, &asmState->m_litLengthEncTable, stackItem->m_litLengthCode, &litLengthState));

					// Encode bits
					ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, stackItem->m_litLengthExtraBits, stackItem->m_numLitLengthExtraBits));
					ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, stackItem->m_matchLengthExtraBits, stackItem->m_numMatchLengthExtraBits));
					ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, (uint32_t)stackItem->m_offsetExtraBits, stackItem->m_numOffsetExtraBits));
				}

				if (!asmState->m_matchLengthEncTable.m_pstate->m_isRLE)
				{
					ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, matchLengthState, asmState->m_matchLengthEncTable.m_pstate->m_table.m_accuracyLog));
				}

				if (!asmState->m_offsetEncTable.m_pstate->m_isRLE)
				{
					ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, offsetState, asmState->m_offsetEncTable.m_pstate->m_table.m_accuracyLog));
				}

				if (!asmState->m_litLengthEncTable.m_pstate->m_isRLE)
				{
					ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, litLengthState, asmState->m_litLengthEncTable.m_pstate->m_table.m_accuracyLog));
				}
			}

			// Flush padding and terminate stream
			ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, 1, 1));
			{
				uint8_t numPaddingBits = 8u - (bitstream.m_numBits % 8u);

				if (numPaddingBits != 8u)
				{
					ZSTDHL_CHECKED(zstdhl_WriteLEStreamBits(&bitstream, 0, numPaddingBits));
				}
			}
		} // if (numSequences > 0)

		ZSTDHL_CHECKED(zstdhl_FlushLEStreamBytes(&bitstream, bitstream.m_numBits / 8u));
	}

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_AssembleBlockImpl(zstdhl_AsmState_t *asmState, const zstdhl_EncBlockDesc_t *encBlock)
{
	ZSTDHL_CHECKED(zstdhl_AssembleLiteralsSection(asmState, encBlock));
	ZSTDHL_CHECKED(zstdhl_AssembleSequencesSection(asmState, encBlock));

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_AssembleAndWriteBlock(zstdhl_AsmState_t *asmState, const zstdhl_EncBlockDesc_t *encBlock, const zstdhl_EncoderOutputObject_t *assemblyOutput)
{
	uint8_t blockHeaderBytes[3];
	uint32_t blockHeader = 0;
	size_t blockSize = 0;
	const void *blockContentData = NULL;
	size_t blockContentSize = 0;

	switch (encBlock->m_blockHeader.m_blockType)
	{
	case ZSTDHL_BLOCK_TYPE_RLE:
		blockSize = encBlock->m_blockHeader.m_blockSize;
		blockContentData = encBlock->m_uncompressedOrRLEData;
		blockContentSize = 1;
		break;
	case ZSTDHL_BLOCK_TYPE_RAW:
		blockSize = encBlock->m_blockHeader.m_blockSize;
		blockContentData = encBlock->m_uncompressedOrRLEData;
		blockContentSize = blockSize;
		break;
	case ZSTDHL_BLOCK_TYPE_COMPRESSED:
		ZSTDHL_CHECKED(zstdhl_AssembleBlockImpl(asmState, encBlock));
		blockSize = encBlock->m_blockHeader.m_blockSize;
		blockContentData = asmState->m_dataBlockVector.m_data;
		blockContentSize = asmState->m_dataBlockVector.m_count;

		if (encBlock->m_autoBlockSizeFlag)
			blockSize = blockContentSize;

		break;
	default:
		return ZSTDHL_RESULT_BLOCK_TYPE_INVALID;
	}

	if (encBlock->m_blockHeader.m_isLastBlock)
		blockHeader |= 1;

	blockHeader |= (encBlock->m_blockHeader.m_blockType << 1);

	if (blockSize >= (1 << 20))
		return ZSTDHL_RESULT_BLOCK_SIZE_INVALID;

	blockHeader |= ((uint32_t)(blockSize << 3));

	blockHeaderBytes[0] = (uint8_t)((blockHeader >> 0) & 0xff);
	blockHeaderBytes[1] = (uint8_t)((blockHeader >> 8) & 0xff);
	blockHeaderBytes[2] = (uint8_t)((blockHeader >> 16) & 0xff);

	ZSTDHL_CHECKED(assemblyOutput->m_writeBitstreamFunc(assemblyOutput->m_userdata, blockHeaderBytes, 3));
	ZSTDHL_CHECKED(assemblyOutput->m_writeBitstreamFunc(assemblyOutput->m_userdata, blockContentData, blockContentSize));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_AssembleBlock(zstdhl_AssemblerPersistentState_t *persistentState, const zstdhl_EncBlockDesc_t *encBlock, const zstdhl_EncoderOutputObject_t *assemblyOutput, const zstdhl_MemoryAllocatorObject_t *alloc)
{
	zstdhl_ResultCode_t resultCode = ZSTDHL_RESULT_OK;
	zstdhl_AsmState_t asmState;

	zstdhl_AsmState_Init(&asmState, persistentState, alloc);

	resultCode = zstdhl_AssembleAndWriteBlock(&asmState, encBlock, assemblyOutput);

	zstdhl_AsmState_Destroy(&asmState);

	return resultCode;
}
