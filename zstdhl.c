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

#define ZSTDHL_CHECKED(n) do {\
	ZSTDHL_DECL(zstdhl_ResultCode_t) result = (n);\
	if (result != ZSTDHL_RESULT_OK)\
		return result;\
	} while(0)

#define ZSTDHL_LESS_THAN_ONE_VALUE ((uint16_t)0xffff)

static zstdhl_ResultCode_t zstdhl_ReadChecked(const zstdhl_StreamSourceObject_t * streamSource, void *dest, size_t numBytes, zstdhl_ResultCode_t failureResult)
{
	if (streamSource->m_api->m_readBytesFunc(streamSource->m_userdata, dest, numBytes) != numBytes)
		return failureResult;

	return ZSTDHL_RESULT_OK;
}

enum zstdhl_BufferID
{
	ZSTDHL_BUFFER_FSE_BITSTREAM,

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

	for (int i = 0; i < ZSTDHL_BUFFER_COUNT; i++)
		buffers->m_buffers[i] = NULL;

	buffers->m_alloc.m_api = alloc->m_api;
	buffers->m_alloc.m_userdata = alloc->m_userdata;
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

	result += value;

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

static void zstdhl_BitSwap32(uint32_t *bits)
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
		zstdhl_BitSwap32(&bitstream->m_bits);
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

zstdhl_ResultCode_t zstdhl_DecodeFSEDescription(zstdhl_ForwardBitstream_t *bitstream, uint8_t maxAccuracyLog, int *outAccuracyLog, uint16_t *probs, int maxProbs)
{
	uint32_t bits = 0;
	int accuracyLog = 0;
	int numProbsDecoded = 0;

	ZSTDHL_CHECKED(zstdhl_ForwardBitstream_ReadBits(bitstream, 4, &bits));
	accuracyLog = (int)bits + 5;

	if (accuracyLog > maxAccuracyLog)
		return ZSTDHL_RESULT_ACCURACY_LOG_TOO_LARGE;

	ZSTDHL_DECL(uint16_t) targetTotalProbs = (1 << accuracyLog);

	ZSTDHL_DECL(uint16_t) cumulativeProb = 0;

	do
	{
		if (maxProbs == 0)
			return ZSTDHL_RESULT_TOO_MANY_PROBS;

		uint16_t maxProbValue = targetTotalProbs - cumulativeProb + 1;

		uint8_t minProbBits = zstdhl_Log2_16(maxProbValue) - 1;
		uint16_t largeProbRange = maxProbValue - (1 << minProbBits) + 1;
		uint16_t largeProbStart = (1 << minProbBits) - largeProbRange;

		uint32_t probValue = 0;

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
			*probs = ZSTDHL_RESULT_TOO_MANY_PROBS;
			cumulativeProb++;

			maxProbs--;
			probs++;
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

	*outAccuracyLog = accuracyLog;

	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}

zstdhl_ResultCode_t zstdhl_ParseFSEHuffmanWeights(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint8_t weightsCompressedSize)
{
	uint16_t litProbs[256];

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



	ZSTDHL_CHECKED(zstdhl_DecodeFSEDescription(&bitstream, 6, &accuracyLog, litProbs, 256));

	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}

zstdhl_ResultCode_t zstdhl_ParseDirectHuffmanWeights(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint8_t numWeights)
{
	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}

zstdhl_ResultCode_t zstdhl_ParseHuffmanTreeDescription(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput)
{
	uint8_t headerByte = 0;

	ZSTDHL_CHECKED(zstdhl_ReadChecked(streamSource, &headerByte, 1, ZSTDHL_RESULT_HUFFMAN_TREE_DESC_TRUNCATED));
	
	ZSTDHL_DECL(uint8_t) maxNumberOfBits = 0;

	if (headerByte < 128)
	{
		ZSTDHL_CHECKED(zstdhl_ParseFSEHuffmanWeights(streamSource, disassemblyOutput, headerByte));
	}
	else
	{
		ZSTDHL_CHECKED(zstdhl_ParseDirectHuffmanWeights(streamSource, disassemblyOutput, headerByte - 128));
	}

	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}

zstdhl_ResultCode_t zstdhl_ParseHuffmanLiteralsSection(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint32_t compressedSize, uint32_t regeneratedSize, int haveNewTree, int is4Stream)
{
	if (haveNewTree)
	{
		ZSTDHL_CHECKED(zstdhl_ParseHuffmanTreeDescription(streamSource, disassemblyOutput));
	}

	if (is4Stream)
	{
		// Parse jump table
		return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
	}

	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}

zstdhl_ResultCode_t zstdhl_ParseLiteralsSection(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint32_t *inOutBlockSize)
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

	ZSTDHL_CHECKED(zstdhl_ReadChecked(streamSource, &headerByte, 1, ZSTDHL_RESULT_LITERALS_SECTION_HEADER_TRUNCATED));

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
		ZSTDHL_CHECKED(zstdhl_ParseHuffmanLiteralsSection(&sliceSourceObj, disassemblyOutput, compressedSize, regeneratedSize, (litSectionType == ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN) ? 1 : 0, is4Stream));
		break;
	default:
		return ZSTDHL_RESULT_INTERNAL_ERROR;
	}


	*inOutBlockSize = (uint32_t)sliceSource.m_sizeRemaining;
	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}

zstdhl_ResultCode_t zstdhl_ParseSequencesSection(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, uint32_t blockSize)
{
	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}

static zstdhl_ResultCode_t zstdhl_ParseCompressedBlock(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers, uint32_t blockSize)
{
#ifndef ZSTDHL_ALLOW_DECL_AFTER_STATEMENT
	zstdhl_ResultCode_t result = ZSTDHL_RESULT_OK;
#endif

	ZSTDHL_CHECKED(zstdhl_ParseLiteralsSection(streamSource, disassemblyOutput, &blockSize));
	ZSTDHL_CHECKED(zstdhl_ParseSequencesSection(streamSource, disassemblyOutput, blockSize));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DisassembleImpl(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, zstdhl_Buffers_t *buffers)
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
			ZSTDHL_CHECKED(zstdhl_ParseCompressedBlock(streamSource, disassemblyOutput, buffers, blockHeader.m_blockSize));
			break;
		default:
			// Shouldn't get here
			return ZSTDHL_RESULT_INTERNAL_ERROR;
		}

		break;
	}

	return ZSTDHL_RESULT_OK;
}

ZSTDHL_EXTERN zstdhl_ResultCode_t zstdhl_Disassemble(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, const zstdhl_MemoryAllocatorObject_t *alloc)
{
	zstdhl_ResultCode_t result = ZSTDHL_RESULT_OK;
	zstdhl_Buffers_t buffers;

	zstdhl_Buffers_Init(&buffers, alloc);

	result = zstdhl_DisassembleImpl(streamSource, disassemblyOutput, &buffers);

	zstdhl_Buffers_DeallocAll(&buffers);

	return result;
}

