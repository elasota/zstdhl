/*
Copyright (c) 2023 Eric Lasota

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include "zstdhl.h"


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

#define ZSTDHL_HUFFMAN_CODE_LENGTH_LIMIT	11

#define ZSTDHL_CHECKED(n) do {\
	ZSTDHL_DECL(zstdhl_ResultCode_t) result = (n);\
	if (result != ZSTDHL_RESULT_OK)\
		return result;\
	} while(0)

#define ZSTDHL_LESS_THAN_ONE_VALUE ((uint32_t)0xffffffffu)

static zstdhl_ResultCode_t zstdhl_ReadChecked(const zstdhl_StreamSourceObject_t * streamSource, void *dest, size_t numBytes, zstdhl_ResultCode_t failureResult)
{
	if (streamSource->m_api->m_readBytesFunc(streamSource->m_userdata, dest, numBytes) != numBytes)
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

	buffers->m_alloc.m_api = alloc->m_api;
	buffers->m_alloc.m_userdata = alloc->m_userdata;
}

zstdhl_ResultCode_t zstdhl_Buffers_Alloc(zstdhl_Buffers_t *buffers, int bufferID, size_t size, void **outPtr)
{
	void *ptr = NULL;

	if (buffers->m_buffers[bufferID])
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	ptr = buffers->m_alloc.m_api->m_allocFunc(buffers->m_alloc.m_userdata, size);
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
		buffers->m_alloc.m_api->m_freeFunc(buffers->m_alloc.m_userdata, buffers->m_buffers[bufferID]);
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

int zstdhl_IsPowerOf2(uint32_t value)
{
	if (value & (value - 1u))
		return 0;

	return 1;
}

typedef struct zstdhl_ForwardBitstream
{
	zstdhl_StreamSourceObject_t m_streamSource;
	uint32_t m_bits;
	uint8_t m_numBits;
} zstdhl_ForwardBitstream_t;

static zstdhl_ResultCode_t zstdhl_ForwardBitstream_Init(zstdhl_ForwardBitstream_t *bitstream, const zstdhl_StreamSourceObject_t *streamSource)
{
	bitstream->m_streamSource.m_api = streamSource->m_api;
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

	numBytes = streamSource->m_streamSource.m_api->m_readBytesFunc(streamSource->m_streamSource.m_userdata, dest, numBytes);
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
		slice->m_sizeRemaining -= bytesRead;

		if (bytesRead < bytesToRead)
			return failureResult;
	}

	return ZSTDHL_RESULT_OK;
}

static zstdhl_StreamSourceAPI_t zstdhl_SliceStreamSourceAPI =
{
	zstdhl_SliceStreamSource_ReadBytes
};

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
	uint8_t readOffset = 0);
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
		outFrameHeader->m_windowSize = outFrameHeader->m_frameContentSize;

	outFrameHeader->m_haveContentChecksum = contentChecksumFlag;

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_ParseRLEBlock(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint32_t blockSize)
{
#ifndef ZSTDHL_ALLOW_DECL_AFTER_STATEMENT
	zstdhl_ResultCode_t result = ZSTDHL_RESULT_OK;
#endif

	uint8_t repeatedByte;

	ZSTDHL_CHECKED(zstdhl_ReadChecked(streamSource, &repeatedByte, 1, ZSTDHL_RESULT_BLOCK_TRUNCATED));

	disassemblyOutput->m_api->m_reportBlockRLEDataFunc(disassemblyOutput->m_userdata, repeatedByte, 1);

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_ParseRawBlock(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint32_t blockSize)
{
#ifndef ZSTDHL_ALLOW_DECL_AFTER_STATEMENT
	zstdhl_ResultCode_t result = ZSTDHL_RESULT_OK;
	uint32_t sizeToRead = 0;
#endif

	uint8_t bytes[1024];

	while (blockSize >= sizeof(bytes))
	{
		ZSTDHL_DECL(uint32_t) sizeToRead = sizeof(bytes);
		if (sizeToRead > blockSize)
			sizeToRead = blockSize;

		ZSTDHL_CHECKED(zstdhl_ReadChecked(streamSource, bytes, sizeToRead, ZSTDHL_RESULT_BLOCK_TRUNCATED));
		disassemblyOutput->m_api->m_reportBlockUncompressedDataFunc(disassemblyOutput->m_userdata, bytes, sizeToRead);
	}

	return ZSTDHL_RESULT_OK;
}


zstdhl_ResultCode_t zstdhl_ParseRawLiteralsSection(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint32_t regeneratedSize)
{
	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}

zstdhl_ResultCode_t zstdhl_ParseRLELiteralsSection(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint32_t regeneratedSize)
{
	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}

zstdhl_ResultCode_t zstdhl_DecodeFSEDescription(zstdhl_ForwardBitstream_t *bitstream, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint8_t maxAccuracyLog, int *outAccuracyLog, uint32_t *probs, int maxProbs)
{
	uint32_t bits = 0;
	int accuracyLog = 0;
	int numProbsDecoded = 0;

	ZSTDHL_CHECKED(zstdhl_ForwardBitstream_ReadBits(bitstream, 4, &bits));
	accuracyLog = (int)bits + 5;

	if (accuracyLog > maxAccuracyLog)
		return ZSTDHL_RESULT_ACCURACY_LOG_TOO_LARGE;

	ZSTDHL_DECL(uint32_t) targetTotalProbs = (1 << accuracyLog);

	ZSTDHL_DECL(uint32_t) cumulativeProb = 0;

	ZSTDHL_CHECKED(disassemblyOutput->m_api->m_reportFSETableStartFunc(disassemblyOutput->m_userdata));

	do
	{
		uint32_t maxProbValue = targetTotalProbs - cumulativeProb + 1;

		uint8_t minProbBits = zstdhl_Log2_16(maxProbValue);
		uint32_t largeProbRange = maxProbValue - (1 << minProbBits) + 1;
		uint32_t largeProbStart = (1 << minProbBits) - largeProbRange;

		uint32_t probValue = 0;

		if (maxProbs == 0)
			return ZSTDHL_RESULT_TOO_MANY_PROBS;

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
			*probs = ZSTDHL_LESS_THAN_ONE_VALUE;
			cumulativeProb++;

			maxProbs--;
			probs++;

			ZSTDHL_CHECKED(disassemblyOutput->m_api->m_reportFSEProbabilityFunc(disassemblyOutput->m_userdata, ZSTDHL_LESS_THAN_ONE_VALUE, 0));
		}
		else
		{
			uint32_t prob = probValue - 1;

			if (prob > 0)
			{
				*probs = prob;

				maxProbs--;
				probs++;

				cumulativeProb += prob;

				ZSTDHL_CHECKED(disassemblyOutput->m_api->m_reportFSEProbabilityFunc(disassemblyOutput->m_userdata, prob, 0));
			}
			else
			{
				int numZeroProbs = 1;

				for (;;)
				{
					uint32_t repeatBits = 0;

					ZSTDHL_CHECKED(zstdhl_ForwardBitstream_ReadBits(bitstream, 2, &repeatBits));

					numZeroProbs += (int)repeatBits;

					if (numZeroProbs > maxProbs)
						*probs = ZSTDHL_RESULT_TOO_MANY_PROBS;

					if (repeatBits < 3)
						break;
				}

				ZSTDHL_CHECKED(disassemblyOutput->m_api->m_reportFSEProbabilityFunc(disassemblyOutput->m_userdata, 0, numZeroProbs - 1));

				while (numZeroProbs > 0)
				{
					numZeroProbs--;
					*probs = 0;
					probs++;
					maxProbs--;
				}
			}
		}
	} while (cumulativeProb < targetTotalProbs);

	while (maxProbs > 0)
	{
		*probs = 0;
		probs++;
		maxProbs--;
	}

	ZSTDHL_DECL(uint32_t) wasteBits = 0;

	if (bitstream->m_numBits)
	{
		ZSTDHL_CHECKED(zstdhl_ForwardBitstream_ReadBits(bitstream, bitstream->m_numBits, &wasteBits));
		ZSTDHL_CHECKED(disassemblyOutput->m_api->m_reportWasteBitsFunc(disassemblyOutput->m_userdata, wasteBits));
	}

	ZSTDHL_CHECKED(disassemblyOutput->m_api->m_reportFSETableEndFunc(disassemblyOutput->m_userdata));

	*outAccuracyLog = accuracyLog;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_ParseFSEStream(zstdhl_ReverseBitstream_t *bitstream, const zstdhl_FSETable_t *fseTable, uint8_t numStates, uint8_t *outBuffer, uint32_t outCapacity, uint32_t *outNumBytesRead)
{
	uint16_t states[2];
	uint8_t activeState = 0;
	uint32_t numBytesRead = 0;

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

		uint8_t sym = cell->m_sym;

		if (numBytesRead == outCapacity)
			return ZSTDHL_RESULT_FSE_OUTPUT_CAPACITY_EXCEEDED;

		outBuffer[numBytesRead++] = sym;

		ZSTDHL_DECL(uint32_t) refillBits = 0;
		ZSTDHL_DECL(zstdhl_ResultCode_t) moreBitsResult = zstdhl_ReverseBitstream_ReadBitsComplete(bitstream, cell->m_numBits, &refillBits);
		
		if (moreBitsResult == ZSTDHL_RESULT_REVERSE_BITSTREAM_TRUNCATED)
		{
			uint8_t statesToFlush = numStates - 1;

			while (statesToFlush > 0)
			{
				if (numBytesRead == outCapacity)
					return ZSTDHL_RESULT_FSE_OUTPUT_CAPACITY_EXCEEDED;

				activeState++;
				if (activeState == numStates)
					activeState = 0;

				outBuffer[numBytesRead++] = fseTable->m_cells[states[activeState]].m_sym;

				statesToFlush--;
			}

			break;
		}
		else if (moreBitsResult == ZSTDHL_RESULT_OK)
		{
			states[activeState] = cell->m_baseline + refillBits;

			if (states[activeState] > fseTable->m_numCells)
			{
				int n = 0;
			}

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

		if (weight > ZSTDHL_HUFFMAN_CODE_LENGTH_LIMIT)
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
	ZSTDHL_DECL(int) accuracyLog = 0;

	ZSTDHL_DECL(zstdhl_SliceStreamSource_t) sliceSource;
	sliceSource.m_sizeRemaining = weightsCompressedSize;
	sliceSource.m_streamSource.m_api = streamSource->m_api;
	sliceSource.m_streamSource.m_userdata = streamSource->m_userdata;

	ZSTDHL_DECL(zstdhl_StreamSourceObject_t) sliceSourceObj;
	sliceSourceObj.m_userdata = &sliceSource;
	sliceSourceObj.m_api = &zstdhl_SliceStreamSourceAPI;

	ZSTDHL_DECL(zstdhl_ForwardBitstream_t) bitstream;

	ZSTDHL_CHECKED(zstdhl_ForwardBitstream_Init(&bitstream, &sliceSourceObj));

	ZSTDHL_CHECKED(zstdhl_DecodeFSEDescription(&bitstream, disassemblyOutput, 6, &accuracyLog, huffTreeDesc->m_weightTableProbabilities, 256));

	huffTreeDesc->m_huffmanWeightFormat = ZSTDHL_HUFFMAN_WEIGHT_ENCODING_FSE;
	huffTreeDesc->m_weightTable.m_accuracyLog = accuracyLog;
	huffTreeDesc->m_weightTable.m_probabilities = huffTreeDesc->m_weightTableProbabilities;
	huffTreeDesc->m_weightTable.m_numProbabilities = 256;

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

zstdhl_ResultCode_t zstdhl_ParseDirectHuffmanWeights(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_HuffmanTreeDesc_t *treeDesc, uint8_t numWeights)
{
	treeDesc->m_huffmanWeightFormat = ZSTDHL_HUFFMAN_WEIGHT_ENCODING_UNCOMPRESSED;

	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}

zstdhl_ResultCode_t zstdhl_ParseHuffmanTreeDescription(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers, zstdhl_HuffmanTreeDesc_t *treeDesc)
{
	uint8_t headerByte = 0;

	ZSTDHL_CHECKED(zstdhl_ReadChecked(streamSource, &headerByte, 1, ZSTDHL_RESULT_HUFFMAN_TREE_DESC_TRUNCATED));
	
	ZSTDHL_DECL(uint8_t) maxNumberOfBits = 0;

	if (headerByte < 128)
	{
		ZSTDHL_CHECKED(zstdhl_ParseFSEHuffmanWeights(streamSource, disassemblyOutput, treeDesc, buffers, headerByte));
	}
	else
	{
		ZSTDHL_CHECKED(zstdhl_ParseDirectHuffmanWeights(streamSource, disassemblyOutput, treeDesc, headerByte - 128));
	}

	ZSTDHL_CHECKED(disassemblyOutput->m_api->m_reportHuffmanTableDescFunc(disassemblyOutput->m_userdata, treeDesc));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_GenerateHuffmanDecodeTable(const zstdhl_HuffmanTreeDesc_t *treeDesc, zstdhl_HuffmanTableDec_t *decTable)
{
	uint32_t weightIterator = 0;
	uint32_t i = 0;
	uint8_t maxBits = 0;
	uint8_t upshiftBits = 0;
	zstdhl_HuffmanTreeWeightDesc_t weightDesc;

	ZSTDHL_CHECKED(zstdhl_ExpandHuffmanWeightTable(&treeDesc->m_partialWeightDesc, &weightDesc));

	for (i = 0; i < 256; i++)
	{
		uint8_t weight = weightDesc.m_weights[i];

		if (weight > 0)
			weightIterator += (1u << (weight - 1));
	}

	maxBits = zstdhl_Log2_32(weightIterator);

	weightIterator = 0;

	if (maxBits > ZSTDHL_HUFFMAN_CODE_LENGTH_LIMIT)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	for (i = 0; i < 2048; i++)
	{
		decTable->m_dec[i].m_numBits = 0;
		decTable->m_dec[i].m_symbol = 0;
	}

	decTable->m_maxBits = maxBits;

	weightIterator = 0;
	for (i = 0; i <= ZSTDHL_HUFFMAN_CODE_LENGTH_LIMIT; i++)
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

		streamSize -= 6;
		huffmanBytes += 6;

		substreamTotal = substreamSizes[0] + substreamSizes[1] + substreamSizes[2];

		if (substreamTotal > streamSize)
			return ZSTDHL_RESULT_JUMP_TABLE_INVALID;

		substreamSizes[3] = streamSize - substreamTotal;

		ZSTDHL_CHECKED(zstdhl_DecodeHuffmanStream4(huffmanBytes, (uint8_t *)literalsPtr, substreamSizes, regeneratedSize, decTable));
	}
	else
	{
		ZSTDHL_CHECKED(zstdhl_DecodeHuffmanStream1(huffmanBytes, (uint8_t *)literalsPtr, streamSize, regeneratedSize, decTable));
	}

	zstdhl_Buffers_Dealloc(buffers, ZSTDHL_BUFFER_HUFFMAN_BITSTREAM);

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_ParseHuffmanLiteralsSection(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers, uint32_t compressedSize, uint32_t regeneratedSize, int haveNewTree, int is4Stream, zstdhl_FramePersistentState_t *pstate)
{
	zstdhl_SliceStreamSource_t sliceSource;
	sliceSource.m_streamSource.m_api = streamSource->m_api;
	sliceSource.m_streamSource.m_userdata = streamSource->m_userdata;
	sliceSource.m_sizeRemaining = compressedSize;

	ZSTDHL_DECL(zstdhl_StreamSourceObject_t) sliceSourceObj;
	sliceSourceObj.m_api = &zstdhl_SliceStreamSourceAPI;
	sliceSourceObj.m_userdata = &sliceSource;

	if (haveNewTree)
	{
		zstdhl_HuffmanTreeDesc_t treeDesc;
		ZSTDHL_CHECKED(zstdhl_ParseHuffmanTreeDescription(&sliceSourceObj, disassemblyOutput, buffers, &treeDesc));
		ZSTDHL_CHECKED(zstdhl_GenerateHuffmanDecodeTable(&treeDesc, &pstate->m_huffmanTable));
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

	ZSTDHL_DECL(uint32_t) blockSize = *inOutBlockSize;

	sliceSource.m_streamSource.m_api = streamSource->m_api;
	sliceSource.m_streamSource.m_userdata = streamSource->m_userdata;
	sliceSource.m_sizeRemaining = blockSize;

	sliceSourceObj.m_api = &zstdhl_SliceStreamSourceAPI;
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

typedef struct zstdhl_SubstreamCompressionStructureDef
{
	uint8_t m_maxAccuracyLog;
	uint8_t m_defaultAccuracyLog;
	uint8_t m_numProbs;
	const uint32_t *m_defaultProbs;
} zstdhl_SubstreamCompressionStructureDef_t;

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

zstdhl_ResultCode_t zstdhl_ParseCompressionDef(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint8_t defByte, int defBitOffset, const zstdhl_SubstreamCompressionStructureDef_t *sdef, zstdhl_SequencesSubstreamCompressionDef_t *cdef, uint32_t *dynamicProbs)
{
	zstdhl_SequencesCompressionMode_t compressionMode = (zstdhl_SequencesCompressionMode_t)((defByte >> defBitOffset) & 3);
	uint32_t i = 0;

	switch (compressionMode)
	{
	case ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED:
		{
			cdef->m_isDefined = 1;
			cdef->m_fseTableDef.m_accuracyLog = sdef->m_defaultAccuracyLog;
			cdef->m_fseTableDef.m_numProbabilities = sdef->m_numProbs;
			cdef->m_fseTableDef.m_probabilities = sdef->m_defaultProbs;
		}
		return ZSTDHL_RESULT_OK;

	case ZSTDHL_SEQ_COMPRESSION_MODE_RLE:
		{
			uint32_t i = 0;
			uint8_t rleByte = 0;

			ZSTDHL_CHECKED(zstdhl_ReadChecked(streamSource, &rleByte, 1, ZSTDHL_RESULT_SEQUENCE_COMPRESSION_DEF_TRUNCATED));

			if (rleByte >= sdef->m_numProbs)
				return ZSTDHL_RESULT_SEQUENCE_RLE_SYMBOL_INVALID;

			cdef->m_isDefined = 1;
			cdef->m_fseTableDef.m_accuracyLog = 0;
			cdef->m_fseTableDef.m_numProbabilities = 1;
			cdef->m_fseTableDef.m_probabilities = dynamicProbs;

			for (i = 0; i < sdef->m_numProbs; i++)
				dynamicProbs[i] = 0;

			dynamicProbs[rleByte] = 1;
		}
		return ZSTDHL_RESULT_OK;

	case ZSTDHL_SEQ_COMPRESSION_MODE_FSE:
		{
			int accuracyLog = 0;

			zstdhl_ForwardBitstream_t bitstream;
			zstdhl_ForwardBitstream_Init(&bitstream, streamSource);

			ZSTDHL_CHECKED(zstdhl_DecodeFSEDescription(&bitstream, disassemblyOutput, sdef->m_maxAccuracyLog, &accuracyLog, dynamicProbs, sdef->m_numProbs));

			cdef->m_isDefined = 1;
			cdef->m_fseTableDef.m_accuracyLog = accuracyLog;
			cdef->m_fseTableDef.m_numProbabilities = sdef->m_numProbs;
			cdef->m_fseTableDef.m_probabilities = dynamicProbs;
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
	zstdhl_FSESymbolTemp_t symbolTemps[53];

	zstdhl_Buffers_Dealloc(buffers, bufferID);
	ZSTDHL_CHECKED(zstdhl_Buffers_Alloc(buffers, bufferID, sizeof(zstdhl_FSETableCell_t) << tableDef->m_accuracyLog, &cellPtr));

	table->m_cells = (zstdhl_FSETableCell_t *)cellPtr;

	ZSTDHL_CHECKED(zstdhl_BuildFSEDistributionTable_ZStd(table, tableDef, symbolTemps));

	ZSTDHL_CHECKED(zstdhl_ReverseBitstream_ReadBitsComplete(bitstream, table->m_accuracyLog, outInitialState));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DecodeSequences(zstdhl_ReverseBitstream_t *bitstream, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers, const zstdhl_FSETableDef_t *litLengthTableDef, const zstdhl_FSETableDef_t *offsetTableDef, const zstdhl_FSETableDef_t *matchLengthTableDef, uint32_t numSequences)
{
	uint32_t litLengthState = 0;
	uint32_t offsetState = 0;
	uint32_t matchLengthState = 0;
	zstdhl_FSETable_t litLengthTable;
	zstdhl_FSETable_t matchLengthTable;
	zstdhl_FSETable_t offsetTable;

	ZSTDHL_CHECKED(zstdhl_InitSequenceDecoding(bitstream, disassemblyOutput, buffers, litLengthTableDef, ZSTDHL_BUFFER_LIT_LENGTH_FSE_TABLE, &litLengthTable, &litLengthState));
	ZSTDHL_CHECKED(zstdhl_InitSequenceDecoding(bitstream, disassemblyOutput, buffers, offsetTableDef, ZSTDHL_BUFFER_OFFSET_FSE_TABLE, &offsetTable, &offsetState));
	ZSTDHL_CHECKED(zstdhl_InitSequenceDecoding(bitstream, disassemblyOutput, buffers, matchLengthTableDef, ZSTDHL_BUFFER_MATCH_LENGTH_FSE_TABLE, &matchLengthTable, &matchLengthState));

	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}

zstdhl_ResultCode_t zstdhl_ParseSequencesSection(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers, uint32_t blockSize, zstdhl_FramePersistentState_t *pstate)
{
	zstdhl_SliceStreamSource_t slice;
	zstdhl_StreamSourceObject_t sliceStream;
	uint8_t headerByte = 0;
	uint32_t numSequences = 0;
	uint32_t i = 0;
	void *sequencesBufferPtr = NULL;

	slice.m_streamSource.m_api = streamSource->m_api;
	slice.m_streamSource.m_userdata = streamSource->m_userdata;
	slice.m_sizeRemaining = blockSize;

	sliceStream.m_api = &zstdhl_SliceStreamSourceAPI;
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
		return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;

	ZSTDHL_CHECKED(zstdhl_ReadChecked(&sliceStream, &headerByte, 1, ZSTDHL_RESULT_SEQUENCES_HEADER_TRUNCATED));

	if (headerByte & 3)
		return ZSTDHL_RESULT_SEQUENCES_COMPRESSION_MODE_RESERVED_BITS_INVALID;

	ZSTDHL_CHECKED(zstdhl_ParseCompressionDef(&sliceStream, disassemblyOutput, headerByte, 6, &zstdhl_litLenSDef, &pstate->m_literalLengthsCDef, pstate->m_litLengthProbs));
	ZSTDHL_CHECKED(zstdhl_ParseCompressionDef(&sliceStream, disassemblyOutput, headerByte, 4, &zstdhl_offsetCodeSDef, &pstate->m_offsetsCDef, pstate->m_offsetProbs));
	ZSTDHL_CHECKED(zstdhl_ParseCompressionDef(&sliceStream, disassemblyOutput, headerByte, 2, &zstdhl_matchLenSDef, &pstate->m_matchLengthsCDef, pstate->m_matchLengthProbs));

	{
		uint32_t bitstreamSize = (uint32_t)slice.m_sizeRemaining;
		zstdhl_ReverseBitstream_t revStream;

		ZSTDHL_CHECKED(zstdhl_Buffers_Alloc(buffers, ZSTDHL_BUFFER_FSE_BITSTREAM, bitstreamSize, &sequencesBufferPtr));

		ZSTDHL_CHECKED(zstdhl_ReadChecked(&sliceStream, sequencesBufferPtr, bitstreamSize, ZSTDHL_RESULT_SEQUENCE_BITSTREAM_TOO_SMALL));

		ZSTDHL_CHECKED(zstdhl_ReverseBitstream_Init(&revStream, (const uint8_t *)sequencesBufferPtr, bitstreamSize));

		ZSTDHL_CHECKED(zstdhl_DecodeSequences(&revStream, disassemblyOutput, buffers, &pstate->m_literalLengthsCDef.m_fseTableDef, &pstate->m_offsetsCDef.m_fseTableDef, &pstate->m_matchLengthsCDef.m_fseTableDef, numSequences));

		zstdhl_Buffers_Dealloc(buffers, ZSTDHL_BUFFER_FSE_BITSTREAM);
	}

	return ZSTDHL_RESULT_OK;

}

static zstdhl_ResultCode_t zstdhl_ParseCompressedBlock(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers, uint32_t blockSize, zstdhl_FramePersistentState_t *pstate)
{
#ifndef ZSTDHL_ALLOW_DECL_AFTER_STATEMENT
	zstdhl_ResultCode_t result = ZSTDHL_RESULT_OK;
#endif

	ZSTDHL_CHECKED(zstdhl_ParseLiteralsSection(streamSource, disassemblyOutput, buffers, &blockSize, pstate));
	ZSTDHL_CHECKED(zstdhl_ParseSequencesSection(streamSource, disassemblyOutput, buffers, blockSize, pstate));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DisassembleImpl(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers, zstdhl_FramePersistentState_t *pstate)
{
#ifndef ZSTDHL_ALLOW_DECL_AFTER_STATEMENT
	zstdhl_FrameHeaderDesc_t frameHeader;
	zstdhl_ResultCode_t result = ZSTDHL_RESULT_OK;
	uint8_t blockHeaderBytes[3];
	zstdhl_BlockHeaderDesc_t blockHeader;
	zstdhl_FrameHeaderDesc_t frameHeader;
#endif

	ZSTDHL_DECL(zstdhl_FrameHeaderDesc_t frameHeader);
	ZSTDHL_CHECKED(zstdhl_ParseFrameHeader(streamSource, &frameHeader));

	disassemblyOutput->m_api->m_reportFrameHeaderFunc(disassemblyOutput->m_userdata, &frameHeader);

	for (;;)
	{
		ZSTDHL_DECL(uint8_t blockHeaderBytes[3]);

		ZSTDHL_CHECKED(zstdhl_ReadChecked(streamSource, blockHeaderBytes, 3, ZSTDHL_RESULT_BLOCK_HEADER_TRUNCATED));

		ZSTDHL_DECL(zstdhl_BlockHeaderDesc_t) blockHeader;

		blockHeader.m_isLastBlock = (blockHeaderBytes[0] & 1);
		blockHeader.m_blockType = (zstdhl_BlockType_t)((blockHeaderBytes[0] >> 1) & 3);
		blockHeader.m_blockSize = ((blockHeaderBytes[0] >> 3) & 0x1f);
		blockHeader.m_blockSize |= (blockHeaderBytes[1] << 5);
		blockHeader.m_blockSize |= (blockHeaderBytes[2] << 13);

		if (blockHeader.m_blockType == ZSTDHL_BLOCK_TYPE_INVALID)
			return ZSTDHL_RESULT_BLOCK_TYPE_INVALID;

		disassemblyOutput->m_api->m_reportBlockHeaderFunc(disassemblyOutput->m_userdata, &blockHeader);

		switch (blockHeader.m_blockType)
		{
		case ZSTDHL_BLOCK_TYPE_RLE:
			ZSTDHL_CHECKED(zstdhl_ParseRLEBlock(streamSource, disassemblyOutput, blockHeader.m_blockSize));
			break;
		case ZSTDHL_BLOCK_TYPE_RAW:
			ZSTDHL_CHECKED(zstdhl_ParseRawBlock(streamSource, disassemblyOutput, blockHeader.m_blockSize));
			break;
		case ZSTDHL_BLOCK_TYPE_COMPRESSED:
			ZSTDHL_CHECKED(zstdhl_ParseCompressedBlock(streamSource, disassemblyOutput, buffers, blockHeader.m_blockSize, pstate));
			break;
		default:
			// Shouldn't get here
			return ZSTDHL_RESULT_INTERNAL_ERROR;
		}

		break;
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_BuildFSEDistributionTable_ZStd(zstdhl_FSETable_t *fseTable, const zstdhl_FSETableDef_t *fseTableDef, zstdhl_FSESymbolTemp_t *symbolTemps)
{
	uint8_t accuracyLog = fseTableDef->m_accuracyLog;
	uint32_t numCells = (1 << accuracyLog);
	zstdhl_FSETableCell_t *cells = fseTable->m_cells;
	uint32_t numNotLowProbCells = numCells;
	uint32_t i = 0;
	uint16_t numProbs = fseTableDef->m_numProbabilities;
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
		uint8_t symbol = cell->m_sym;
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

zstdhl_ResultCode_t zstdhl_BuildFSEDistributionTable_RANS(zstdhl_FSETable_t *fseTable, const zstdhl_FSETableDef_t *fseTableDef, zstdhl_FSESymbolTemp_t *symbolTemps)
{
	uint8_t accuracyLog = fseTableDef->m_accuracyLog;
	uint32_t numCells = (1 << accuracyLog);
	zstdhl_FSETableCell_t *cells = fseTable->m_cells;
	uint32_t numNotLowProbCells = numCells;
	uint32_t i = 0;
	uint16_t numProbs = fseTableDef->m_numProbabilities;
	const uint32_t *probs = fseTableDef->m_probabilities;
	uint32_t start = 0;

	uint32_t cellMask = numCells - 1;

	for (i = 0; i < numProbs; i++)
	{
		zstdhl_FSESymbolTemp_t *sym = symbolTemps + i;
		uint32_t effProb = (probs[i] == ZSTDHL_LESS_THAN_ONE_VALUE) ? 1 : probs[i];

		if (effProb > 0)
		{
			sym->m_baseline = start;

			while (effProb > 0)
			{
				cells[start].m_sym = i;
				start++;
				effProb--;
			}
		}
	}

	for (i = 0; i < numCells; i++)
	{
		uint8_t sym = cells[i].m_sym;
		zstdhl_FSESymbolTemp_t *symTemp = symbolTemps + sym;
		uint32_t effProb = (probs[sym] == ZSTDHL_LESS_THAN_ONE_VALUE) ? 1 : probs[sym];

		// Implicit 1 in top bit
		uint32_t evolvedState = effProb + i - symbolTemps[sym].m_baseline;
		uint32_t initialEvolvedState = evolvedState;

		cells[i].m_numBits = 0;

		while ((evolvedState >> accuracyLog) == 0)
		{
			cells[i].m_numBits++;
			evolvedState <<= 1;
		}

		cells[i].m_baseline = (evolvedState & cellMask);
	}

	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}


zstdhl_ResultCode_t zstdhl_BuildFSEDistributionTable_Hybrid(zstdhl_FSETable_t *fseTable, const zstdhl_FSETableDef_t *fseTableDef, zstdhl_FSESymbolTemp_t *symbolTemps)
{
	uint8_t accuracyLog = fseTableDef->m_accuracyLog;
	uint32_t numCells = (1 << accuracyLog);
	zstdhl_FSETableCell_t *cells = fseTable->m_cells;
	uint32_t numNotLowProbCells = numCells;
	uint32_t i = 0;
	uint16_t numProbs = fseTableDef->m_numProbabilities;
	const uint32_t *probs = fseTableDef->m_probabilities;
	uint32_t start = 0;
	uint32_t advanceStep = (numCells >> 1) + (numCells >> 3) + 3;

	uint32_t cellMask = numCells - 1;

	for (i = 0; i < numProbs; i++)
	{
		zstdhl_FSESymbolTemp_t *sym = symbolTemps + i;
		uint32_t effProb = (probs[i] == ZSTDHL_LESS_THAN_ONE_VALUE) ? 1 : probs[i];

		if (effProb > 0)
		{
			sym->m_baseline = start;

			while (effProb > 0)
			{
				cells[(start * advanceStep) & cellMask].m_sym = i;
				start++;
				effProb--;
			}
		}
	}

	for (i = 0; i < numCells; i++)
	{
		uint32_t cellIndex = ((i * advanceStep) & cellMask);
		zstdhl_FSETableCell_t *cell = cells + cellIndex;
		uint8_t sym = cell->m_sym;
		zstdhl_FSESymbolTemp_t *symTemp = symbolTemps + sym;
		uint32_t effProb = (probs[sym] == ZSTDHL_LESS_THAN_ONE_VALUE) ? 1 : probs[sym];

		// Implicit 1 in top bit
		uint32_t evolvedState = effProb + i - symbolTemps[sym].m_baseline;
		uint32_t initialEvolvedState = evolvedState;

		cell->m_numBits = 0;

		while ((evolvedState >> accuracyLog) == 0)
		{
			cell->m_numBits++;
			evolvedState <<= 1;
		}

		cell->m_baseline = (evolvedState & cellMask);
	}

	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
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

