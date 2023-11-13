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

#pragma once

#ifndef __ZSTD_HL_H__
#define __ZSTD_HL_H__

#include <stdint.h>
#include <stddef.h>

typedef enum zstdhl_BlockType
{
	ZSTDHL_BLOCK_TYPE_RAW,
	ZSTDHL_BLOCK_TYPE_RLE,
	ZSTDHL_BLOCK_TYPE_COMPRESSED,

	ZSTDHL_BLOCK_TYPE_INVALID,
} zstdhl_BlockType_t;

typedef enum zstdhl_LiteralsSectionType
{
	ZSTDHL_LITERALS_SECTION_TYPE_RAW,
	ZSTDHL_LITERALS_SECTION_TYPE_RLE,
	ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN,
	ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN_REUSE,
} zstdhl_LiteralsSectionType_t;

typedef enum zstdhl_HuffmanStreamMode
{
	ZSTDHL_HUFFMAN_STREAM_MODE_1_STREAM,
	ZSTDHL_HUFFMAN_STREAM_MODE_4_STREAMS,
} zstdhl_HuffmanStreamMode_t;

typedef enum zstdhl_HuffmanWeightEncoding
{
	ZSTDHL_HUFFMAN_WEIGHT_ENCODING_FSE,
	ZSTDHL_HUFFMAN_WEIGHT_ENCODING_UNCOMPRESSED,
} zstdhl_HuffmanWeightEncoding_t;

typedef enum zstdhl_SequencesCompressionMode
{
	ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED,
	ZSTDHL_SEQ_COMPRESSION_MODE_RLE,
	ZSTDHL_SEQ_COMPRESSION_MODE_FSE,
	ZSTDHL_SEQ_COMPRESSION_MODE_REUSE,

	ZSTDHL_SEQ_COMPRESSION_MODE_INVALID,
} zstdhl_SequencesCompressionMode_t;

enum zstdhl_SeqConstants
{
	ZSTDHL_SEQ_CONST_NUM_MATCH_LENGTH_CODES = 53,
	ZSTDHL_SEQ_CONST_NUM_LITERAL_LENGTH_CODES = 36,
	ZSTDHL_SEQ_CONST_NUM_OFFSET_CODES = 32,
};

typedef enum zstdhl_ResultCode
{
	ZSTDHL_RESULT_OK = 0,

	ZSTDHL_RESULT_FRAME_HEADER_TRUNCATED,
	ZSTDHL_RESULT_MAGIC_NUMBER_MISMATCH,
	ZSTDHL_RESULT_FRAME_HEADER_RESERVED_BIT_WAS_SET,
	ZSTDHL_RESULT_BLOCK_HEADER_TRUNCATED,
	ZSTDHL_RESULT_BLOCK_TYPE_INVALID,
	ZSTDHL_RESULT_BLOCK_TRUNCATED,
	ZSTDHL_RESULT_LITERALS_SECTION_HEADER_TRUNCATED,
	ZSTDHL_RESULT_HUFFMAN_TREE_DESC_TRUNCATED,
	ZSTDHL_RESULT_FORWARD_BITSTREAM_TRUNCATED,
	ZSTDHL_RESULT_ACCURACY_LOG_TOO_LARGE,
	ZSTDHL_RESULT_TOO_MANY_PROBS,
	ZSTDHL_RESULT_FSE_OUTPUT_CAPACITY_EXCEEDED,
	ZSTDHL_RESULT_REVERSE_BITSTREAM_EMPTY,
	ZSTDHL_RESULT_REVERSE_BITSTREAM_MISSING_PAD_BIT,
	ZSTDHL_RESULT_REVERSE_BITSTREAM_TRUNCATED,
	ZSTDHL_RESULT_REVERSE_BITSTREAM_TOO_SMALL,
	ZSTDHL_RESULT_HUFFMAN_CODE_TOO_LONG,
	ZSTDHL_RESULT_HUFFMAN_TABLE_EMPTY,
	ZSTDHL_RESULT_HUFFMAN_TABLE_MISSING_1_WEIGHT,
	ZSTDHL_RESULT_HUFFMAN_TABLE_IMPLICIT_WEIGHT_UNRESOLVABLE,
	ZSTDHL_RESULT_HUFFMAN_TABLE_NOT_SET,
	ZSTDHL_RESULT_JUMP_TABLE_TRUNCATED,
	ZSTDHL_RESULT_JUMP_TABLE_INVALID,
	ZSTDHL_RESULT_HUFFMAN_BITSTREAM_TOO_SMALL,
	ZSTDHL_RESULT_HUFFMAN_4_STREAM_REGENERATED_SIZE_TOO_SMALL,
	ZSTDHL_RESULT_HUFFMAN_STREAM_INCOMPLETELY_CONSUMED,
	ZSTDHL_RESULT_SEQUENCES_HEADER_TRUNCATED,
	ZSTDHL_RESULT_SEQUENCES_COMPRESSION_MODE_RESERVED_BITS_INVALID,
	ZSTDHL_RESULT_SEQUENCE_COMPRESSION_MODE_REUSE_WITHOUT_PRIOR_BLOCK,
	ZSTDHL_RESULT_SEQUENCE_COMPRESSION_DEF_TRUNCATED,
	ZSTDHL_RESULT_SEQUENCE_RLE_SYMBOL_INVALID,
	ZSTDHL_RESULT_SEQUENCE_BITSTREAM_TOO_SMALL,

	ZSTDHL_RESULT_INTERNAL_ERROR,
	ZSTDHL_RESULT_NOT_YET_IMPLEMENTED,
	ZSTDHL_RESULT_OUT_OF_MEMORY,

	ZSTDHL_RESULT_NOT_ENOUGH_BITS,

	ZSTDHL_RESULT_FAIL,
} zstdhl_ResultCode_t;

typedef struct zstdhl_StreamSourceAPI
{
	size_t(*m_readBytesFunc)(void *userdata, void *dest, size_t numBytes);
} zstdhl_StreamSourceAPI_t;

typedef struct zstdhl_StreamSourceObject
{
	const zstdhl_StreamSourceAPI_t *m_api;
	void *m_userdata;
} zstdhl_StreamSourceObject_t;

typedef struct zstdhl_FSETableDef
{
	uint8_t m_accuracyLog;
	const uint32_t *m_probabilities;
	uint16_t m_numProbabilities;
} zstdhl_FSETableDef_t;

typedef struct zstdhl_HuffmanTreeWeightDesc
{
	uint8_t m_weights[256];
} zstdhl_HuffmanTreeWeightDesc_t;

typedef struct zstdhl_HuffmanTreePartialWeightDesc
{
	uint8_t m_specifiedWeights[255];
	uint8_t m_numSpecifiedWeights;
} zstdhl_HuffmanTreePartialWeightDesc_t;

typedef struct zstdhl_HuffmanTreeDesc
{
	zstdhl_HuffmanWeightEncoding_t m_huffmanWeightFormat;

	zstdhl_FSETableDef_t m_weightTable;
	uint32_t m_weightTableProbabilities[256];

	zstdhl_HuffmanTreePartialWeightDesc_t m_partialWeightDesc;
} zstdhl_HuffmanTreeDesc_t;

typedef struct zstdhl_FSETableCell
{
	uint16_t m_baseline;
	uint8_t m_sym;
	uint8_t m_numBits;
} zstdhl_FSETableCell_t;

typedef struct zstdhl_FSETable
{
	zstdhl_FSETableCell_t *m_cells;
	uint32_t m_numCells;
	uint8_t m_accuracyLog;
} zstdhl_FSETable_t;

typedef struct zstdhl_FSESymbolTemp
{
	uint32_t m_baseline;
	uint32_t m_numLargeSteppingRemaining;
	uint8_t m_smallSize;
} zstdhl_FSESymbolTemp_t;

typedef struct zstdhl_HuffmanTreeBranch
{
	uint8_t m_left;
	uint8_t m_right;
	uint8_t m_leftIsLeaf;
	uint8_t m_rightIsLeaf;
} zstdhl_HuffmanTreeBranch_t;

typedef struct zstdhl_HuffmanTreeLeaf
{
	uint8_t m_symbol;
} zstdhl_HuffmanTreeLeaf_t;

typedef struct zstdhl_HuffmanTree
{
	uint16_t m_numLeafs;
	uint8_t m_numBranches;

	zstdhl_HuffmanTreeBranch_t m_nodes[256];
	zstdhl_HuffmanTreeLeaf_t m_leafs[256];
} zstdhl_HuffmanTree_t;

typedef struct zstdhl_HuffmanTableEncEntry
{
	uint16_t m_bits;
	uint8_t m_symbol;
	uint8_t m_numBits;
} zstdhl_HuffmanTableEncEntry_t;

typedef struct zstdhl_HuffmanTableDecEntry
{
	uint8_t m_symbol;
	uint8_t m_numBits;
} zstdhl_HuffmanTableDecEntry_t;

typedef struct zstdhl_HuffmanTableEnc
{
	zstdhl_HuffmanTableEncEntry_t m_entries[256];
} zstdhl_HuffmanTableEnc_t;

typedef struct zstdhl_HuffmanTableDec
{
	zstdhl_HuffmanTableDecEntry_t m_dec[2048];
	uint8_t m_maxBits;
} zstdhl_HuffmanTableDec_t;

typedef struct zstdhl_LiteralsSectionDesc
{
	zstdhl_LiteralsSectionType_t m_sectionType;
	uint32_t m_regeneratedSize;
	uint32_t m_compressedSize;

	zstdhl_HuffmanTreeDesc_t m_huffmanTreeDesc;
	zstdhl_HuffmanStreamMode_t m_huffmanStreamMode;
} zstdhl_LiteralsSectionDesc_t;

typedef struct zstdhl_SequencesSectionDesc
{
	uint32_t m_numSequences;
	zstdhl_SequencesCompressionMode_t m_literalLengthsMode;
	zstdhl_SequencesCompressionMode_t m_offsetsMode;
	zstdhl_SequencesCompressionMode_t m_matchLengthsMode;

	zstdhl_FSETableDef_t m_literalLengthsTable;
	zstdhl_FSETableDef_t m_offsetsTable;
	zstdhl_FSETableDef_t m_matchLengthsTable;

	uint32_t m_probabilities[ZSTDHL_SEQ_CONST_NUM_MATCH_LENGTH_CODES + ZSTDHL_SEQ_CONST_NUM_LITERAL_LENGTH_CODES + ZSTDHL_SEQ_CONST_NUM_OFFSET_CODES];
} zstdhl_SequencesSectionDesc_t;

typedef struct zstdhl_FrameHeaderDesc
{
	uint64_t m_windowSize;
	uint64_t m_frameContentSize;
	uint32_t m_dictionaryID;

	uint8_t m_haveDictionaryID;
	uint8_t m_haveContentChecksum;
	uint8_t m_haveFrameContentSize;
} zstdhl_FrameHeaderDesc_t;

typedef struct zstdhl_BlockHeaderDesc
{
	zstdhl_BlockType_t m_blockType;

	uint8_t m_isLastBlock;
	uint32_t m_blockSize;
} zstdhl_BlockHeaderDesc_t;

typedef struct zstdhl_DisassemblyOutputAPI
{
	zstdhl_ResultCode_t (*m_reportFrameHeaderFunc)(void *userdata, const zstdhl_FrameHeaderDesc_t *frameHeader);
	zstdhl_ResultCode_t (*m_reportBlockHeaderFunc)(void *userdata, const zstdhl_BlockHeaderDesc_t *blockHeader);
	zstdhl_ResultCode_t (*m_reportLiteralsSectionFunc)(void *userdata, const zstdhl_LiteralsSectionDesc_t *litSection);
	zstdhl_ResultCode_t (*m_reportSequencesSectionFunc)(void *userdata, const zstdhl_SequencesSectionDesc_t *seqSection);
	zstdhl_ResultCode_t (*m_reportBlockRLEDataFunc)(void *userdata, uint8_t value, size_t count);
	zstdhl_ResultCode_t (*m_reportBlockUncompressedDataFunc)(void *userdata, const void *data, size_t size);

	zstdhl_ResultCode_t (*m_reportFSETableStartFunc)(void *userdata);
	zstdhl_ResultCode_t (*m_reportFSETableEndFunc)(void *userdata);
	zstdhl_ResultCode_t (*m_reportFSEProbabilityFunc)(void *userdata, uint32_t probability, int numRepeats);

	zstdhl_ResultCode_t (*m_reportWasteBitsFunc)(void *userdata, uint32_t wasteBits);

	zstdhl_ResultCode_t (*m_reportHuffmanTableDescFunc)(void *userdata, zstdhl_HuffmanTreeDesc_t *treeDesc);
} zstdhl_DisassemblyOutputAPI_t;

typedef struct zstdhl_DisassemblyOutputObject
{
	const zstdhl_DisassemblyOutputAPI_t *m_api;
	void *m_userdata;
} zstdhl_DisassemblyOutputObject_t;

typedef struct zstdhl_MemoryAllocatorAPI
{
	void *(*m_allocFunc)(void *userdata, size_t size);
	void (*m_freeFunc)(void *userdata, void *ptr);
} zstdhl_MemoryAllocatorAPI_t;


typedef struct zstdhl_MemoryAllocatorObject
{
	const zstdhl_MemoryAllocatorAPI_t *m_api;
	void *m_userdata;
} zstdhl_MemoryAllocatorObject_t;


#ifdef __cplusplus
extern "C"
{
#endif
zstdhl_ResultCode_t zstdhl_Disassemble(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, const zstdhl_MemoryAllocatorObject_t *alloc);
uint32_t zstdhl_GetLessThanOneConstant(void);
zstdhl_ResultCode_t zstdhl_BuildFSEDistributionTable_ZStd(zstdhl_FSETable_t *fseTable, const zstdhl_FSETableDef_t *fseTableDef, zstdhl_FSESymbolTemp_t *symbolTemps);
zstdhl_ResultCode_t zstdhl_BuildFSEDistributionTable_RANS(zstdhl_FSETable_t *fseTable, const zstdhl_FSETableDef_t *fseTableDef, zstdhl_FSESymbolTemp_t *symbolTemps);

#ifdef __cplusplus
}
#endif

#endif
