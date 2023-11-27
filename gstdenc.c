#include "zstdhl.h"
#include "zstdhl_util.h"
#include "zstdhl_internal.h"

typedef struct gstd_LaneState gstd_LaneState_t;

void CheckMem();

#define GSTD_FLUSH_GRANULARITY		4
#define GSTD_MAX_FLUSH_POSITIONS	2
#define GSTD_SYNC_COMMAND_PEEK_FLAG		0x80
#define GSTD_SYNC_COMMAND_PEEK_ALL_FLAG	0x40

#define GSTD_CONTROL_DECOMPRESSED_SIZE_OFFSET	0
#define GSTD_CONTROL_LAST_BLOCK_BIT_OFFSET		22
#define GSTD_CONTROL_AUX_BIT_OFFSET				23
#define GSTD_CONTROL_LIT_SECTION_TYPE_OFFSET	24
#define GSTD_CONTROL_LIT_LENGTH_MODE_OFFSET		26
#define GSTD_CONTROL_OFFSET_MODE_OFFSET			28
#define GSTD_CONTROL_MATCH_LENGTH_MODE_OFFSET	30

#define GSTD_MAX_OFFSET_CODE					31
#define GSTD_MAX_MATCH_LENGTH_CODE				52
#define GSTD_MAX_LIT_LENGTH_CODE				35
#define GSTD_MAX_HUFFMAN_WEIGHT					11
#define GSTD_MAX_HUFFMAN_CODE_LENGTH			11

#define GSTD_MAX_HUFFMAN_WEIGHT_ACCURACY_LOG	6
#define GSTD_MAX_OFFSET_ACCURACY_LOG			8
#define GSTD_MAX_MATCH_LENGTH_ACCURACY_LOG		9
#define GSTD_MAX_LIT_LENGTH_ACCURACY_LOG		9

#define GSTD_MAX_LIT_LENGTH_EXTRA_BITS			16
#define GSTD_MAX_MATCH_LENGTH_EXTRA_BITS		16

#define GSTD_MAX_ACCURACY_LOG					9

#define GSTD_MAX_ZERO_PROB_REPEAT_COUNT			7

typedef struct gstd_InterleavedBitstream
{
	uint8_t m_bits[GSTD_FLUSH_GRANULARITY];
	uint8_t m_numBits;
	size_t m_flushPositions[GSTD_MAX_FLUSH_POSITIONS];
	uint8_t m_numFlushPositions;
} gstd_InterleavedBitstream_t;

typedef struct gstd_PendingSequence
{
	uint32_t m_litLength;
	uint32_t m_matchLength;
	uint32_t m_offsetCode;
} gstd_PendingSequence_t;

typedef struct gstd_EncoderState
{
	zstdhl_MemoryAllocatorObject_t m_alloc;
	zstdhl_Vector_t m_laneStateVector;

	zstdhl_Vector_t m_pendingOutputVector;
	size_t m_syncCommandReadOffset;

	zstdhl_Vector_t m_pendingSequencesVector;

	const zstdhl_EncoderOutputObject_t *m_output;
	size_t m_numLanes;

	gstd_InterleavedBitstream_t m_rawBytesBitstream;

	gstd_LaneState_t *m_laneStates;

	zstdhl_FSETableDef_t m_huffWeightTableDef;
	zstdhl_FSETableDef_t m_litLengthTableDef;
	zstdhl_FSETableDef_t m_matchLengthTableDef;
	zstdhl_FSETableDef_t m_offsetTableDef;

	zstdhl_FSETable_t m_huffWeightTable;
	zstdhl_FSETable_t m_litLengthTable;
	zstdhl_FSETable_t m_matchLengthTable;
	zstdhl_FSETable_t m_offsetTable;

	zstdhl_FSETableEnc_t m_huffWeightsTableEnc;
	zstdhl_FSETableEnc_t m_litLengthTableEnc;
	zstdhl_FSETableEnc_t m_matchLengthTableEnc;
	zstdhl_FSETableEnc_t m_offsetTableEnc;

	zstdhl_FSETableCell_t m_huffWeightCells[1 << GSTD_MAX_HUFFMAN_WEIGHT_ACCURACY_LOG];
	zstdhl_FSETableCell_t m_offsetCells[1 << GSTD_MAX_OFFSET_ACCURACY_LOG];
	zstdhl_FSETableCell_t m_matchLengthCells[1 << GSTD_MAX_MATCH_LENGTH_ACCURACY_LOG];
	zstdhl_FSETableCell_t m_litLengthCells[1 << GSTD_MAX_LIT_LENGTH_ACCURACY_LOG];

	uint32_t m_huffWeightProbs[GSTD_MAX_HUFFMAN_WEIGHT + 1];
	uint32_t m_offsetProbs[GSTD_MAX_OFFSET_CODE + 1];
	uint32_t m_matchLengthProbs[GSTD_MAX_MATCH_LENGTH_CODE + 1];
	uint32_t m_litLengthProbs[GSTD_MAX_LIT_LENGTH_CODE + 1];

	uint16_t m_huffWeightNextStates[(GSTD_MAX_HUFFMAN_WEIGHT + 1) << GSTD_MAX_HUFFMAN_WEIGHT_ACCURACY_LOG];
	uint16_t m_offsetNextStates[(GSTD_MAX_OFFSET_CODE + 1) << GSTD_MAX_OFFSET_ACCURACY_LOG];
	uint16_t m_matchLengthNextStates[(GSTD_MAX_MATCH_LENGTH_CODE + 1) << GSTD_MAX_MATCH_LENGTH_ACCURACY_LOG];
	uint16_t m_litLengthNextStates[(GSTD_MAX_LIT_LENGTH_CODE + 1) << GSTD_MAX_LIT_LENGTH_ACCURACY_LOG];

	zstdhl_SequencesCompressionMode_t m_offsetMode;
	zstdhl_SequencesCompressionMode_t m_matchLengthMode;
	zstdhl_SequencesCompressionMode_t m_litLengthMode;

	zstdhl_HuffmanTableEnc_t m_huffmanEnc;
} gstd_EncoderState_t;

void gstd_InterleavedBitstream_Init(gstd_InterleavedBitstream_t *bitstream)
{
	int i = 0;

	for (i = 0; i < GSTD_FLUSH_GRANULARITY; i++)
		bitstream->m_bits[i] = 0;

	for (i = 0; i < GSTD_MAX_FLUSH_POSITIONS; i++)
		bitstream->m_flushPositions[i] = 0;

	bitstream->m_numBits = 0;
	bitstream->m_numFlushPositions = 0;
}

typedef struct gstd_LanePendingSequenceValues
{
	uint32_t m_value;
	uint32_t m_extra;
	uint8_t m_extraNumBits;
} gstd_LanePendingSequenceValues_t;

struct gstd_LaneState
{
	gstd_InterleavedBitstream_t m_interleavedBitstream;
	zstdhl_FSEEncStack_t m_fseStack;
	uint16_t m_currentFSEState;
	uint8_t m_bitsNeededToRefill;

	gstd_LanePendingSequenceValues_t m_pendingOffset;
	gstd_LanePendingSequenceValues_t m_pendingMatchLength;
	gstd_LanePendingSequenceValues_t m_pendingLitLength;
};

void gstd_LaneState_Init(gstd_LaneState_t *laneState, const zstdhl_MemoryAllocatorObject_t *alloc)
{
	gstd_InterleavedBitstream_Init(&laneState->m_interleavedBitstream);
	zstdhl_FSEEncStack_Init(&laneState->m_fseStack, alloc);
	laneState->m_currentFSEState = 0;
}

void gstd_LaneState_Destroy(gstd_LaneState_t *laneState)
{
	zstdhl_FSEEncStack_Destroy(&laneState->m_fseStack);
}


zstdhl_ResultCode_t gstd_EncoderState_Init(gstd_EncoderState_t *encState, const zstdhl_EncoderOutputObject_t *output, size_t numLanes, const zstdhl_MemoryAllocatorObject_t *alloc)
{
	size_t i = 0;

	encState->m_alloc.m_reallocFunc = alloc->m_reallocFunc;
	encState->m_alloc.m_userdata = alloc->m_userdata;

	zstdhl_Vector_Init(&encState->m_laneStateVector, sizeof(gstd_LaneState_t), alloc);
	zstdhl_Vector_Init(&encState->m_pendingOutputVector, 1, alloc);
	zstdhl_Vector_Init(&encState->m_pendingSequencesVector, sizeof(gstd_PendingSequence_t), alloc);

	encState->m_output = output;
	encState->m_numLanes = numLanes;
	encState->m_syncCommandReadOffset = 0;

	ZSTDHL_CHECKED(zstdhl_Vector_Append(&encState->m_laneStateVector, NULL, numLanes));

	encState->m_laneStates = (gstd_LaneState_t *)encState->m_laneStateVector.m_data;
	for (i = 0; i < numLanes; i++)
		gstd_LaneState_Init(encState->m_laneStates + i, alloc);

	gstd_InterleavedBitstream_Init(&encState->m_rawBytesBitstream);

	encState->m_huffWeightTableDef.m_probabilities = encState->m_huffWeightProbs;
	encState->m_huffWeightTableDef.m_numProbabilities = 0;

	encState->m_litLengthTableDef.m_probabilities = encState->m_litLengthProbs;
	encState->m_litLengthTableDef.m_numProbabilities = 0;

	encState->m_matchLengthTableDef.m_probabilities = encState->m_matchLengthProbs;
	encState->m_matchLengthTableDef.m_numProbabilities = 0;

	encState->m_offsetTableDef.m_probabilities = encState->m_offsetProbs;
	encState->m_offsetTableDef.m_numProbabilities = 0;

	encState->m_huffWeightsTableEnc.m_nextStates = encState->m_huffWeightNextStates;
	encState->m_litLengthTableEnc.m_nextStates = encState->m_litLengthNextStates;
	encState->m_matchLengthTableEnc.m_nextStates = encState->m_matchLengthNextStates;
	encState->m_offsetTableEnc.m_nextStates = encState->m_offsetNextStates;

	encState->m_huffWeightTable.m_cells = encState->m_huffWeightCells;
	encState->m_litLengthTable.m_cells = encState->m_litLengthCells;
	encState->m_matchLengthTable.m_cells = encState->m_matchLengthCells;
	encState->m_offsetTable.m_cells = encState->m_offsetCells;

	encState->m_offsetMode = ZSTDHL_SEQ_COMPRESSION_MODE_INVALID;
	encState->m_matchLengthMode = ZSTDHL_SEQ_COMPRESSION_MODE_INVALID;
	encState->m_litLengthMode = ZSTDHL_SEQ_COMPRESSION_MODE_INVALID;

	for (i = 0; i < 256; i++)
	{
		encState->m_huffmanEnc.m_entries[i].m_bits = (uint16_t)i;
		encState->m_huffmanEnc.m_entries[i].m_numBits = 8;
	}

	return ZSTDHL_RESULT_OK;
}

void gstd_EncoderState_Destroy(gstd_EncoderState_t *encState)
{
	size_t i = 0;

	for (i = 0; i < encState->m_laneStateVector.m_count; i++)
		gstd_LaneState_Destroy(encState->m_laneStates + i);

	zstdhl_Vector_Destroy(&encState->m_laneStateVector);
	zstdhl_Vector_Destroy(&encState->m_pendingOutputVector);
	zstdhl_Vector_Destroy(&encState->m_pendingSequencesVector);
}

static zstdhl_ResultCode_t gstd_FlushBlocks(gstd_EncoderState_t *encState)
{
	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}

zstdhl_ResultCode_t gstd_Encoder_Create(const zstdhl_EncoderOutputObject_t *output, size_t numLanes, const zstdhl_MemoryAllocatorObject_t *alloc, gstd_EncoderState_t **outEncState)
{
	zstdhl_ResultCode_t resultCode = ZSTDHL_RESULT_OK;
	gstd_EncoderState_t *encState = alloc->m_reallocFunc(alloc->m_userdata, NULL, sizeof(gstd_EncoderState_t));
	if (!encState)
		return ZSTDHL_RESULT_OUT_OF_MEMORY;

	resultCode = gstd_EncoderState_Init(encState, output, numLanes, alloc);
	if (resultCode != ZSTDHL_RESULT_OK)
	{
		gstd_EncoderState_Destroy(encState);
		*outEncState = NULL;
		return resultCode;
	}

	*outEncState = encState;
	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_Encoder_SyncPeek(gstd_EncoderState_t *enc, gstd_InterleavedBitstream_t *bitstream, uint8_t numBits)
{
	uint8_t b[GSTD_FLUSH_GRANULARITY];
	uint8_t numBitsUnallocated = bitstream->m_numFlushPositions * GSTD_FLUSH_GRANULARITY * 8 - bitstream->m_numBits;
	int i = 0;

	if (numBitsUnallocated >= numBits)
		return ZSTDHL_RESULT_OK;

	for (i = 0; i < GSTD_FLUSH_GRANULARITY; i++)
		b[i] = 0;

	CheckMem();

	// Not enough bits
	while (numBitsUnallocated < numBits)
	{
		if (bitstream->m_numFlushPositions == GSTD_MAX_FLUSH_POSITIONS)
			return ZSTDHL_RESULT_INTERNAL_ERROR;

		bitstream->m_flushPositions[bitstream->m_numFlushPositions++] = enc->m_pendingOutputVector.m_count;

		ZSTDHL_CHECKED(zstdhl_Vector_Append(&enc->m_pendingOutputVector, b, GSTD_FLUSH_GRANULARITY));
		numBitsUnallocated += GSTD_FLUSH_GRANULARITY * 8;
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_Encoder_SyncBroadcastPeek(gstd_EncoderState_t *enc, uint8_t numBits, size_t numLanes)
{
	size_t i = 0;

	if (numLanes > enc->m_numLanes)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	for (i = 0; i < numLanes; i++)
		ZSTDHL_CHECKED(gstd_Encoder_SyncPeek(enc, &enc->m_laneStates[i].m_interleavedBitstream, numBits));

	return ZSTDHL_RESULT_OK;
}


zstdhl_ResultCode_t gstd_Encoder_SyncBroadcastPeekAll(gstd_EncoderState_t *enc, uint8_t numBits)
{
	return gstd_Encoder_SyncBroadcastPeek(enc, numBits, enc->m_numLanes);
}

zstdhl_ResultCode_t gstd_Encoder_PutBits(gstd_EncoderState_t *enc, gstd_InterleavedBitstream_t *bitstream, uint32_t value, uint8_t numBits)
{
	CheckMem();

	while (numBits > 0)
	{
		uint8_t partialBits = (bitstream->m_numBits & 7);
		uint8_t byteOffset = (bitstream->m_numBits / 8);

		if (byteOffset >= GSTD_FLUSH_GRANULARITY)
			return ZSTDHL_RESULT_INTERNAL_ERROR;

		CheckMem();

		if (partialBits != 0)
		{
			uint8_t bitsAvailableAtTop = 8 - partialBits;
			uint8_t bitMask = (1 << bitsAvailableAtTop) - 1;
			uint8_t bitsToAdd = bitsAvailableAtTop;

			if (bitsToAdd > numBits)
				bitsToAdd = numBits;

			bitstream->m_bits[byteOffset] |= (value & bitMask) << partialBits;
			bitstream->m_numBits += bitsToAdd;
			numBits -= bitsToAdd;
			value >>= bitsToAdd;
		}
		else
		{
			uint8_t bitsToAdd = 8;

			if (bitsToAdd > numBits)
				bitsToAdd = numBits;

			bitstream->m_bits[byteOffset] = (value & 0xffu);
			bitstream->m_numBits += bitsToAdd;
			numBits -= bitsToAdd;
			value >>= bitsToAdd;
		}

		if (bitstream->m_numBits == GSTD_FLUSH_GRANULARITY * 8)
		{
			if (bitstream->m_numFlushPositions == 0)
				return ZSTDHL_RESULT_INTERNAL_ERROR;

			int i = 0;
			uint8_t *flushOut = (uint8_t *)enc->m_pendingOutputVector.m_data + bitstream->m_flushPositions[0];

			CheckMem();

			for (i = 0; i < GSTD_FLUSH_GRANULARITY; i++)
				flushOut[i] = bitstream->m_bits[i];

			for (i = 0; i < GSTD_FLUSH_GRANULARITY; i++)
				bitstream->m_bits[i] = 0;

			bitstream->m_numBits = 0;

			for (i = 1; i < bitstream->m_numFlushPositions; i++)
				bitstream->m_flushPositions[i - 1] = bitstream->m_flushPositions[i];

			bitstream->m_numFlushPositions--;

			CheckMem();
		}
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_Encoder_EncodeFSETable(gstd_EncoderState_t *enc, const zstdhl_EncBlockDesc_t *block, const zstdhl_FSETableDef_t *table)
{
	size_t i = 0;
	uint8_t accuracyLog = table->m_accuracyLog;
	uint8_t peekSize = accuracyLog + 3;
	size_t numProbs = 0;
	size_t numLanesWritten = 0;
	uint32_t lessThanOneProbValue = zstdhl_GetLessThanOneConstant();
	uint32_t probSpaceRemaining = (1 << accuracyLog);

	// Find the real prob count
	for (i = 0; i < table->m_numProbabilities; i++)
	{
		if (table->m_probabilities[i] != 0)
			numProbs = i + 1;
	}

	if (numProbs == 0)
		return ZSTDHL_RESULT_FSE_TABLE_INVALID;

	for (i = 0; i < numProbs; i++)
	{
		size_t repeatCount = 0;
		uint32_t prob = table->m_probabilities[i];
		uint32_t outputValue = 0;
		uint32_t outputBits = 0;

		if (prob == lessThanOneProbValue)
			prob = 1;

		if (prob > probSpaceRemaining)
			return ZSTDHL_RESULT_FSE_TABLE_INVALID;

		if (prob == 0)
		{
			while (table->m_probabilities[i + 1] == 0 && repeatCount < GSTD_MAX_ZERO_PROB_REPEAT_COUNT)
			{
				repeatCount++;
				i++;
			}
		}

		if (numLanesWritten == 0)
			ZSTDHL_CHECKED(gstd_Encoder_SyncBroadcastPeekAll(enc, peekSize));

		outputValue = prob;
		outputBits = zstdhl_Log2_32(probSpaceRemaining) + 1;
		if (prob == 0)
		{
			outputValue |= (repeatCount << outputBits);
			outputBits += 3;
		}

		ZSTDHL_CHECKED(gstd_Encoder_PutBits(enc, &enc->m_laneStates[numLanesWritten].m_interleavedBitstream, outputValue, outputBits));

		numLanesWritten++;

		if (numLanesWritten == 32)
			numLanesWritten = 0;

		probSpaceRemaining -= prob;
	}

	return ZSTDHL_RESULT_OK;
}


zstdhl_ResultCode_t gstd_BuildFSEDistributionTable(zstdhl_FSETable_t *fseTable, const zstdhl_FSETableDef_t *fseTableDef)
{
	uint8_t accuracyLog = fseTableDef->m_accuracyLog;
	uint32_t numCells = (1 << accuracyLog);
	zstdhl_FSETableCell_t *cells = fseTable->m_cells;
	uint32_t numNotLowProbCells = numCells;
	size_t i = 0;
	size_t j = 0;
	size_t numProbs = fseTableDef->m_numProbabilities;
	const uint32_t *probs = fseTableDef->m_probabilities;
	uint32_t lessThanOneValue = zstdhl_GetLessThanOneConstant();

	uint32_t advanceStep = (numCells >> 1) + (numCells >> 3) + 3;
	uint32_t cellMask = numCells - 1;

	uint32_t insertPos = 0;

	fseTable->m_numCells = numCells;
	fseTable->m_accuracyLog = accuracyLog;

	for (i = 0; i < numProbs; i++)
	{
		uint32_t prob = probs[i];
		uint32_t baseline;
		uint32_t numLargeSteppingRemaining;
		uint8_t smallSize;

		int probDivisionBits = 0;
		if (prob == lessThanOneValue)
			prob = 1;

		if (prob == 0)
			continue;

		probDivisionBits = zstdhl_Log2_32((prob - 1) * 2 + 1);	// Should be 5 for 21 through 32

		smallSize = accuracyLog - probDivisionBits;
		numLargeSteppingRemaining = (uint32_t)(1 << probDivisionBits) - prob;
		if (numLargeSteppingRemaining > 0)
			baseline = (1 << accuracyLog) - (numLargeSteppingRemaining << (smallSize + 1));
		else
			baseline = 0;

		for (j = 0; j < prob; j++)
		{
			zstdhl_FSETableCell_t *cell = cells + insertPos;
			cell->m_sym = i;
			cell->m_baseline = baseline;

			if (numLargeSteppingRemaining)
			{
				numLargeSteppingRemaining--;
				cell->m_numBits = smallSize + 1;

				if (numLargeSteppingRemaining == 0)
					baseline = 0;
				else
					baseline += (uint32_t)(1 << (smallSize + 1));
			}
			else
			{
				cell->m_numBits = smallSize;
				baseline += (uint32_t)(1 << smallSize);
			}

			insertPos = (insertPos + advanceStep) % numCells;
		}
	}

	return ZSTDHL_RESULT_OK;
}

// Perform actions corresponding to a decoder value extraction.
// This decodes a value and puts the decoder in a drained state.
zstdhl_ResultCode_t gstd_Encoder_CheckAndPutFSEValue(gstd_EncoderState_t *enc, size_t laneIndex, const zstdhl_FSETable_t *table, const zstdhl_FSETableEnc_t *encTable, uint16_t value)
{
	uint16_t stateMask = (1 << table->m_accuracyLog) - 1;
	gstd_LaneState_t *laneState = enc->m_laneStates + laneIndex;
	const zstdhl_FSETableCell_t *cell = table->m_cells + (laneState->m_currentFSEState & stateMask);

	if (laneState->m_bitsNeededToRefill != 0)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	if (cell->m_sym != value)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	laneState->m_bitsNeededToRefill = cell->m_numBits;

	return ZSTDHL_RESULT_OK;
}

// Perform actions corresponding to a decoder state refill.
// The state refill retrieves enough bits to reconstruct the full state value.
zstdhl_ResultCode_t gstd_Encoder_FlushStateRefill(gstd_EncoderState_t *enc, size_t numLanes)
{
	size_t laneIndex = 0;

	for (laneIndex = 0; laneIndex < numLanes; laneIndex++)
	{
		gstd_LaneState_t *laneState = enc->m_laneStates + laneIndex;
		uint16_t drainMask = (1 << laneState->m_bitsNeededToRefill) - 1;

		ZSTDHL_CHECKED(zstdhl_FSEEncStack_Pop(&laneState->m_fseStack, &laneState->m_currentFSEState));

		ZSTDHL_CHECKED(gstd_Encoder_PutBits(enc, &laneState->m_interleavedBitstream, laneState->m_currentFSEState & drainMask, laneState->m_bitsNeededToRefill));
		laneState->m_bitsNeededToRefill = 0;
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_Encoder_EncodeHuffmanTree(gstd_EncoderState_t *enc, const zstdhl_EncBlockDesc_t *block, uint32_t *outAuxBit)
{
	const zstdhl_HuffmanTreeDesc_t *huffmanTreeDesc = &block->m_huffmanTreeDesc;
	uint8_t numSpecifiedWeights = huffmanTreeDesc->m_partialWeightDesc.m_numSpecifiedWeights;
	
	*outAuxBit = 0;

	ZSTDHL_CHECKED(gstd_Encoder_SyncPeek(enc, &enc->m_rawBytesBitstream, 16));

	if (huffmanTreeDesc->m_huffmanWeightFormat == ZSTDHL_HUFFMAN_WEIGHT_ENCODING_UNCOMPRESSED)
	{
		ZSTDHL_CHECKED(gstd_Encoder_PutBits(enc, &enc->m_rawBytesBitstream, 0, 8));
		ZSTDHL_CHECKED(gstd_Encoder_PutBits(enc, &enc->m_rawBytesBitstream, numSpecifiedWeights, 8));

		int i = 0;
		for (i = 0; i < numSpecifiedWeights; i += 2)
		{
			if ((i & 1) == 0)
				ZSTDHL_CHECKED(gstd_Encoder_SyncPeek(enc, &enc->m_rawBytesBitstream, 8));

			ZSTDHL_CHECKED(gstd_Encoder_PutBits(enc, &enc->m_rawBytesBitstream, huffmanTreeDesc->m_partialWeightDesc.m_specifiedWeights[i], 4));
		}

		if ((numSpecifiedWeights & 1) != 0)
			ZSTDHL_CHECKED(gstd_Encoder_PutBits(enc, &enc->m_rawBytesBitstream, 0, 4));
	}
	else
	{
		const zstdhl_FSETableDef_t *huffTable = &huffmanTreeDesc->m_weightTable;
		size_t i = 0;
		uint8_t accuracyLog = huffmanTreeDesc->m_weightTable.m_accuracyLog;

		if (huffmanTreeDesc->m_huffmanWeightFormat != ZSTDHL_HUFFMAN_WEIGHT_ENCODING_FSE || accuracyLog > 6 || accuracyLog < 5 || numSpecifiedWeights == 0)
			return ZSTDHL_RESULT_INTERNAL_ERROR;

		*outAuxBit = accuracyLog - 5;

		// Write out data
		ZSTDHL_CHECKED(gstd_Encoder_PutBits(enc, &enc->m_rawBytesBitstream, numSpecifiedWeights, 8));
		ZSTDHL_CHECKED(gstd_Encoder_EncodeFSETable(enc, block, huffTable));

		for (i = 0; i < numSpecifiedWeights; i++)
		{
			size_t laneIndex = i % enc->m_numLanes;
			if (laneIndex == 0)
			{
				size_t broadcastSize = numSpecifiedWeights - i;
				uint8_t bitsToRefill = GSTD_MAX_ACCURACY_LOG;

				if (broadcastSize > enc->m_numLanes)
					broadcastSize = enc->m_numLanes;

				if (i > 0)
					bitsToRefill = GSTD_MAX_HUFFMAN_WEIGHT_ACCURACY_LOG;

				ZSTDHL_CHECKED(gstd_Encoder_SyncBroadcastPeek(enc, bitsToRefill, broadcastSize));
				ZSTDHL_CHECKED(gstd_Encoder_FlushStateRefill(enc, broadcastSize));
			}

			ZSTDHL_CHECKED(gstd_Encoder_CheckAndPutFSEValue(enc, laneIndex, &enc->m_huffWeightTable, &enc->m_huffWeightsTableEnc, huffmanTreeDesc->m_partialWeightDesc.m_specifiedWeights[i]));
		}
	}

	return ZSTDHL_RESULT_OK;
}



zstdhl_ResultCode_t gstd_GenerateHuffmanEncodeTable(const zstdhl_HuffmanTreeDesc_t *treeDesc, zstdhl_HuffmanTableEnc_t *encTable)
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

	if (maxBits > GSTD_MAX_HUFFMAN_WEIGHT)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	for (i = 0; i < 256; i++)
	{
		encTable->m_entries[i].m_numBits = 0;
		encTable->m_entries[i].m_bits = 0;
	}

	for (i = 0; i < maxBits; i++)
	{
		uint8_t numBits = maxBits - i;
		uint8_t expectedWeight = (maxBits - numBits) + 1;

		uint32_t stepping = (1u << (expectedWeight - 1));
		uint32_t sym = 0;

		for (sym = 0; sym < 256; sym++)
		{
			if (weightDesc.m_weights[sym] == expectedWeight)
			{
				weightIterator -= stepping;

				encTable->m_entries[sym].m_bits = (weightIterator >> (expectedWeight - 1));
				encTable->m_entries[sym].m_numBits = numBits;
			}
		}
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_Encoder_EncodeHuffmanLiterals(gstd_EncoderState_t *enc, const zstdhl_EncBlockDesc_t *block, int haveNewTree, uint32_t *outAuxBit)
{
	size_t i = 0;
	size_t numLanes = enc->m_numLanes;
	const zstdhl_StreamSourceObject_t *streamObj = block->m_litSectionDesc.m_decompressedLiteralsStream;

	if (haveNewTree)
	{
		ZSTDHL_CHECKED(gstd_Encoder_EncodeHuffmanTree(enc, block, outAuxBit));

		ZSTDHL_CHECKED(gstd_GenerateHuffmanEncodeTable(&block->m_huffmanTreeDesc, &enc->m_huffmanEnc));
	}
	else
		*outAuxBit = 0;

	for (i = 0; i < block->m_litSectionHeader.m_regeneratedSize; i++)
	{
		size_t laneIndex = i % numLanes;
		uint8_t lit = 0;
		const zstdhl_HuffmanTableEncEntry_t *tableEntry = NULL;

		ZSTDHL_CHECKED(zstdhl_ReadChecked(streamObj, &lit, 1, ZSTDHL_RESULT_INPUT_FAILED));

		tableEntry = enc->m_huffmanEnc.m_entries + lit;

		if (laneIndex == 0)
		{
			size_t broadcastSize = block->m_litSectionHeader.m_regeneratedSize - i;
			if (broadcastSize > numLanes)
				broadcastSize = numLanes;

			ZSTDHL_CHECKED(gstd_Encoder_SyncBroadcastPeek(enc, GSTD_MAX_HUFFMAN_CODE_LENGTH, broadcastSize));
		}

		ZSTDHL_CHECKED(gstd_Encoder_PutBits(enc, &enc->m_laneStates[laneIndex].m_interleavedBitstream, tableEntry->m_bits, tableEntry->m_numBits));
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_Encoder_EncodeRawLiterals(gstd_EncoderState_t *enc, const zstdhl_EncBlockDesc_t *block)
{
	uint8_t b = 0;
	size_t i = 0;
	const zstdhl_StreamSourceObject_t *streamObj = block->m_litSectionDesc.m_decompressedLiteralsStream;

	for (i = 0; i < block->m_litSectionDesc.m_numValues; i++)
	{
		ZSTDHL_CHECKED(zstdhl_ReadChecked(streamObj, &b, 1, ZSTDHL_RESULT_INPUT_FAILED));

		ZSTDHL_CHECKED(gstd_Encoder_SyncPeek(enc, &enc->m_rawBytesBitstream, 8));
		ZSTDHL_CHECKED(gstd_Encoder_PutBits(enc, &enc->m_rawBytesBitstream, b, 8));
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_Encoder_EncodePackedSize(gstd_EncoderState_t *enc, uint32_t sizeValue)
{
	CheckMem();

	ZSTDHL_CHECKED(gstd_Encoder_SyncPeek(enc, &enc->m_rawBytesBitstream, 24));

	CheckMem();

	if (sizeValue < 128)
	{
		ZSTDHL_CHECKED(gstd_Encoder_PutBits(enc, &enc->m_rawBytesBitstream, sizeValue << 1, 8));
	}
	else
	{
		sizeValue -= 128;

		if (sizeValue < 16384)
		{
			ZSTDHL_CHECKED(gstd_Encoder_PutBits(enc, &enc->m_rawBytesBitstream, (sizeValue << 2) + 1, 16));
		}
		else
		{
			sizeValue -= 16384;

			ZSTDHL_CHECKED(gstd_Encoder_PutBits(enc, &enc->m_rawBytesBitstream, (sizeValue << 2) + 3, 24));
		}
	}

	CheckMem();

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_Encoder_EncodeLiteralsSection(gstd_EncoderState_t *enc, const zstdhl_EncBlockDesc_t *block, uint32_t *outAuxBit)
{
	ZSTDHL_CHECKED(gstd_Encoder_EncodePackedSize(enc, block->m_litSectionHeader.m_regeneratedSize));

	switch (block->m_litSectionHeader.m_sectionType)
	{
	case ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN:
		return gstd_Encoder_EncodeHuffmanLiterals(enc, block, 1, outAuxBit);
	case ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN_REUSE:
		return gstd_Encoder_EncodeHuffmanLiterals(enc, block, 0, outAuxBit);
	case ZSTDHL_LITERALS_SECTION_TYPE_RLE:
	case ZSTDHL_LITERALS_SECTION_TYPE_RAW:
		// These work identically
		return gstd_Encoder_EncodeRawLiterals(enc, block);
	default:
		return ZSTDHL_RESULT_INTERNAL_ERROR;
	}
}

zstdhl_ResultCode_t gstd_Encoder_EncodeSequencesSection(gstd_EncoderState_t *enc, const zstdhl_EncBlockDesc_t *block, uint32_t *outDecompressedSize)
{
	size_t sliceBase = 0;
	size_t laneIndex = 0;
	const gstd_PendingSequence_t *allSequences = (const gstd_PendingSequence_t *)enc->m_pendingSequencesVector.m_data;

	ZSTDHL_CHECKED(gstd_Encoder_EncodePackedSize(enc, block->m_seqSectionDesc.m_numSequences));

	for (sliceBase = 0; sliceBase < enc->m_pendingSequencesVector.m_count; sliceBase += enc->m_numLanes)
	{
		const gstd_PendingSequence_t *laneSequences = allSequences + sliceBase;
		size_t broadcastSize = enc->m_pendingSequencesVector.m_count - sliceBase;
		uint8_t fseStatesRefillSize = GSTD_MAX_OFFSET_ACCURACY_LOG + GSTD_MAX_LIT_LENGTH_ACCURACY_LOG + GSTD_MAX_MATCH_LENGTH_ACCURACY_LOG;

		if (broadcastSize > enc->m_numLanes)
			broadcastSize = enc->m_numLanes;

		for (laneIndex = 0; laneIndex < broadcastSize; laneIndex++)
		{
			gstd_LaneState_t *laneState = enc->m_laneStates + laneIndex;
			const gstd_PendingSequence_t *seq = laneSequences + laneIndex;

			ZSTDHL_CHECKED(zstdhl_EncodeLitLength(seq->m_litLength, &laneState->m_pendingLitLength.m_value, &laneState->m_pendingLitLength.m_extra, &laneState->m_pendingLitLength.m_extraNumBits));
			ZSTDHL_CHECKED(zstdhl_EncodeMatchLength(seq->m_matchLength, &laneState->m_pendingMatchLength.m_value, &laneState->m_pendingMatchLength.m_extra, &laneState->m_pendingMatchLength.m_extraNumBits));
			ZSTDHL_CHECKED(zstdhl_EncodeOffsetCode(seq->m_offsetCode, &laneState->m_pendingOffset.m_value, &laneState->m_pendingOffset.m_extra, &laneState->m_pendingOffset.m_extraNumBits));
		}

		ZSTDHL_CHECKED(gstd_Encoder_SyncBroadcastPeek(enc, fseStatesRefillSize, broadcastSize));

		if (enc->m_litLengthMode == ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED || enc->m_litLengthMode == ZSTDHL_SEQ_COMPRESSION_MODE_FSE)
		{
			ZSTDHL_CHECKED(gstd_Encoder_FlushStateRefill(enc, broadcastSize));

			for (laneIndex = 0; laneIndex < broadcastSize; laneIndex++)
				ZSTDHL_CHECKED(gstd_Encoder_CheckAndPutFSEValue(enc, laneIndex, &enc->m_litLengthTable, &enc->m_litLengthTableEnc, enc->m_laneStates[laneIndex].m_pendingLitLength.m_value));
		}

		if (enc->m_matchLengthMode == ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED || enc->m_matchLengthMode == ZSTDHL_SEQ_COMPRESSION_MODE_FSE)
		{
			ZSTDHL_CHECKED(gstd_Encoder_FlushStateRefill(enc, broadcastSize));

			for (laneIndex = 0; laneIndex < broadcastSize; laneIndex++)
				ZSTDHL_CHECKED(gstd_Encoder_CheckAndPutFSEValue(enc, laneIndex, &enc->m_matchLengthTable, &enc->m_matchLengthTableEnc, enc->m_laneStates[laneIndex].m_pendingMatchLength.m_value));
		}

		if (enc->m_offsetMode == ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED || enc->m_offsetMode == ZSTDHL_SEQ_COMPRESSION_MODE_FSE)
		{
			ZSTDHL_CHECKED(gstd_Encoder_FlushStateRefill(enc, broadcastSize));

			for (laneIndex = 0; laneIndex < broadcastSize; laneIndex++)
				ZSTDHL_CHECKED(gstd_Encoder_CheckAndPutFSEValue(enc, laneIndex, &enc->m_offsetTable, &enc->m_offsetTableEnc, enc->m_laneStates[laneIndex].m_pendingOffset.m_value));
		}

		ZSTDHL_CHECKED(gstd_Encoder_SyncBroadcastPeek(enc, GSTD_MAX_LIT_LENGTH_EXTRA_BITS + GSTD_MAX_MATCH_LENGTH_EXTRA_BITS, broadcastSize));

		for (laneIndex = 0; laneIndex < broadcastSize; laneIndex++)
		{
			gstd_LaneState_t *laneState = enc->m_laneStates + laneIndex;
			ZSTDHL_CHECKED(gstd_Encoder_PutBits(enc, &laneState->m_interleavedBitstream, laneState->m_pendingLitLength.m_extra, laneState->m_pendingLitLength.m_extraNumBits));
			ZSTDHL_CHECKED(gstd_Encoder_PutBits(enc, &laneState->m_interleavedBitstream, laneState->m_pendingMatchLength.m_extra, laneState->m_pendingMatchLength.m_extraNumBits));
		}

		ZSTDHL_CHECKED(gstd_Encoder_SyncBroadcastPeek(enc, GSTD_MAX_OFFSET_CODE, broadcastSize));

		for (laneIndex = 0; laneIndex < broadcastSize; laneIndex++)
		{
			gstd_LaneState_t *laneState = enc->m_laneStates + laneIndex;
			ZSTDHL_CHECKED(gstd_Encoder_PutBits(enc, &laneState->m_interleavedBitstream, laneState->m_pendingOffset.m_extra, laneState->m_pendingOffset.m_extraNumBits));
		}
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_Encoder_QueueAllSequences(gstd_EncoderState_t *enc, const zstdhl_EncBlockDesc_t *block)
{
	uint32_t numSequences = block->m_seqSectionDesc.m_numSequences;
	size_t i = 0;

	if (enc->m_pendingSequencesVector.m_count != 0)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	for (i = 0; i < numSequences; i++)
	{
		zstdhl_SequenceDesc_t seqDesc;
		gstd_PendingSequence_t seq;

		ZSTDHL_CHECKED(block->m_seqCollection.m_getNextSequence(block->m_seqCollection.m_userdata, &seqDesc));


		seq.m_litLength = seqDesc.m_litLength;
		seq.m_matchLength = seqDesc.m_matchLength;

		switch (seqDesc.m_offsetType)
		{
		case ZSTDHL_OFFSET_TYPE_REPEAT_1_MINUS_1:
			seq.m_litLength = 0;
			seq.m_offsetCode = 3;
			break;
		case ZSTDHL_OFFSET_TYPE_REPEAT_1:
			if (seqDesc.m_litLength == 0)
				return ZSTDHL_RESULT_INTERNAL_ERROR;
			seq.m_offsetCode = 1;
			break;
		case ZSTDHL_OFFSET_TYPE_REPEAT_2:
			if (seqDesc.m_litLength == 0)
				seq.m_offsetCode = 1;
			else
				seq.m_offsetCode = 2;
			break;
		case ZSTDHL_OFFSET_TYPE_REPEAT_3:
			if (seqDesc.m_litLength == 0)
				seq.m_offsetCode = 2;
			else
				seq.m_offsetCode = 3;
			break;
		case ZSTDHL_OFFSET_TYPE_SPECIFIED:
			if (seqDesc.m_offsetValueNumBits > 32 || (0xffffffffu - 3u) < seqDesc.m_offsetValueBigNum[0])
				return ZSTDHL_RESULT_INTEGER_OVERFLOW;

			seq.m_offsetCode = seqDesc.m_offsetValueBigNum[0] + 3;
			break;
		default:
			return ZSTDHL_RESULT_INTERNAL_ERROR;
		}

		ZSTDHL_CHECKED(zstdhl_Vector_Append(&enc->m_pendingSequencesVector, &seq, 1));
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_Encoder_ImportTable(zstdhl_SequencesCompressionMode_t sectionType, const zstdhl_EncSeqCompressionDesc_t *compressionDesc, zstdhl_SequencesCompressionMode_t *inOutMode, zstdhl_FSETableDef_t *tableDef, zstdhl_FSETable_t *table, zstdhl_FSETableEnc_t *encTable, uint32_t *probs, const zstdhl_SubstreamCompressionStructureDef_t *sdef, size_t numSymbols)
{
	if (sectionType == ZSTDHL_SEQ_COMPRESSION_MODE_FSE)
	{
		size_t i = 0;

		tableDef->m_accuracyLog = compressionDesc->m_fseProbs->m_accuracyLog;
		tableDef->m_numProbabilities = compressionDesc->m_fseProbs->m_numProbabilities;

		for (i = 0; i < tableDef->m_numProbabilities; i++)
			probs[i] = compressionDesc->m_fseProbs->m_probabilities[i];

		*inOutMode = ZSTDHL_SEQ_COMPRESSION_MODE_FSE;
	}
	else if (sectionType == ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED)
	{
		size_t i = 0;

		if ((*inOutMode) == ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED)
			return ZSTDHL_RESULT_OK;	// Don't need to rebuild

		tableDef->m_accuracyLog = sdef->m_defaultAccuracyLog;
		tableDef->m_numProbabilities = sdef->m_numProbs;

		for (i = 0; i < sdef->m_numProbs; i++)
			probs[i] = sdef->m_defaultProbs[i];

		*inOutMode = ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED;
	}
	else if (sectionType == ZSTDHL_SEQ_COMPRESSION_MODE_RLE)
	{
		*inOutMode = ZSTDHL_SEQ_COMPRESSION_MODE_RLE;
		return ZSTDHL_RESULT_OK;
	}
	else if (sectionType == ZSTDHL_SEQ_COMPRESSION_MODE_REUSE)
	{
		if ((*inOutMode) == ZSTDHL_SEQ_COMPRESSION_MODE_INVALID)
			return ZSTDHL_RESULT_INTERNAL_ERROR;

		return ZSTDHL_RESULT_OK;
	}
	else
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	ZSTDHL_CHECKED(gstd_BuildFSEDistributionTable(table, tableDef));
	zstdhl_BuildFSEEncodeTable(encTable, table, numSymbols);

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_Encoder_ResolveInitialFSEStates(gstd_EncoderState_t *enc, const zstdhl_EncBlockDesc_t *block)
{
	int haveHuffmanFSE = 0;
	int haveOffsetFSE = 0;
	int haveMatchLengthFSE = 0;
	int haveLitLengthFSE = 0;
	size_t i = 0;

	CheckMem();
	ZSTDHL_CHECKED(gstd_Encoder_ImportTable(block->m_seqSectionDesc.m_offsetsMode, &block->m_offsetsModeCompressionDesc, &enc->m_offsetMode, &enc->m_offsetTableDef, &enc->m_offsetTable, &enc->m_offsetTableEnc, enc->m_offsetProbs, zstdhl_GetDefaultOffsetFSEProperties(), GSTD_MAX_OFFSET_CODE + 1));
	CheckMem();
	ZSTDHL_CHECKED(gstd_Encoder_ImportTable(block->m_seqSectionDesc.m_matchLengthsMode, &block->m_matchLengthsCompressionDesc, &enc->m_matchLengthMode, &enc->m_matchLengthTableDef, &enc->m_matchLengthTable, &enc->m_matchLengthTableEnc, enc->m_matchLengthProbs, zstdhl_GetDefaultMatchLengthFSEProperties(), GSTD_MAX_MATCH_LENGTH_CODE + 1));
	CheckMem();
	ZSTDHL_CHECKED(gstd_Encoder_ImportTable(block->m_seqSectionDesc.m_literalLengthsMode, &block->m_literalLengthsCompressionDesc, &enc->m_litLengthMode, &enc->m_litLengthTableDef, &enc->m_litLengthTable, &enc->m_litLengthTableEnc, enc->m_litLengthProbs, zstdhl_GetDefaultLitLengthFSEProperties(), GSTD_MAX_LIT_LENGTH_CODE + 1));
	CheckMem();

	if (enc->m_offsetMode == ZSTDHL_SEQ_COMPRESSION_MODE_FSE || enc->m_offsetMode == ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED)
		haveOffsetFSE = 1;

	if (enc->m_matchLengthMode == ZSTDHL_SEQ_COMPRESSION_MODE_FSE || enc->m_matchLengthMode == ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED)
		haveMatchLengthFSE = 1;

	if (enc->m_litLengthMode == ZSTDHL_SEQ_COMPRESSION_MODE_FSE || enc->m_litLengthMode == ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED)
		haveLitLengthFSE = 1;

	for (i = 0; i < enc->m_pendingSequencesVector.m_count; i++)
	{
		size_t sequenceIndex = enc->m_pendingSequencesVector.m_count - 1 - i;
		size_t laneIndex = sequenceIndex % enc->m_numLanes;
		const gstd_PendingSequence_t *seq = ((const gstd_PendingSequence_t *)enc->m_pendingSequencesVector.m_data) + sequenceIndex;
		uint32_t fseValue = 0;
		uint32_t extraValue = 0;
		uint8_t extraBits = 0;

		if (haveOffsetFSE)
		{
			CheckMem();
			ZSTDHL_CHECKED(zstdhl_EncodeOffsetCode(seq->m_offsetCode, &fseValue, &extraValue, &extraBits));
			CheckMem();
			ZSTDHL_CHECKED(zstdhl_EncodeFSEValue(&enc->m_laneStates[laneIndex].m_fseStack, &enc->m_offsetTableEnc, &enc->m_offsetTable, fseValue));
			CheckMem();
		}

		if (haveMatchLengthFSE)
		{
			CheckMem();
			ZSTDHL_CHECKED(zstdhl_EncodeMatchLength(seq->m_matchLength, &fseValue, &extraValue, &extraBits));
			CheckMem();
			ZSTDHL_CHECKED(zstdhl_EncodeFSEValue(&enc->m_laneStates[laneIndex].m_fseStack, &enc->m_matchLengthTableEnc, &enc->m_matchLengthTable, fseValue));
			CheckMem();
		}

		if (haveLitLengthFSE)
		{
			CheckMem();
			ZSTDHL_CHECKED(zstdhl_EncodeLitLength(seq->m_litLength, &fseValue, &extraValue, &extraBits));
			CheckMem();
			ZSTDHL_CHECKED(zstdhl_EncodeFSEValue(&enc->m_laneStates[laneIndex].m_fseStack, &enc->m_litLengthTableEnc, &enc->m_litLengthTable, fseValue));
			CheckMem();
		}
	}

	if (block->m_litSectionHeader.m_sectionType == ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN && block->m_huffmanTreeDesc.m_huffmanWeightFormat == ZSTDHL_HUFFMAN_WEIGHT_ENCODING_FSE)
	{
		const zstdhl_FSETableDef_t *blockDef = &block->m_huffmanTreeDesc.m_weightTable;
		uint8_t numSpecifiedWeights = block->m_huffmanTreeDesc.m_partialWeightDesc.m_numSpecifiedWeights;

		for (i = 0; i < blockDef->m_numProbabilities; i++)
			enc->m_huffWeightProbs[i] = blockDef->m_probabilities[i];

		enc->m_huffWeightTableDef.m_accuracyLog = blockDef->m_accuracyLog;
		enc->m_huffWeightTableDef.m_numProbabilities = blockDef->m_numProbabilities;

		CheckMem();
		ZSTDHL_CHECKED(gstd_BuildFSEDistributionTable(&enc->m_huffWeightTable, &enc->m_huffWeightTableDef));
		CheckMem();
		zstdhl_BuildFSEEncodeTable(&enc->m_huffWeightsTableEnc, &enc->m_huffWeightTable, GSTD_MAX_HUFFMAN_WEIGHT);
		CheckMem();

		for (i = 0; i < numSpecifiedWeights; i++)
		{
			size_t weightIndex = numSpecifiedWeights - 1 - i;
			size_t laneIndex = weightIndex % enc->m_numLanes;
			uint8_t fseValue = block->m_huffmanTreeDesc.m_partialWeightDesc.m_specifiedWeights[weightIndex];

			CheckMem();
			ZSTDHL_CHECKED(zstdhl_EncodeFSEValue(&enc->m_laneStates[laneIndex].m_fseStack, &enc->m_huffWeightsTableEnc, &enc->m_huffWeightTable, fseValue));
			CheckMem();
		}
	}

	for (i = 0; i < enc->m_numLanes; i++)
	{
		gstd_LaneState_t *laneState = enc->m_laneStates + i;

		laneState->m_currentFSEState = 0;
		laneState->m_bitsNeededToRefill = GSTD_MAX_ACCURACY_LOG;
	}

	CheckMem();

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_Encoder_EncodeCompressedBlock(gstd_EncoderState_t *enc, const zstdhl_EncBlockDesc_t *block, uint32_t *outDecompressedSize, uint32_t *outAuxBit)
{
	ZSTDHL_CHECKED(gstd_Encoder_QueueAllSequences(enc, block));

	CheckMem();

	ZSTDHL_CHECKED(gstd_Encoder_ResolveInitialFSEStates(enc, block));

	CheckMem();

	ZSTDHL_CHECKED(gstd_Encoder_EncodeLiteralsSection(enc, block, outAuxBit));
	ZSTDHL_CHECKED(gstd_Encoder_EncodeSequencesSection(enc, block, outDecompressedSize));

	zstdhl_Vector_Clear(&enc->m_pendingSequencesVector);

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_Encoder_AddBlock(gstd_EncoderState_t *enc, const zstdhl_EncBlockDesc_t *block)
{
	uint32_t controlWord = 0;
	uint32_t decompressedSize = 0;
	uint32_t auxBit = 0;

	switch (block->m_blockHeader.m_blockType)
	{
	case ZSTDHL_BLOCK_TYPE_RAW:
		return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
	case ZSTDHL_BLOCK_TYPE_RLE:
		return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
	case ZSTDHL_BLOCK_TYPE_COMPRESSED:
		controlWord |= (block->m_litSectionHeader.m_sectionType << GSTD_CONTROL_LIT_SECTION_TYPE_OFFSET);
		controlWord |= (block->m_seqSectionDesc.m_literalLengthsMode << GSTD_CONTROL_LIT_LENGTH_MODE_OFFSET);
		controlWord |= (block->m_seqSectionDesc.m_offsetsMode << GSTD_CONTROL_OFFSET_MODE_OFFSET);
		controlWord |= (block->m_seqSectionDesc.m_offsetsMode << GSTD_CONTROL_MATCH_LENGTH_MODE_OFFSET);
		ZSTDHL_CHECKED(gstd_Encoder_EncodeCompressedBlock(enc, block, &decompressedSize, &auxBit));
		break;

	default:
		return ZSTDHL_RESULT_BLOCK_TYPE_INVALID;
	};

	if (block->m_blockHeader.m_isLastBlock)
		controlWord |= (1 << GSTD_CONTROL_LAST_BLOCK_BIT_OFFSET);

	controlWord |= (decompressedSize << GSTD_CONTROL_DECOMPRESSED_SIZE_OFFSET);
	controlWord |= (auxBit << GSTD_CONTROL_AUX_BIT_OFFSET);

	ZSTDHL_CHECKED(enc->m_output->m_writeControlWordFunc(enc->m_output->m_userdata, controlWord));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_Encoder_Finish(gstd_EncoderState_t *enc)
{
	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}

void gstd_Encoder_Destroy(gstd_EncoderState_t *enc)
{
	gstd_EncoderState_Destroy(enc);
	enc->m_alloc.m_reallocFunc(enc->m_alloc.m_userdata, enc, 0);
}

typedef enum gstd_TranscodeFSETablePurpose
{
	GSTD_TRANSCODE_FSE_TABLE_PURPOSE_NONE,

	GSTD_TRANSCODE_FSE_TABLE_PURPOSE_HUFFMAN_WEIGHTS,

	GSTD_TRANSCODE_FSE_TABLE_PURPOSE_LIT_LENGTH,
	GSTD_TRANSCODE_FSE_TABLE_PURPOSE_OFFSET,
	GSTD_TRANSCODE_FSE_TABLE_PURPOSE_MATCH_LENGTH,
} gstd_TranscodeFSETablePurpose_t;

typedef struct gstd_VectorByteReader
{
	const zstdhl_Vector_t *m_vector;
	size_t m_offset;
} zstdhl_VectorByteReader_t;

void gstd_VectorByteReader_Init(zstdhl_VectorByteReader_t *reader, const zstdhl_Vector_t *vector)
{
	reader->m_vector = vector;
	reader->m_offset = 0;
}

size_t gstd_VectorByteReader_ReadBytes(void *userdata, void *dest, size_t numBytes)
{
	zstdhl_VectorByteReader_t *reader = (zstdhl_VectorByteReader_t *)userdata;
	uint8_t *destBytes = dest;
	const uint8_t *srcBytes = ((const uint8_t *)reader->m_vector->m_data) + reader->m_offset;
	size_t bytesAvailable = reader->m_vector->m_count - reader->m_offset;
	size_t i = 0;

	if (numBytes > bytesAvailable)
		numBytes = bytesAvailable;

	for (i = 0; i < numBytes; i++)
		destBytes[i] = srcBytes[i];

	reader->m_offset += numBytes;

	return numBytes;
}


typedef struct gstd_VectorSequenceReader
{
	const zstdhl_Vector_t *m_vector;
	const zstdhl_Vector_t *m_bigNumVector;
	size_t m_offset;
} gstd_VectorSequenceReader_t;

typedef struct gstd_Sequence
{
	uint32_t m_litLength;
	uint32_t m_matchLength;

	size_t m_offsetNumBits;
	size_t m_offsetStart;

	zstdhl_OffsetType_t m_offsetType;
} gstd_Sequence_t;

void gstd_VectorSequenceReader_Init(gstd_VectorSequenceReader_t *reader, const zstdhl_Vector_t *vector, const zstdhl_Vector_t *bigNumVector)
{
	reader->m_vector = vector;
	reader->m_offset = 0;
	reader->m_bigNumVector = bigNumVector;
}

zstdhl_ResultCode_t gstd_VectorSequenceReader_GetNextSequence(void *userdata, zstdhl_SequenceDesc_t *sequence)
{
	gstd_VectorSequenceReader_t *reader = (gstd_VectorSequenceReader_t *)userdata;

	gstd_Sequence_t *srcSequence = ((gstd_Sequence_t *)reader->m_vector->m_data) + reader->m_offset;

	if (reader->m_offset == reader->m_vector->m_count)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	sequence->m_litLength = srcSequence->m_litLength;
	sequence->m_matchLength = srcSequence->m_matchLength;
	sequence->m_offsetType = srcSequence->m_offsetType;
	sequence->m_offsetValueBigNum = NULL;
	sequence->m_offsetValueNumBits = 0;

	if (srcSequence->m_offsetType == ZSTDHL_OFFSET_TYPE_SPECIFIED)
	{
		sequence->m_offsetValueBigNum = (uint32_t *)reader->m_bigNumVector->m_data + srcSequence->m_offsetStart;
		sequence->m_offsetValueNumBits = srcSequence->m_offsetNumBits;
	}

	reader->m_offset++;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t(*m_getNextSequence)(void *userdata, zstdhl_SequenceDesc_t *sequence);


typedef struct gstd_TranscodeState
{
	zstdhl_MemoryAllocatorObject_t m_alloc;

	zstdhl_EncBlockDesc_t m_encBlock;

	gstd_TranscodeFSETablePurpose_t m_fseTablePurpose;

	zstdhl_Vector_t m_literalsVector;
	zstdhl_VectorByteReader_t m_literalsReader;
	zstdhl_StreamSourceObject_t m_literalsReaderStreamSource;

	zstdhl_Vector_t m_seqVector;
	gstd_VectorSequenceReader_t m_seqReader;

	zstdhl_Vector_t m_seqOffsetsVector;

	zstdhl_Vector_t m_litLengthProbsVector;
	zstdhl_Vector_t m_offsetProbsVector;
	zstdhl_Vector_t m_matchLengthProbsVector;

	zstdhl_FSETableDef_t m_litLengthTable;
	zstdhl_FSETableDef_t m_offsetTable;
	zstdhl_FSETableDef_t m_matchLengthTable;

	gstd_EncoderState_t *m_enc;
} gstd_TranscodeState_t;

zstdhl_ResultCode_t gstd_TranscodeState_Init(gstd_TranscodeState_t *state, gstd_EncoderState_t *enc, const zstdhl_MemoryAllocatorObject_t *alloc)
{
	state->m_alloc.m_reallocFunc = alloc->m_reallocFunc;
	state->m_alloc.m_userdata = alloc->m_userdata;
	state->m_enc = enc;

	zstdhl_Vector_Init(&state->m_literalsVector, 1, alloc);
	zstdhl_Vector_Init(&state->m_seqVector, sizeof(gstd_Sequence_t), alloc);
	zstdhl_Vector_Init(&state->m_seqOffsetsVector, sizeof(uint32_t), alloc);
	zstdhl_Vector_Init(&state->m_litLengthProbsVector, sizeof(uint32_t), alloc);
	zstdhl_Vector_Init(&state->m_offsetProbsVector, sizeof(uint32_t), alloc);
	zstdhl_Vector_Init(&state->m_matchLengthProbsVector, sizeof(uint32_t), alloc);

	gstd_VectorByteReader_Init(&state->m_literalsReader, &state->m_literalsVector);

	state->m_literalsReaderStreamSource.m_readBytesFunc = gstd_VectorByteReader_ReadBytes;
	state->m_literalsReaderStreamSource.m_userdata = &state->m_literalsReader;

	gstd_VectorSequenceReader_Init(&state->m_seqReader, &state->m_seqVector, &state->m_seqOffsetsVector);

	state->m_encBlock.m_seqCollection.m_getNextSequence = gstd_VectorSequenceReader_GetNextSequence;
	state->m_encBlock.m_seqCollection.m_userdata = &state->m_seqReader;

	state->m_fseTablePurpose = GSTD_TRANSCODE_FSE_TABLE_PURPOSE_NONE;

	state->m_encBlock.m_autoBlockSizeFlag = 1;
	state->m_encBlock.m_autoLitSizeFlag = 1;
	state->m_encBlock.m_autoHuffmanStreamSizesFlags[0] = 1;
	state->m_encBlock.m_autoHuffmanStreamSizesFlags[1] = 1;
	state->m_encBlock.m_autoHuffmanStreamSizesFlags[2] = 1;
	state->m_encBlock.m_autoHuffmanStreamSizesFlags[3] = 1;

	state->m_encBlock.m_litSectionHeader.m_compressedSize = 0;
	state->m_encBlock.m_litSectionHeader.m_regeneratedSize = 0;
	state->m_encBlock.m_litSectionHeader.m_sectionType = ZSTDHL_LITERALS_SECTION_TYPE_RAW;

	state->m_encBlock.m_blockHeader.m_blockSize = 0;
	state->m_encBlock.m_litSectionDesc.m_decompressedLiteralsStream = &state->m_literalsReaderStreamSource;
	state->m_encBlock.m_litSectionDesc.m_huffmanStreamMode = ZSTDHL_HUFFMAN_STREAM_MODE_NONE;	// TODO
	state->m_encBlock.m_litSectionDesc.m_huffmanStreamSizes[0] = 0;
	state->m_encBlock.m_litSectionDesc.m_huffmanStreamSizes[1] = 0;
	state->m_encBlock.m_litSectionDesc.m_huffmanStreamSizes[2] = 0;
	state->m_encBlock.m_litSectionDesc.m_huffmanStreamSizes[3] = 0;
	state->m_encBlock.m_litSectionDesc.m_numValues = 0;

	state->m_encBlock.m_seqSectionDesc.m_literalLengthsMode = ZSTDHL_SEQ_COMPRESSION_MODE_INVALID;
	state->m_encBlock.m_seqSectionDesc.m_matchLengthsMode = ZSTDHL_SEQ_COMPRESSION_MODE_INVALID;
	state->m_encBlock.m_seqSectionDesc.m_offsetsMode = ZSTDHL_SEQ_COMPRESSION_MODE_INVALID;
	state->m_encBlock.m_seqSectionDesc.m_numSequences = 0;

	state->m_encBlock.m_huffmanTreeDesc.m_huffmanWeightFormat = ZSTDHL_HUFFMAN_WEIGHT_ENCODING_UNCOMPRESSED;

	state->m_encBlock.m_literalLengthsCompressionDesc.m_fseProbs = &state->m_litLengthTable;
	state->m_encBlock.m_literalLengthsCompressionDesc.m_rleByte = 0;

	state->m_encBlock.m_offsetsModeCompressionDesc.m_fseProbs = &state->m_offsetTable;
	state->m_encBlock.m_offsetsModeCompressionDesc.m_rleByte = 0;

	state->m_encBlock.m_matchLengthsCompressionDesc.m_fseProbs = &state->m_matchLengthTable;
	state->m_encBlock.m_offsetsModeCompressionDesc.m_rleByte = 0;

	return ZSTDHL_RESULT_OK;
}

void gstd_TranscodeState_Destroy(gstd_TranscodeState_t *state)
{
	zstdhl_Vector_Destroy(&state->m_literalsVector);
	zstdhl_Vector_Destroy(&state->m_seqVector);
	zstdhl_Vector_Destroy(&state->m_seqOffsetsVector);
	zstdhl_Vector_Destroy(&state->m_litLengthProbsVector);
	zstdhl_Vector_Destroy(&state->m_offsetProbsVector);
	zstdhl_Vector_Destroy(&state->m_matchLengthProbsVector);
}

static gstd_TranscodeFSETablePurpose_t gstd_SelectNextFSETablePurpose(const gstd_TranscodeState_t *state, gstd_TranscodeFSETablePurpose_t prevPurpose)
{
	switch (prevPurpose)
	{
	case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_NONE:
	case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_HUFFMAN_WEIGHTS:
		if (state->m_encBlock.m_seqSectionDesc.m_literalLengthsMode == ZSTDHL_SEQ_COMPRESSION_MODE_RLE || state->m_encBlock.m_seqSectionDesc.m_literalLengthsMode == ZSTDHL_SEQ_COMPRESSION_MODE_FSE)
			return GSTD_TRANSCODE_FSE_TABLE_PURPOSE_LIT_LENGTH;

		// Fallthrough
	case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_LIT_LENGTH:
		if (state->m_encBlock.m_seqSectionDesc.m_offsetsMode == ZSTDHL_SEQ_COMPRESSION_MODE_RLE || state->m_encBlock.m_seqSectionDesc.m_offsetsMode == ZSTDHL_SEQ_COMPRESSION_MODE_FSE)
			return GSTD_TRANSCODE_FSE_TABLE_PURPOSE_OFFSET;

		// Fallthrough
	case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_OFFSET:
		if (state->m_encBlock.m_seqSectionDesc.m_matchLengthsMode == ZSTDHL_SEQ_COMPRESSION_MODE_RLE || state->m_encBlock.m_seqSectionDesc.m_matchLengthsMode == ZSTDHL_SEQ_COMPRESSION_MODE_FSE)
			return GSTD_TRANSCODE_FSE_TABLE_PURPOSE_MATCH_LENGTH;

		// Fallthrough
	default:
		return GSTD_TRANSCODE_FSE_TABLE_PURPOSE_NONE;
	}
}


zstdhl_ResultCode_t gstd_TranscodeBlockHeader(gstd_TranscodeState_t *state, const zstdhl_BlockHeaderDesc_t *blockHeader)
{
	state->m_encBlock.m_blockHeader.m_blockType = blockHeader->m_blockType;
	state->m_encBlock.m_blockHeader.m_isLastBlock = blockHeader->m_isLastBlock;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_TranscodeLiteralsSectionHeader(gstd_TranscodeState_t *state, const zstdhl_LiteralsSectionHeader_t *litHeader)
{
	state->m_encBlock.m_litSectionHeader.m_regeneratedSize = litHeader->m_regeneratedSize;
	state->m_encBlock.m_litSectionHeader.m_sectionType = litHeader->m_sectionType;

	state->m_fseTablePurpose = GSTD_TRANSCODE_FSE_TABLE_PURPOSE_HUFFMAN_WEIGHTS;

	switch (litHeader->m_sectionType)
	{
	case ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN:
	case ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN_REUSE:
	case ZSTDHL_LITERALS_SECTION_TYPE_RAW:
	case ZSTDHL_LITERALS_SECTION_TYPE_RLE:
		break;
	default:
		return ZSTDHL_RESULT_INTERNAL_ERROR;
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_TranscodeLiteralsSection(gstd_TranscodeState_t *state, const zstdhl_LiteralsSectionDesc_t *litSection)
{
	size_t i = 0;
	size_t dataRemaining = litSection->m_numValues;

	zstdhl_Vector_Clear(&state->m_literalsVector);

	while (dataRemaining > 0)
	{
		uint8_t values[1024];
		size_t valuesToRead = 0;
		size_t valuesRead = 0;

		valuesToRead = dataRemaining;
		if (valuesToRead > sizeof(values))
			valuesToRead = sizeof(values);

		valuesRead = litSection->m_decompressedLiteralsStream->m_readBytesFunc(litSection->m_decompressedLiteralsStream->m_userdata, values, valuesToRead);

		if (valuesToRead != valuesRead)
			return ZSTDHL_RESULT_INPUT_FAILED;

		ZSTDHL_CHECKED(zstdhl_Vector_Append(&state->m_literalsVector, values, valuesRead));

		dataRemaining -= valuesRead;
	}

	state->m_literalsReader.m_offset = 0;
	state->m_encBlock.m_litSectionDesc.m_huffmanStreamMode = litSection->m_huffmanStreamMode;
	state->m_encBlock.m_litSectionDesc.m_numValues = litSection->m_numValues;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_TranscodeSequencesSection(gstd_TranscodeState_t *state, const zstdhl_SequencesSectionDesc_t *seqSection)
{
	state->m_encBlock.m_seqSectionDesc.m_literalLengthsMode = seqSection->m_literalLengthsMode;
	state->m_encBlock.m_seqSectionDesc.m_matchLengthsMode = seqSection->m_matchLengthsMode;
	state->m_encBlock.m_seqSectionDesc.m_offsetsMode = seqSection->m_offsetsMode;
	state->m_encBlock.m_seqSectionDesc.m_numSequences = seqSection->m_numSequences;

	state->m_fseTablePurpose = gstd_SelectNextFSETablePurpose(state, GSTD_TRANSCODE_FSE_TABLE_PURPOSE_NONE);

	zstdhl_Vector_Clear(&state->m_seqVector);
	state->m_seqReader.m_offset = 0;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_TranscodeBlockRLEData(gstd_TranscodeState_t *state, const zstdhl_BlockRLEDesc_t *rleDesc)
{
	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}

zstdhl_ResultCode_t gstd_TranscodeBlockUncompressedData(gstd_TranscodeState_t *state, const zstdhl_BlockUncompressedDesc_t *uncompressedDesc)
{
	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}

zstdhl_ResultCode_t gstd_TranscodeFSETableStart(gstd_TranscodeState_t *state, const zstdhl_FSETableStartDesc_t *tableStartDesc)
{
	switch (state->m_fseTablePurpose)
	{
	case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_HUFFMAN_WEIGHTS:
		state->m_encBlock.m_huffmanTreeDesc.m_weightTable.m_accuracyLog = tableStartDesc->m_accuracyLog;
		state->m_encBlock.m_huffmanTreeDesc.m_weightTable.m_numProbabilities = 0;
		state->m_encBlock.m_huffmanTreeDesc.m_weightTable.m_probabilities = state->m_encBlock.m_huffmanTreeDesc.m_weightTableProbabilities;
		break;
	case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_LIT_LENGTH:
		state->m_litLengthTable.m_accuracyLog = tableStartDesc->m_accuracyLog;
		zstdhl_Vector_Clear(&state->m_litLengthProbsVector);
		break;
	case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_OFFSET:
		state->m_offsetTable.m_accuracyLog = tableStartDesc->m_accuracyLog;
		zstdhl_Vector_Clear(&state->m_offsetProbsVector);
		break;
	case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_MATCH_LENGTH:
		state->m_matchLengthTable.m_accuracyLog = tableStartDesc->m_accuracyLog;
		zstdhl_Vector_Clear(&state->m_matchLengthProbsVector);
		break;
	default:
		return ZSTDHL_RESULT_INTERNAL_ERROR;
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_TranscodeFSETableEnd(gstd_TranscodeState_t *state)
{
	switch (state->m_fseTablePurpose)
	{
	case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_HUFFMAN_WEIGHTS:
		state->m_encBlock.m_huffmanTreeDesc.m_weightTable.m_probabilities = state->m_encBlock.m_huffmanTreeDesc.m_weightTableProbabilities;
		break;
	case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_LIT_LENGTH:
		state->m_litLengthTable.m_probabilities = (const uint32_t *)state->m_litLengthProbsVector.m_data;
		state->m_litLengthTable.m_numProbabilities = state->m_litLengthProbsVector.m_count;
		state->m_fseTablePurpose = gstd_SelectNextFSETablePurpose(state, state->m_fseTablePurpose);
		break;
	case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_OFFSET:
		state->m_offsetTable.m_probabilities = (const uint32_t *)state->m_offsetProbsVector.m_data;
		state->m_offsetTable.m_numProbabilities = state->m_offsetProbsVector.m_count;
		state->m_fseTablePurpose = gstd_SelectNextFSETablePurpose(state, state->m_fseTablePurpose);
		break;
	case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_MATCH_LENGTH:
		state->m_matchLengthTable.m_probabilities = (const uint32_t *)state->m_matchLengthProbsVector.m_data;
		state->m_matchLengthTable.m_numProbabilities = state->m_matchLengthProbsVector.m_count;
		state->m_fseTablePurpose = gstd_SelectNextFSETablePurpose(state, state->m_fseTablePurpose);
		break;

	default:
		return ZSTDHL_RESULT_INTERNAL_ERROR;
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_TranscodeFSETableProbability(gstd_TranscodeState_t *state, const zstdhl_ProbabilityDesc_t *probDesc)
{
	size_t i = 0;

	for (i = 0; i <= probDesc->m_repeatCount; i++)
	{
		switch (state->m_fseTablePurpose)
		{
		case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_HUFFMAN_WEIGHTS:
			if (state->m_encBlock.m_huffmanTreeDesc.m_weightTable.m_numProbabilities == 256)
				return ZSTDHL_RESULT_INTERNAL_ERROR;
			state->m_encBlock.m_huffmanTreeDesc.m_weightTableProbabilities[state->m_encBlock.m_huffmanTreeDesc.m_weightTable.m_numProbabilities++] = probDesc->m_prob;
			break;
		case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_LIT_LENGTH:
			ZSTDHL_CHECKED(zstdhl_Vector_Append(&state->m_litLengthProbsVector, &probDesc->m_prob, 1));
			break;
		case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_MATCH_LENGTH:
			ZSTDHL_CHECKED(zstdhl_Vector_Append(&state->m_matchLengthProbsVector, &probDesc->m_prob, 1));
			break;
		case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_OFFSET:
			ZSTDHL_CHECKED(zstdhl_Vector_Append(&state->m_offsetProbsVector, &probDesc->m_prob, 1));
			break;

		default:
			return ZSTDHL_RESULT_INTERNAL_ERROR;
		}
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_TranscodeSequenceRLEByte(gstd_TranscodeState_t *state, const uint8_t *rleByte)
{
	switch (state->m_fseTablePurpose)
	{
	case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_LIT_LENGTH:
		state->m_encBlock.m_literalLengthsCompressionDesc.m_rleByte = *rleByte;
		break;
	case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_OFFSET:
		state->m_encBlock.m_offsetsModeCompressionDesc.m_rleByte = *rleByte;
		break;
	case GSTD_TRANSCODE_FSE_TABLE_PURPOSE_MATCH_LENGTH:
		state->m_encBlock.m_matchLengthsCompressionDesc.m_rleByte = *rleByte;
		break;
	default:
		return ZSTDHL_RESULT_INTERNAL_ERROR;
	}

	state->m_fseTablePurpose = gstd_SelectNextFSETablePurpose(state, state->m_fseTablePurpose);

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_TranscodeWasteBits(gstd_TranscodeState_t *state, const zstdhl_WasteBitsDesc_t *wasteBits)
{
	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_TranscodeHuffmanTree(gstd_TranscodeState_t *state, const zstdhl_HuffmanTreeDesc_t *treeDesc)
{
	size_t i = 0;

	state->m_encBlock.m_huffmanTreeDesc.m_huffmanWeightFormat = treeDesc->m_huffmanWeightFormat;
	state->m_encBlock.m_huffmanTreeDesc.m_partialWeightDesc.m_numSpecifiedWeights = treeDesc->m_partialWeightDesc.m_numSpecifiedWeights;

	for (i = 0; i < treeDesc->m_partialWeightDesc.m_numSpecifiedWeights; i++)
		state->m_encBlock.m_huffmanTreeDesc.m_partialWeightDesc.m_specifiedWeights[i] = treeDesc->m_partialWeightDesc.m_specifiedWeights[i];

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_TranscodeSequence(gstd_TranscodeState_t *state, const zstdhl_SequenceDesc_t *seqDesc)
{
	gstd_Sequence_t seq;
	seq.m_litLength = seqDesc->m_litLength;
	seq.m_matchLength = seqDesc->m_matchLength;
	seq.m_offsetNumBits = 0;
	seq.m_offsetStart = 0;
	seq.m_offsetType = seqDesc->m_offsetType;

	if (seqDesc->m_offsetType == ZSTDHL_OFFSET_TYPE_SPECIFIED)
	{
		size_t numDWords = (seqDesc->m_offsetValueNumBits + 31u) / 32u;

		seq.m_offsetNumBits = seqDesc->m_offsetValueNumBits;
		seq.m_offsetStart = state->m_seqOffsetsVector.m_count;

		ZSTDHL_CHECKED(zstdhl_Vector_Append(&state->m_seqOffsetsVector, seqDesc->m_offsetValueBigNum, numDWords));
	}

	ZSTDHL_CHECKED(zstdhl_Vector_Append(&state->m_seqVector, &seq, 1));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_TranscodeBlockEnd(gstd_TranscodeState_t *state)
{
	ZSTDHL_CHECKED(gstd_Encoder_AddBlock(state->m_enc, &state->m_encBlock));

	zstdhl_Vector_Clear(&state->m_literalsVector);
	zstdhl_Vector_Clear(&state->m_seqVector);
	zstdhl_Vector_Clear(&state->m_seqOffsetsVector);

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t gstd_TranscodeFrameEnd(gstd_TranscodeState_t *state)
{
	return ZSTDHL_RESULT_NOT_YET_IMPLEMENTED;
}

zstdhl_ResultCode_t gstd_TranscodeElement(void *userdata, int elementType, const void *element)
{
	gstd_TranscodeState_t *state = (gstd_TranscodeState_t *)userdata;

	switch (elementType)
	{
	case ZSTDHL_ELEMENT_TYPE_FRAME_HEADER:
		return ZSTDHL_RESULT_OK;
	case ZSTDHL_ELEMENT_TYPE_BLOCK_HEADER:
		return gstd_TranscodeBlockHeader(state, element);
	case ZSTDHL_ELEMENT_TYPE_LITERALS_SECTION_HEADER:
		return gstd_TranscodeLiteralsSectionHeader(state, element);
	case ZSTDHL_ELEMENT_TYPE_LITERALS_SECTION:
		return gstd_TranscodeLiteralsSection(state, element);
	case ZSTDHL_ELEMENT_TYPE_SEQUENCES_SECTION:
		return gstd_TranscodeSequencesSection(state, element);
	case ZSTDHL_ELEMENT_TYPE_BLOCK_RLE_DATA:
		return gstd_TranscodeBlockRLEData(state, element);
	case ZSTDHL_ELEMENT_TYPE_BLOCK_UNCOMPRESSED_DATA:
		return gstd_TranscodeBlockUncompressedData(state, element);

	case ZSTDHL_ELEMENT_TYPE_FSE_TABLE_START:
		return gstd_TranscodeFSETableStart(state, element);
	case ZSTDHL_ELEMENT_TYPE_FSE_TABLE_END:
		return gstd_TranscodeFSETableEnd(state);
	case ZSTDHL_ELEMENT_TYPE_FSE_PROBABILITY:
		return gstd_TranscodeFSETableProbability(state, element);

	case ZSTDHL_ELEMENT_TYPE_SEQUENCE_RLE_BYTE:
		return gstd_TranscodeSequenceRLEByte(state, element);

	case ZSTDHL_ELEMENT_TYPE_WASTE_BITS:
		return gstd_TranscodeWasteBits(state, element);
	case ZSTDHL_ELEMENT_TYPE_HUFFMAN_TREE:
		return gstd_TranscodeHuffmanTree(state, element);

	case ZSTDHL_ELEMENT_TYPE_SEQUENCE:
		return gstd_TranscodeSequence(state, element);

	case ZSTDHL_ELEMENT_TYPE_BLOCK_END:
		return gstd_TranscodeBlockEnd(state);

	case ZSTDHL_ELEMENT_TYPE_FRAME_END:
		return gstd_TranscodeFrameEnd(state);

	default:
		return ZSTDHL_RESULT_INTERNAL_ERROR;
	}

	return ZSTDHL_RESULT_OK;
}


zstdhl_ResultCode_t gstd_Encoder_Transcode(gstd_EncoderState_t *encState, const zstdhl_StreamSourceObject_t *streamSource, const zstdhl_MemoryAllocatorObject_t *alloc)
{
	gstd_TranscodeState_t tcState;
	zstdhl_ResultCode_t resultCode = ZSTDHL_RESULT_OK;

	ZSTDHL_CHECKED(gstd_TranscodeState_Init(&tcState, encState, alloc));

	zstdhl_DisassemblyOutputObject_t disasmOutputObj;

	disasmOutputObj.m_reportDisassembledElementFunc = gstd_TranscodeElement;
	disasmOutputObj.m_userdata = &tcState;

	resultCode = zstdhl_Disassemble(streamSource, &disasmOutputObj, alloc);

	gstd_TranscodeState_Destroy(&tcState);

	return resultCode;
}
