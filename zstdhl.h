/*
Copyright (c) 2023 Eric Lasota

This software is available under the terms of the MIT license
or the Apache License, Version 2.0.  For more information, see
the included LICENSE.txt file.
*/

#pragma once

#ifndef __ZSTD_HL_H__
#define __ZSTD_HL_H__

#include <stdint.h>
#include <stddef.h>

enum zstdhl_Constants
{
	ZSTDHL_MIN_ACCURACY_LOG = 5,

	ZSTDHL_MAX_HUFFMAN_WEIGHT_ACCURACY_LOG = 6,
	ZSTDHL_MAX_OFFSET_ACCURACY_LOG = 8,
	ZSTDHL_MAX_MATCH_LENGTH_ACCURACY_LOG = 9,
	ZSTDHL_MAX_LIT_LENGTH_ACCURACY_LOG = 9,

	ZSTDHL_MAX_MATCH_LENGTH_CODE = 52,
	ZSTDHL_MAX_LIT_LENGTH_CODE = 35,
	ZSTDHL_MAX_HUFFMAN_WEIGHT = 11,
	ZSTDHL_MAX_HUFFMAN_CODE_LENGTH = 11,
};

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
	ZSTDHL_HUFFMAN_STREAM_MODE_NONE,
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
	ZSTDHL_RESULT_BLOCK_SIZE_INVALID,
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
	ZSTDHL_RESULT_HUFFMAN_TABLE_DAMAGED,
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
	ZSTDHL_RESULT_SEQUENCE_BITSTREAM_INCOMPLETELY_CONSUMED,
	ZSTDHL_RESULT_LITERALS_SECTION_TRUNCATED,
	ZSTDHL_RESULT_FSE_TABLE_INVALID,
	ZSTDHL_RESULT_FSE_TABLE_MISSING_SYMBOL,

	// Assembly errors
	ZSTDHL_RESULT_LITERALS_SECTION_REGENERATED_SIZE_INVALID,
	ZSTDHL_RESULT_LITERALS_SECTION_VALUE_COUNT_MISMATCH,
	ZSTDHL_RESULT_LITERALS_SECTION_TOO_MUCH_DATA_FOR_1_STREAM_MODE,
	ZSTDHL_RESULT_HUFFMAN_STREAM_MODE_INVALID,
	ZSTDHL_RESULT_HUFFMAN_BITSTREAM_TOO_LARGE,
	ZSTDHL_RESULT_HUFFMAN_NOT_ENOUGH_WEIGHTS_FOR_FSE_MODE,
	ZSTDHL_RESULT_HUFFMAN_TOO_MANY_WEIGHTS_FOR_DIRECT_ENCODING,
	ZSTDHL_RESULT_HUFFMAN_TREE_MISSING_VALUE,
	ZSTDHL_RESULT_ACCURACY_LOG_TOO_SMALL,
	ZSTDHL_RESULT_PROBABILITY_TABLE_INVALID,
	ZSTDHL_RESULT_REUSED_TABLE_WITHOUT_EXISTING_TABLE,
	ZSTDHL_RESULT_SYMBOL_DOES_NOT_MATCH_RLE,
	ZSTDHL_RESULT_LITERALS_SECTION_TOO_LARGE,
	ZSTDHL_RESULT_DICTIONARY_MISMATCH,

	// Misc errors
	ZSTDHL_RESULT_INTERNAL_ERROR,
	ZSTDHL_RESULT_NOT_YET_IMPLEMENTED,
	ZSTDHL_RESULT_OUT_OF_MEMORY,
	ZSTDHL_RESULT_INTEGER_OVERFLOW,
	ZSTDHL_RESULT_OFFSET_TOO_LARGE,

	ZSTDHL_RESULT_NOT_ENOUGH_BITS,

	ZSTDHL_RESULT_FAIL,

	ZSTDHL_RESULT_OUTPUT_FAILED,
	ZSTDHL_RESULT_INPUT_FAILED,
	ZSTDHL_RESULT_INVALID_VALUE,

	ZSTDHL_RESULT_SOFT_FAULT,
	ZSTDHL_RESULT_REVERSE_BITSTREAM_TRUNCATED_SOFT_FAULT,
} zstdhl_ResultCode_t;

typedef enum zstdhl_OffsetType
{
	ZSTDHL_OFFSET_TYPE_REPEAT_1,			// Only valid when lit length != 0
	ZSTDHL_OFFSET_TYPE_REPEAT_2,
	ZSTDHL_OFFSET_TYPE_REPEAT_3,

	ZSTDHL_OFFSET_TYPE_REPEAT_1_MINUS_1,	// Only valid when lit length == 0

	ZSTDHL_OFFSET_TYPE_SPECIFIED,
} zstdhl_OffsetType_t;

typedef struct zstdhl_StreamSourceObject
{
	size_t (*m_readBytesFunc)(void *userdata, void *dest, size_t numBytes);
	void *m_userdata;
} zstdhl_StreamSourceObject_t;

typedef struct zstdhl_FSETableDef
{
	uint8_t m_accuracyLog;
	const uint32_t *m_probabilities;
	size_t m_numProbabilities;
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
	size_t m_sym;
	uint16_t m_baseline;
	uint8_t m_numBits;
} zstdhl_FSETableCell_t;

typedef struct zstdhl_FSETable
{
	zstdhl_FSETableCell_t *m_cells;
	uint32_t m_numCells;
	uint8_t m_accuracyLog;
} zstdhl_FSETable_t;

typedef struct zstdhl_FSETableEnc
{
	uint16_t *m_nextStates;	// [(nextSymbol << accuracyLog) + prevState]
} zstdhl_FSETableEnc_t;

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

typedef struct zstdhl_LiteralsSectionHeader
{
	zstdhl_LiteralsSectionType_t m_sectionType;
	uint32_t m_regeneratedSize;
	uint32_t m_compressedSize;
} zstdhl_LiteralsSectionHeader_t;

typedef struct zstdhl_LiteralsSectionDesc
{
	zstdhl_HuffmanStreamMode_t m_huffmanStreamMode;

	uint32_t m_huffmanStreamSizes[4];

	size_t m_numValues;
	zstdhl_StreamSourceObject_t *m_decompressedLiteralsStream;
} zstdhl_LiteralsSectionDesc_t;

typedef struct zstdhl_SequencesSectionDesc
{
	uint32_t m_numSequences;
	zstdhl_SequencesCompressionMode_t m_offsetsMode;
	zstdhl_SequencesCompressionMode_t m_matchLengthsMode;
	zstdhl_SequencesCompressionMode_t m_literalLengthsMode;
} zstdhl_SequencesSectionDesc_t;

typedef struct zstdhl_FrameHeaderDesc
{
	uint64_t m_windowSize;
	uint64_t m_frameContentSize;
	uint32_t m_dictionaryID;

	uint8_t m_haveDictionaryID;
	uint8_t m_haveContentChecksum;
	uint8_t m_haveFrameContentSize;
	uint8_t m_haveWindowSize;
	uint8_t m_isSingleSegment;
} zstdhl_FrameHeaderDesc_t;

typedef struct zstdhl_BlockHeaderDesc
{
	zstdhl_BlockType_t m_blockType;

	uint8_t m_isLastBlock;
	uint32_t m_blockSize;
} zstdhl_BlockHeaderDesc_t;

typedef struct zstdhl_BlockRLEDesc
{
	uint8_t m_value;
	size_t m_count;
} zstdhl_BlockRLEDesc_t;

typedef struct zstdhl_BlockUncompressedDesc
{
	const void *m_data;
	size_t m_size;
} zstdhl_BlockUncompressedDesc_t;

typedef struct zstdhl_ProbabilityDesc
{
	uint32_t m_prob;
	size_t m_repeatCount;
} zstdhl_ProbabilityDesc_t;

typedef struct zstdhl_WasteBitsDesc
{
	uint8_t m_numBits;
	uint8_t m_bits;
} zstdhl_WasteBitsDesc_t;

typedef struct zstdhl_SequenceDesc
{
	uint32_t m_litLength;
	uint32_t m_matchLength;

	uint32_t *m_offsetValueBigNum;
	size_t m_offsetValueNumBits;
	zstdhl_OffsetType_t m_offsetType;
} zstdhl_SequenceDesc_t;

typedef struct zstdhl_FSETableStartDesc
{
	uint8_t m_accuracyLog;
} zstdhl_FSETableStartDesc_t;

typedef struct zstdhl_DictHeaderDesc
{
	uint32_t m_dictID;
} zstdhl_DictHeaderDesc_t;

typedef struct zstdhl_DictRecentOffsets
{
	uint32_t m_offset1;
	uint32_t m_offset2;
	uint32_t m_offset3;
} zstdhl_DictRecentOffsets_t;

typedef struct zstdhl_DictDesc
{
	zstdhl_DictHeaderDesc_t m_dictHeader;

	zstdhl_FSETableDef_t m_matchLengthDesc;
	zstdhl_FSETableDef_t m_litLengthDesc;
	zstdhl_FSETableDef_t m_offsetDesc;
	zstdhl_HuffmanTreeDesc_t m_huffmanTreeDesc;

	zstdhl_DictRecentOffsets_t m_recentOffsets;
} zstdhl_DictDesc_t;


typedef enum zstdhl_ElementType
{
	ZSTDHL_ELEMENT_TYPE_FRAME_HEADER,				// zstdhl_FrameHeaderDesc_t
	ZSTDHL_ELEMENT_TYPE_BLOCK_HEADER,				// zstdhl_BlockHeaderDesc_t
	ZSTDHL_ELEMENT_TYPE_LITERALS_SECTION_HEADER,	// zstdhl_LiteralsSectionHeader_t
	ZSTDHL_ELEMENT_TYPE_LITERALS_SECTION,			// zstdhl_LiteralsSectionDesc_t
	ZSTDHL_ELEMENT_TYPE_SEQUENCES_SECTION,			// zstdhl_SequencesSectionDesc_t
	ZSTDHL_ELEMENT_TYPE_BLOCK_RLE_DATA,				// zstdhl_BlockRLEDesc_t
	ZSTDHL_ELEMENT_TYPE_BLOCK_UNCOMPRESSED_DATA,	// zstdhl_BlockUncompressedDesc_t

	ZSTDHL_ELEMENT_TYPE_FSE_TABLE_START,			// zstdhl_FSETableStartDesc_t
	ZSTDHL_ELEMENT_TYPE_FSE_TABLE_END,				// Nothing
	ZSTDHL_ELEMENT_TYPE_FSE_PROBABILITY,			// zstdhl_ProbabilityDesc_t

	ZSTDHL_ELEMENT_TYPE_SEQUENCE_RLE_BYTE,			// uint8_t

	ZSTDHL_ELEMENT_TYPE_WASTE_BITS,					// zstdhl_WasteBitsDesc_t
	ZSTDHL_ELEMENT_TYPE_HUFFMAN_TREE,				// zstdhl_HuffmanTreeDesc_t

	ZSTDHL_ELEMENT_TYPE_SEQUENCE,					// zstdhl_SequenceDesc_t

	ZSTDHL_ELEMENT_TYPE_BLOCK_END,					// Nothing
	ZSTDHL_ELEMENT_TYPE_FRAME_END,					// Nothing

	ZSTDHL_ELEMENT_TYPE_DICT_START,					// zstdhl_DictHeaderDesc_t
	ZSTDHL_ELEMENT_TYPE_DICT_RECENT_OFFSETS,		// zstdhl_DictRecentOffsets_t
	ZSTDHL_ELEMENT_TYPE_DICT_END,					// Nothing
} zstdhl_ElementType_t;

typedef struct zstdhl_DisassemblyOutputObject
{
	zstdhl_ResultCode_t (*m_reportDisassembledElementFunc)(void *userdata, int elementType, const void *elementData);
	void *m_userdata;
} zstdhl_DisassemblyOutputObject_t;

typedef struct zstdhl_MemoryAllocatorObject
{
	void *(*m_reallocFunc)(void *userdata, void *ptr, size_t newSize);
	void *m_userdata;
} zstdhl_MemoryAllocatorObject_t;

typedef struct zstdhl_Vector
{
	zstdhl_MemoryAllocatorObject_t m_alloc;
	void *m_data;
	void *m_dataEnd;
	size_t m_count;
	size_t m_capacity;
	size_t m_elementSize;
	size_t m_maxCapacity;
	size_t m_capacityBytes;
} zstdhl_Vector_t;

typedef struct zstdhl_FSEEncStack
{
	zstdhl_Vector_t m_statesStackVector;
} zstdhl_FSEEncStack_t;

typedef struct zstdhl_MemBufferStreamSource
{
	const void *m_data;
	size_t m_sizeRemaining;
} zstdhl_MemBufferStreamSource_t;

typedef struct zstdhl_EncSeqCompressionDesc
{
	const zstdhl_FSETableDef_t *m_fseProbs;
	uint8_t m_rleByte;
} zstdhl_EncSeqCompressionDesc_t;

typedef struct zstdhl_SequenceCollectionObject
{
	zstdhl_ResultCode_t (*m_getNextSequence)(void *userdata, zstdhl_SequenceDesc_t *sequence);
	void *m_userdata;
} zstdhl_SequenceCollectionObject_t;

typedef struct zstdhl_EncBlockDesc
{
	zstdhl_BlockHeaderDesc_t m_blockHeader;
	zstdhl_LiteralsSectionHeader_t m_litSectionHeader;
	zstdhl_LiteralsSectionDesc_t m_litSectionDesc;
	zstdhl_SequencesSectionDesc_t m_seqSectionDesc;

	zstdhl_HuffmanTreeDesc_t m_huffmanTreeDesc;

	zstdhl_EncSeqCompressionDesc_t m_literalLengthsCompressionDesc;
	zstdhl_EncSeqCompressionDesc_t m_offsetsModeCompressionDesc;
	zstdhl_EncSeqCompressionDesc_t m_matchLengthsCompressionDesc;

	zstdhl_SequenceCollectionObject_t m_seqCollection;

	uint8_t m_autoBlockSizeFlag;
	uint8_t m_autoLitCompressedSizeFlag;
	uint8_t m_autoLitRegeneratedSizeFlag;
	uint8_t m_autoHuffmanStreamSizesFlags[4];

	const void *m_uncompressedOrRLEData;
} zstdhl_EncBlockDesc_t;

typedef struct zstdhl_AsmPersistentTableState
{
	uint8_t m_isAssigned;
	uint8_t m_isRLE;
	uint8_t m_rleByte;
	zstdhl_FSETable_t m_table;
} zstdhl_AsmPersistentTableState_t;

typedef struct zstdhl_AssemblerPersistentState
{
	zstdhl_HuffmanTreePartialWeightDesc_t m_huffmanTree;
	uint8_t m_haveHuffmanTree;

	zstdhl_FSETableCell_t m_offsetCells[1 << ZSTDHL_MAX_OFFSET_ACCURACY_LOG];
	zstdhl_FSETableCell_t m_matchLengthCells[1 << ZSTDHL_MAX_MATCH_LENGTH_ACCURACY_LOG];
	zstdhl_FSETableCell_t m_litLengthCells[1 << ZSTDHL_MAX_LIT_LENGTH_ACCURACY_LOG];

	zstdhl_AsmPersistentTableState_t m_offsetTable;
	zstdhl_AsmPersistentTableState_t m_matchLengthTable;
	zstdhl_AsmPersistentTableState_t m_litLengthTable;
} zstdhl_AssemblerPersistentState_t;

typedef struct zstdhl_EncoderOutputObject
{
	zstdhl_ResultCode_t (*m_writeBitstreamFunc)(void *userdata, const void *data, size_t size);
	void *m_userdata;
} zstdhl_EncoderOutputObject_t;

typedef struct zstdhl_SubstreamCompressionStructureDef
{
	uint8_t m_maxAccuracyLog;
	uint8_t m_defaultAccuracyLog;
	uint8_t m_numProbs;
	const uint32_t *m_defaultProbs;
} zstdhl_SubstreamCompressionStructureDef_t;

typedef struct zstdhl_DeflateConv_State zstdhl_DeflateConv_State_t;

#ifdef __cplusplus
extern "C"
{
#endif

zstdhl_ResultCode_t zstdhl_DisassembleDict(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, const zstdhl_MemoryAllocatorObject_t *alloc);
zstdhl_ResultCode_t zstdhl_Disassemble(const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_DictDesc_t *dictDesc, const zstdhl_DisassemblyOutputObject_t *disassemblyOutput, const zstdhl_MemoryAllocatorObject_t *alloc);
zstdhl_ResultCode_t zstdhl_InitAssemblerState(zstdhl_AssemblerPersistentState_t *persistentState);
zstdhl_ResultCode_t zstdhl_AssembleFrame(const zstdhl_FrameHeaderDesc_t *encFrame, const zstdhl_EncoderOutputObject_t *assemblyOutput, uint64_t optFrameContentSize);
zstdhl_ResultCode_t zstdhl_AssembleBlock(zstdhl_AssemblerPersistentState_t *persistentState, const zstdhl_EncBlockDesc_t *encBlock, const zstdhl_EncoderOutputObject_t *assemblyOutput, const zstdhl_MemoryAllocatorObject_t *alloc);

uint32_t zstdhl_GetLessThanOneConstant(void);
zstdhl_ResultCode_t zstdhl_BuildFSEDistributionTable_ZStd(zstdhl_FSETable_t *fseTable, const zstdhl_FSETableDef_t *fseTableDef, zstdhl_FSESymbolTemp_t *symbolTemps);
void zstdhl_BuildFSEEncodeTable(zstdhl_FSETableEnc_t *encTable, const zstdhl_FSETable_t *table, size_t numSymbols);
zstdhl_ResultCode_t zstdhl_EncodeFSEValue(zstdhl_FSEEncStack_t *stack, const zstdhl_FSETableEnc_t *encTable, const zstdhl_FSETable_t *table, uint16_t value);

void zstdhl_FSEEncStack_Init(zstdhl_FSEEncStack_t *stack, const zstdhl_MemoryAllocatorObject_t *alloc);
zstdhl_ResultCode_t zstdhl_FSEEncStack_Pop(zstdhl_FSEEncStack_t *stack, uint16_t *outState);
void zstdhl_FSEEncStack_Destroy(zstdhl_FSEEncStack_t *stack);

const zstdhl_SubstreamCompressionStructureDef_t *zstdhl_GetDefaultLitLengthFSEProperties(void);
const zstdhl_SubstreamCompressionStructureDef_t *zstdhl_GetDefaultMatchLengthFSEProperties(void);
const zstdhl_SubstreamCompressionStructureDef_t *zstdhl_GetDefaultOffsetFSEProperties(void);

zstdhl_ResultCode_t zstdhl_EncodeOffsetCode(uint32_t value, uint32_t *outFSEValue, uint32_t *outExtraValue, uint8_t *outExtraBits);
zstdhl_ResultCode_t zstdhl_EncodeMatchLength(uint32_t value, uint32_t *outFSEValue, uint32_t *outExtraValue, uint8_t *outExtraBits);
zstdhl_ResultCode_t zstdhl_EncodeLitLength(uint32_t value, uint32_t *outFSEValue, uint32_t *outExtraValue, uint8_t *outExtraBits);
zstdhl_ResultCode_t zstdhl_ResolveOffsetCode32(zstdhl_OffsetType_t offsetType, uint32_t litLength, uint32_t offsetValue, uint32_t *outOffsetCode);

zstdhl_ResultCode_t zstdhl_GenerateHuffmanDecodeTable(const zstdhl_HuffmanTreePartialWeightDesc_t *partialWeightDesc, zstdhl_HuffmanTableDec_t *decTable);
zstdhl_ResultCode_t zstdhl_GenerateHuffmanEncodeTable(const zstdhl_HuffmanTreePartialWeightDesc_t *partialWeightDesc, zstdhl_HuffmanTableEnc_t *encTable);
zstdhl_ResultCode_t zstdhl_ExpandHuffmanWeightTable(const zstdhl_HuffmanTreePartialWeightDesc_t *partialDesc, zstdhl_HuffmanTreeWeightDesc_t *fullDesc);

zstdhl_ResultCode_t zstdhl_ReadChecked(const zstdhl_StreamSourceObject_t *streamSource, void *dest, size_t numBytes, zstdhl_ResultCode_t failureResult);

void zstdhl_Vector_Init(zstdhl_Vector_t *vec, size_t elementSize, const zstdhl_MemoryAllocatorObject_t *alloc);
zstdhl_ResultCode_t zstdhl_Vector_Append(zstdhl_Vector_t *vec, const void *data, size_t count);
void zstdhl_Vector_Clear(zstdhl_Vector_t *vec);
void zstdhl_Vector_Shrink(zstdhl_Vector_t *vec, size_t newCount);
void zstdhl_Vector_Reset(zstdhl_Vector_t *vec);
void zstdhl_Vector_Destroy(zstdhl_Vector_t *vec);

void zstdhl_MemBufferStreamSource_Init(zstdhl_MemBufferStreamSource_t *streamSource, const void *data, size_t size);
size_t zstdhl_MemBufferStreamSource_ReadBytes(void *userdata, void *dest, size_t numBytes);

zstdhl_ResultCode_t zstdhl_DeflateConv_CreateState(const zstdhl_MemoryAllocatorObject_t *alloc, const zstdhl_StreamSourceObject_t *streamSource, zstdhl_DeflateConv_State_t **outState);
void zstdhl_DeflateConv_DestroyState(zstdhl_DeflateConv_State_t *state);
zstdhl_ResultCode_t zstdhl_DeflateConv_Convert(zstdhl_DeflateConv_State_t *state, uint8_t *outEOFFlag, zstdhl_EncBlockDesc_t *outTempBlockDesc);

zstdhl_ResultCode_t zstdhl_CreateHuffmanDescFromSymbolCounts(const size_t *symbolCounts, size_t numSymbolCounts, zstdhl_HuffmanTreeDesc_t *outTreeDesc);
zstdhl_ResultCode_t zstdhl_CreateFSEDefFromSymbolCounts(const size_t *symbolCounts, size_t numSymbolCounts, zstdhl_Vector_t *probsVector, uint8_t maxAccuracyLog, zstdhl_FSETableDef_t *outTableDef);

#ifdef __cplusplus
}
#endif

#endif
