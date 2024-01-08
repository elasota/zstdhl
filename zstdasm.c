/*
Copyright (c) 2023 Eric Lasota

This software is available under the terms of the MIT license
or the Apache License, Version 2.0.  For more information, see
the included LICENSE.txt file.
*/

#define _CRT_SECURE_NO_WARNINGS
#define _CRTDBG_MAP_ALLOC

#include "zstdhl.h"
#include "gstdenc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <crtdbg.h>

#define ZSTDASM_TOKEN_BLOCK_HEADER "blockHeader"

#define ZSTDASM_CHECKED(n)	\
	do\
	{\
		zstdhl_ResultCode_t result = (n);\
		if (result != ZSTDHL_RESULT_OK)\
			return result;\
	} while(0)

typedef enum AsmMode
{
	AsmMode_Asm,
	AsmMode_Disasm,
	AsmMode_GstdEnc,

	AsmMode_Invalid,
} AsmMode_t;

typedef struct DisasmState
{
	FILE *m_f;
	zstdhl_MemoryAllocatorObject_t m_alloc;
	zstdhl_Vector_t m_bigNumU32Vector;
	zstdhl_Vector_t m_bigNumDigitVector;
} DisasmState_t;

size_t ReadBytes(void *userdata, void *dest, size_t numBytes)
{
	return fread(dest, 1, numBytes, (FILE *)userdata);
}

typedef struct GstdEncodeState
{
	FILE *m_f;
} GstdEncodeState_t;

zstdhl_ResultCode_t WriteBytes(void *userdata, const void *data, size_t numBytes)
{
	GstdEncodeState_t *encodeState = (GstdEncodeState_t *)userdata;

	if (fwrite(data, 1, numBytes, encodeState->m_f) == numBytes)
		return ZSTDHL_RESULT_OK;

	return ZSTDHL_RESULT_OUTPUT_FAILED;
}

void *Realloc(void *userdata, void *ptr, size_t numBytes)
{
	void *result = NULL;

	if (ptr == NULL && numBytes == 0)
		return NULL;

	_ASSERTE(_CrtCheckMemory());

	result = realloc(ptr, numBytes);

	_ASSERTE(_CrtCheckMemory());

	return result;
}

zstdhl_ResultCode_t WriteBuffer(DisasmState_t *dstate, const void *data, size_t len)
{
	size_t numWritten = fwrite(data, 1, len, dstate->m_f);
	if (numWritten != len)
		return ZSTDHL_RESULT_OUTPUT_FAILED;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteString(DisasmState_t *dstate, const char *str)
{
	size_t len = strlen(str);

	return WriteBuffer(dstate, str, len);
}

#define CREATE_INTEGER_WRITE_WRITE_FUNC(funcName, type)	\
zstdhl_ResultCode_t funcName(DisasmState_t *dstate, type v)\
{\
	char chars[sizeof(v) * 8 / 3 + 2];\
	char *outChar = chars + sizeof(chars) - 1;\
	*outChar = '\0';\
	if (v == 0)\
	{\
		outChar--;\
		*outChar = '0';\
	}\
	else\
	{\
		do\
		{\
			size_t digit = v % 10u;\
			outChar--;\
			*outChar = (char)('0' + digit);\
			v /= 10u;\
		} while (v != 0);\
	}\
	return WriteString(dstate, outChar);\
}

CREATE_INTEGER_WRITE_WRITE_FUNC(WriteSize, size_t)
CREATE_INTEGER_WRITE_WRITE_FUNC(WriteU8, uint8_t)
CREATE_INTEGER_WRITE_WRITE_FUNC(WriteU16, uint16_t)
CREATE_INTEGER_WRITE_WRITE_FUNC(WriteU32, uint32_t)
CREATE_INTEGER_WRITE_WRITE_FUNC(WriteU64, uint64_t)

zstdhl_ResultCode_t WriteBigNum(DisasmState_t *dstate, uint32_t *dwords, size_t numBits)
{
	size_t numWords = (numBits + 15u) / 16u;
	zstdhl_Vector_t *digitVector = &dstate->m_bigNumDigitVector;
	char terminator = '\0';

	zstdhl_Vector_Clear(digitVector);

	while (numWords > 0)
	{
		uint32_t remainder = 0;
		char c = '0';

		if (numWords <= 2)
		{
			remainder = dwords[0] % 10u;
			dwords[0] /= 10u;

			if (dwords[0] == 0)
				numWords = 0;
		}
		else
		{
			// Long divide by 10 and extract remainder
			size_t wordIndex = numWords - 1;

			for (;;)
			{
				uint32_t dword = dwords[wordIndex / 2];
				uint32_t word = (wordIndex & 1) ? (dword >> 16) : (dword & 0xffffu);

				uint32_t wordWithLongDivRemainder = word | (remainder << 16);

				word = wordWithLongDivRemainder / 10u;
				remainder = wordWithLongDivRemainder % 10u;

				if (wordIndex & 1)
					dword = ((dword & 0xffffu) | (word << 16));
				else
					dword = ((dword & 0xffff0000u) | word);

				dwords[wordIndex / 2] = dword;

				if (numWords > 0 && wordIndex == numWords - 1 && word == 0)
					numWords--;

				if (wordIndex == 0)
					break;

				wordIndex--;
			}
		}

		c += (char)remainder;

		ZSTDASM_CHECKED(zstdhl_Vector_Append(digitVector, &c, 1));
	}

	if (digitVector->m_count == 0)
	{
		char c = '0';
		ZSTDASM_CHECKED(zstdhl_Vector_Append(digitVector, &c, 1));
	}
	else
	{
		size_t i = 0;
		size_t numDigits = digitVector->m_count;
		char *chars = (char *)digitVector->m_data;

		for (i = 0; i < numDigits / 2u; i++)
		{
			char temp = chars[numDigits - 1 - i];
			chars[numDigits - 1 - i] = chars[i];
			chars[i] = temp;
		}
	}

	ZSTDASM_CHECKED(WriteBuffer(dstate, digitVector->m_data, digitVector->m_count));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteChar(DisasmState_t *dstate, int16_t ch)
{
	char chars[10];

	if (ch < 0)
		ch += 256;

	if (ch == '\\')
	{
		chars[0] = '\\';
		chars[1] = '\\';
		return WriteBuffer(dstate, chars, 2);
	}

	if (ch == '\"')
	{
		chars[0] = '\\';
		chars[1] = '\"';
		return WriteBuffer(dstate, chars, 2);
	}

	if (ch == '\'')
	{
		chars[0] = '\\';
		chars[1] = '\'';
		return WriteBuffer(dstate, chars, 2);
	}

	if (ch >= 32 && ch <= 126)
	{
		chars[0] = (char)ch;
		return WriteBuffer(dstate, chars, 1);
	}
	else
	{
		const char *nibbles = "0123456789abcdef";

		chars[0] = '\\';
		chars[1] = 'x';
		chars[2] = nibbles[(ch >> 4) & 0xf];
		chars[3] = nibbles[ch & 0xf];
		return WriteBuffer(dstate, chars, 4);
	}
}

zstdhl_ResultCode_t WriteCommentedDataBlock(DisasmState_t *dstate, size_t numValues, const zstdhl_StreamSourceObject_t *streamSource)
{
	size_t i = 0;
	size_t col = 0;
	uint8_t colBytes[16];
	char commentChars[sizeof(colBytes)];
	size_t numColumns = sizeof(colBytes);

	for (size_t i = 0; i < numValues; i += numColumns)
	{
		size_t bytesToRead = numValues - i;
		size_t bytesRead = 0;

		if (bytesToRead > numColumns)
			bytesToRead = numColumns;

		bytesRead = streamSource->m_readBytesFunc(streamSource->m_userdata, colBytes, bytesToRead);

		if (bytesRead != bytesToRead)
			return ZSTDHL_RESULT_INTERNAL_ERROR;

		for (col = 0; col < bytesRead; col++)
		{
			const char *hexStr = "0123456789abcdef";
			uint8_t byte = colBytes[col];
			char byteChars[3];


			if (byte < 32 || byte > 126)
				commentChars[col] = '.';
			else
				commentChars[col] = (char)byte;

			byteChars[0] = hexStr[(byte >> 4) & 0xf];
			byteChars[1] = hexStr[byte & 0xf];
			byteChars[2] = ' ';

			ZSTDASM_CHECKED(WriteBuffer(dstate, byteChars, 3));
		}

		for (col = bytesRead; col < numColumns; col++)
		{
			ZSTDASM_CHECKED(WriteString(dstate, "   "));
		}
		ZSTDASM_CHECKED(WriteString(dstate, "    ; "));
		ZSTDASM_CHECKED(WriteBuffer(dstate, commentChars, bytesRead));
		ZSTDASM_CHECKED(WriteString(dstate, "\n"));
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteFrameHeader(DisasmState_t *dstate, const zstdhl_FrameHeaderDesc_t *element)
{
	ZSTDASM_CHECKED(WriteString(dstate, "frameHeader windowSize "));
	ZSTDASM_CHECKED(WriteU64(dstate, element->m_windowSize));

	if (element->m_haveFrameContentSize)
	{
		ZSTDASM_CHECKED(WriteString(dstate, " frameContentSize "));
		ZSTDASM_CHECKED(WriteU64(dstate, element->m_frameContentSize));
	}

	if (element->m_haveDictionaryID)
	{
		ZSTDASM_CHECKED(WriteString(dstate, " dictionaryID "));
		ZSTDASM_CHECKED(WriteU32(dstate, element->m_dictionaryID));
	}

	if (element->m_haveContentChecksum)
	{
		ZSTDASM_CHECKED(WriteString(dstate, " checksum"));
	}

	ZSTDASM_CHECKED(WriteString(dstate, "\n"));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteBlockHeader(DisasmState_t *dstate, const zstdhl_BlockHeaderDesc_t *element)
{
	ZSTDASM_CHECKED(WriteString(dstate, "blockHeader"));

	if (element->m_isLastBlock)
	{
		ZSTDASM_CHECKED(WriteString(dstate, " last"));
	}

	switch (element->m_blockType)
	{
	case ZSTDHL_BLOCK_TYPE_RAW:
		ZSTDASM_CHECKED(WriteString(dstate, " raw"));
		break;
	case ZSTDHL_BLOCK_TYPE_RLE:
		ZSTDASM_CHECKED(WriteString(dstate, " rle"));
		break;
	case ZSTDHL_BLOCK_TYPE_COMPRESSED:
		ZSTDASM_CHECKED(WriteString(dstate, " compressed"));
		break;
	default:
		return ZSTDHL_RESULT_INTERNAL_ERROR;
	}

	ZSTDASM_CHECKED(WriteString(dstate, " size "));
	ZSTDASM_CHECKED(WriteU32(dstate, element->m_blockSize));
	ZSTDASM_CHECKED(WriteString(dstate, "\n"));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteLiteralsSectionHeader(DisasmState_t *dstate, const zstdhl_LiteralsSectionHeader_t *element)
{
	ZSTDASM_CHECKED(WriteString(dstate, "literals"));

	switch (element->m_sectionType)
	{
	case ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN:
		ZSTDASM_CHECKED(WriteString(dstate, " huffman"));
		break;
	case ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN_REUSE:
		ZSTDASM_CHECKED(WriteString(dstate, " huffmanReuse"));
		break;
	case ZSTDHL_LITERALS_SECTION_TYPE_RAW:
		ZSTDASM_CHECKED(WriteString(dstate, " raw"));
		break;
	case ZSTDHL_LITERALS_SECTION_TYPE_RLE:
		ZSTDASM_CHECKED(WriteString(dstate, " rle"));
		break;
	default:
		return ZSTDHL_RESULT_INTERNAL_ERROR;
	}

	ZSTDASM_CHECKED(WriteString(dstate, " regeneratedSize "));
	ZSTDASM_CHECKED(WriteU32(dstate, element->m_regeneratedSize));

	if (element->m_sectionType == ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN || element->m_sectionType == ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN_REUSE)
	{
		ZSTDASM_CHECKED(WriteString(dstate, " compressedSize "));
		ZSTDASM_CHECKED(WriteU32(dstate, element->m_compressedSize));
	}
	ZSTDASM_CHECKED(WriteString(dstate, "\n"));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteLiteralsSection(DisasmState_t *dstate, const zstdhl_LiteralsSectionDesc_t *element)
{
	ZSTDASM_CHECKED(WriteString(dstate, "literalValues\n"));
	ZSTDASM_CHECKED(WriteCommentedDataBlock(dstate, element->m_numValues, element->m_decompressedLiteralsStream));
	ZSTDASM_CHECKED(WriteString(dstate, "endLiteralValues\n"));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteSequencesSection(DisasmState_t *dstate, const zstdhl_SequencesSectionDesc_t *element)
{
	const char *modeNames[3] = { " litLengthMode ", " matchLengthMode ", " offsetsMode " };
	zstdhl_SequencesCompressionMode_t seqModes[3] = { element->m_literalLengthsMode, element->m_matchLengthsMode, element->m_offsetsMode };
	int i = 0;

	ZSTDASM_CHECKED(WriteString(dstate, "sequences"));
	for (i = 0; i < 3; i++)
	{
		ZSTDASM_CHECKED(WriteString(dstate, modeNames[i]));

		switch (seqModes[i])
		{
		case ZSTDHL_SEQ_COMPRESSION_MODE_FSE:
			ZSTDASM_CHECKED(WriteString(dstate, "fse"));
			break;
		case ZSTDHL_SEQ_COMPRESSION_MODE_RLE:
			ZSTDASM_CHECKED(WriteString(dstate, "rle"));
			break;
		case ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED:
			ZSTDASM_CHECKED(WriteString(dstate, "predef"));
			break;
		case ZSTDHL_SEQ_COMPRESSION_MODE_REUSE:
			ZSTDASM_CHECKED(WriteString(dstate, "reuse"));
			break;
		default:
			return ZSTDHL_RESULT_INTERNAL_ERROR;
		}
	}

	ZSTDASM_CHECKED(WriteString(dstate, " numSequences "));
	ZSTDASM_CHECKED(WriteU32(dstate, element->m_numSequences));

	ZSTDASM_CHECKED(WriteString(dstate, "\n"));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteBlockRLEData(DisasmState_t *dstate, const zstdhl_BlockRLEDesc_t *element)
{
	zstdhl_MemBufferStreamSource_t memSource;
	zstdhl_StreamSourceObject_t memSourceObj;

	zstdhl_MemBufferStreamSource_Init(&memSource, &element->m_value, 1);

	memSourceObj.m_readBytesFunc = zstdhl_MemBufferStreamSource_ReadBytes;
	memSourceObj.m_userdata = &memSource;

	ZSTDASM_CHECKED(WriteCommentedDataBlock(dstate, 1, &memSourceObj));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteBlockUncompressedData(DisasmState_t *dstate, const zstdhl_BlockUncompressedDesc_t *element)
{
	zstdhl_MemBufferStreamSource_t memSource;
	zstdhl_StreamSourceObject_t memSourceObj;

	zstdhl_MemBufferStreamSource_Init(&memSource, element->m_data, element->m_size);

	memSourceObj.m_readBytesFunc = zstdhl_MemBufferStreamSource_ReadBytes;
	memSourceObj.m_userdata = &memSource;

	ZSTDASM_CHECKED(WriteCommentedDataBlock(dstate, element->m_size, &memSourceObj));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteFSETableStart(DisasmState_t *dstate, const zstdhl_FSETableStartDesc_t *element)
{
	ZSTDASM_CHECKED(WriteString(dstate, "fseTableStart accuracyLog "));
	ZSTDASM_CHECKED(WriteU8(dstate, element->m_accuracyLog));
	ZSTDASM_CHECKED(WriteString(dstate, "\n"));
	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteFSETableEnd(DisasmState_t *dstate)
{
	ZSTDASM_CHECKED(WriteString(dstate, "fseTableEnd\n"));
	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteBlockEnd(DisasmState_t *dstate)
{
	ZSTDASM_CHECKED(WriteString(dstate, "blockEnd\n"));
	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteFrameEnd(DisasmState_t *dstate)
{
	ZSTDASM_CHECKED(WriteString(dstate, "frameEnd\n"));
	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteFSETableProbability(DisasmState_t *dstate, const zstdhl_ProbabilityDesc_t *element)
{
	ZSTDASM_CHECKED(WriteU32(dstate, element->m_prob));
	if (element->m_prob == 0)
	{
		ZSTDASM_CHECKED(WriteString(dstate, " repeat "));
		ZSTDASM_CHECKED(WriteSize(dstate, element->m_repeatCount));
	}
	ZSTDASM_CHECKED(WriteString(dstate, "\n"));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteSequenceRLEByte(DisasmState_t *dstate, const uint8_t *b)
{
	ZSTDASM_CHECKED(WriteChar(dstate, *b));
	ZSTDASM_CHECKED(WriteString(dstate, "\n"));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteWasteBits(DisasmState_t *dstate, const zstdhl_WasteBitsDesc_t *element)
{
	if (element->m_numBits > 0 && element->m_bits != 0)
	{
		ZSTDASM_CHECKED(WriteString(dstate, "wasteBits "));
		ZSTDASM_CHECKED(WriteU8(dstate, element->m_numBits));

		ZSTDASM_CHECKED(WriteString(dstate, " value "));
		ZSTDASM_CHECKED(WriteU8(dstate, element->m_bits));

		ZSTDASM_CHECKED(WriteString(dstate, "\n"));
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteHuffmanTree(DisasmState_t *dstate, const zstdhl_HuffmanTreeDesc_t *element)
{
	ZSTDASM_CHECKED(WriteString(dstate, "huffmanTableStart"));
	uint16_t i = 0;
	uint8_t numWeightDescs = element->m_partialWeightDesc.m_numSpecifiedWeights;

	for (i = 0; i <= numWeightDescs; i++)
	{
		if (i != numWeightDescs && element->m_partialWeightDesc.m_specifiedWeights[i] == 0)
			continue;

		ZSTDASM_CHECKED(WriteString(dstate, "'"));
		ZSTDASM_CHECKED(WriteChar(dstate, i));
		ZSTDASM_CHECKED(WriteString(dstate, "' "));

		if (i == numWeightDescs)
		{
			ZSTDASM_CHECKED(WriteString(dstate, "terminal"));
		}
		else
		{
			ZSTDASM_CHECKED(WriteU8(dstate, element->m_partialWeightDesc.m_specifiedWeights[i]));
		}
		ZSTDASM_CHECKED(WriteString(dstate, "\n"));
	}
	ZSTDASM_CHECKED(WriteString(dstate, "huffmanTableEnd\n"));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t WriteSequence(DisasmState_t *dstate, const zstdhl_SequenceDesc_t *element)
{
	ZSTDASM_CHECKED(WriteString(dstate, "lit "));
	ZSTDASM_CHECKED(WriteU32(dstate, element->m_litLength));

	switch (element->m_offsetType)
	{
	case ZSTDHL_OFFSET_TYPE_REPEAT_1:
		ZSTDASM_CHECKED(WriteString(dstate, " offs rep1"));
		break;
	case ZSTDHL_OFFSET_TYPE_REPEAT_1_MINUS_1:
		ZSTDASM_CHECKED(WriteString(dstate, " offs rep1minus1"));
		break;
	case ZSTDHL_OFFSET_TYPE_REPEAT_2:
		ZSTDASM_CHECKED(WriteString(dstate, " offs rep2"));
		break;
	case ZSTDHL_OFFSET_TYPE_REPEAT_3:
		ZSTDASM_CHECKED(WriteString(dstate, " offs rep3"));
		break;
	case ZSTDHL_OFFSET_TYPE_SPECIFIED:
		ZSTDASM_CHECKED(WriteString(dstate, " offs "));
		ZSTDASM_CHECKED(WriteBigNum(dstate, element->m_offsetValueBigNum, element->m_offsetValueNumBits));
		break;
	default:
		return ZSTDHL_RESULT_INTERNAL_ERROR;
	}

	ZSTDASM_CHECKED(WriteString(dstate, " match "));
	ZSTDASM_CHECKED(WriteU32(dstate, element->m_matchLength));

	ZSTDASM_CHECKED(WriteString(dstate, "\n"));

	return ZSTDHL_RESULT_OK;
}


zstdhl_ResultCode_t DisassembleElement(void *userdata, int elementType, const void *element)
{
	DisasmState_t *dstate = (DisasmState_t *)userdata;

	switch (elementType)
	{
	case ZSTDHL_ELEMENT_TYPE_FRAME_HEADER:
		return WriteFrameHeader(dstate, element);
	case ZSTDHL_ELEMENT_TYPE_BLOCK_HEADER:
		return WriteBlockHeader(dstate, element);
	case ZSTDHL_ELEMENT_TYPE_LITERALS_SECTION_HEADER:
		return WriteLiteralsSectionHeader(dstate, element);
	case ZSTDHL_ELEMENT_TYPE_LITERALS_SECTION:
		return WriteLiteralsSection(dstate, element);
	case ZSTDHL_ELEMENT_TYPE_SEQUENCES_SECTION:
		return WriteSequencesSection(dstate, element);
	case ZSTDHL_ELEMENT_TYPE_BLOCK_RLE_DATA:
		return WriteBlockRLEData(dstate, element);
	case ZSTDHL_ELEMENT_TYPE_BLOCK_UNCOMPRESSED_DATA:
		return WriteBlockUncompressedData(dstate, element);

	case ZSTDHL_ELEMENT_TYPE_FSE_TABLE_START:
		return WriteFSETableStart(dstate, element);
	case ZSTDHL_ELEMENT_TYPE_FSE_TABLE_END:
		return WriteFSETableEnd(dstate);
	case ZSTDHL_ELEMENT_TYPE_FSE_PROBABILITY:
		return WriteFSETableProbability(dstate, element);
	case ZSTDHL_ELEMENT_TYPE_SEQUENCE_RLE_BYTE:
		return WriteSequenceRLEByte(dstate, element);

	case ZSTDHL_ELEMENT_TYPE_WASTE_BITS:
		return WriteWasteBits(dstate, element);
	case ZSTDHL_ELEMENT_TYPE_HUFFMAN_TREE:
		return WriteHuffmanTree(dstate, element);

	case ZSTDHL_ELEMENT_TYPE_SEQUENCE:
		return WriteSequence(dstate, element);

	case ZSTDHL_ELEMENT_TYPE_BLOCK_END:
		return WriteBlockEnd(dstate);

	case ZSTDHL_ELEMENT_TYPE_FRAME_END:
		return WriteFrameEnd(dstate);

	default:
		return ZSTDHL_RESULT_INTERNAL_ERROR;
	}

	return ZSTDHL_RESULT_OK;
}


int main(int argc, const char **argv)
{
	const char *modeStr = NULL;
	AsmMode_t asmMode = AsmMode_Invalid;
	FILE *inputF = NULL;
	FILE *outputF = NULL;
	zstdhl_ResultCode_t result = ZSTDHL_RESULT_OK;
	zstdhl_DisassemblyOutputObject_t disasmObject;
	zstdhl_StreamSourceObject_t streamSourceObj;
	zstdhl_MemoryAllocatorObject_t memAllocObj;

	if (argc != 4)
	{
		fprintf(stderr, "Usage: zstdasm <mode> <input> <output>\n");
		fprintf(stderr, "Commands:\n");
		fprintf(stderr, "    asm - Converts text input into Zstd stream\n");
		fprintf(stderr, "    disasm - Converts Zstd stream into text input\n");
		fprintf(stderr, "    gstdenc - Converts Zstd stream into Gstd stream\n");
		return -1;
	}

	modeStr = argv[1];

	if (!strcmp(modeStr, "asm"))
		asmMode = AsmMode_Asm;
	else if (!strcmp(modeStr, "disasm"))
		asmMode = AsmMode_Disasm;
	else if (!strcmp(modeStr, "gstdenc"))
		asmMode = AsmMode_GstdEnc;
	else
	{
		fprintf(stderr, "Invalid mode\n");
		return -1;
	}


	inputF = fopen(argv[2], "rb");

	if (!inputF)
	{
		fprintf(stderr, "Couldn't open input file\n");
		return -1;
	}

	outputF = fopen(argv[3], "wb");

	if (!outputF)
	{
		fprintf(stderr, "Couldn't open input file\n");
		return -1;
	}

	if (asmMode == AsmMode_Disasm)
	{
		DisasmState_t disasmState;

		memAllocObj.m_reallocFunc = Realloc;
		memAllocObj.m_userdata = NULL;

		disasmState.m_f = outputF;
		disasmState.m_alloc.m_reallocFunc = memAllocObj.m_reallocFunc;
		disasmState.m_alloc.m_userdata = memAllocObj.m_userdata;

		zstdhl_Vector_Init(&disasmState.m_bigNumU32Vector, sizeof(uint32_t), &memAllocObj);
		zstdhl_Vector_Init(&disasmState.m_bigNumDigitVector, sizeof(char), &memAllocObj);

		disasmObject.m_userdata = &disasmState;
		disasmObject.m_reportDisassembledElementFunc = DisassembleElement;

		streamSourceObj.m_readBytesFunc = ReadBytes;
		streamSourceObj.m_userdata = inputF;

		memAllocObj.m_reallocFunc = Realloc;
		memAllocObj.m_userdata = NULL;

		result = zstdhl_Disassemble(&streamSourceObj, &disasmObject, &memAllocObj);

		zstdhl_Vector_Destroy(&disasmState.m_bigNumU32Vector);
		zstdhl_Vector_Destroy(&disasmState.m_bigNumDigitVector);
	}

	if (asmMode == AsmMode_GstdEnc)
	{
		gstd_EncoderState_t *encState;
		zstdhl_EncoderOutputObject_t encOut;
		GstdEncodeState_t encOutObject;
		uint8_t maxOffsetCode = gstd_ComputeMaxOffsetExtraBits(128 * 1024);

		memAllocObj.m_reallocFunc = Realloc;
		memAllocObj.m_userdata = NULL;

		encOutObject.m_f = outputF;

		encOut.m_writeBitstreamFunc = WriteBytes;
		encOut.m_userdata = &encOutObject;

		result = gstd_Encoder_Create(&encOut, 32, maxOffsetCode , &memAllocObj, &encState);
		if (result == ZSTDHL_RESULT_OK)
		{
			streamSourceObj.m_readBytesFunc = ReadBytes;
			streamSourceObj.m_userdata = inputF;

			result = gstd_Encoder_Transcode(encState, &streamSourceObj, &memAllocObj);

			gstd_Encoder_Destroy(encState);
		}
	}

	fclose(inputF);
	fclose(outputF);

	if (result != ZSTDHL_RESULT_OK)
	{
		fprintf(stderr, "Failed with error code %i", (int)(result));
		return -1;
	}

	return 0;
}
