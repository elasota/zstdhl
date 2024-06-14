/*
Copyright (c) 2023 Eric Lasota

This software is available under the terms of the MIT license
or the Apache License, Version 2.0.  For more information, see
the included LICENSE.txt file.
*/
#include "zstdhl.h"
#include "zstdhl_util.h"
#include "zstdhl_internal.h"

#include <stdio.h>

#define ZSTDHL_DEFLATECONV_MAX_CODE_LENGTH_CODES	19
#define ZSTDHL_DEFLATECONV_MAX_LIT_LENGTH_CODES		288	// Actually 286 but static trees use 288
#define ZSTDHL_DEFLATECONV_MAX_DIST_CODES			32	// Actually 30 but static trees use 32

#define ZSTDHL_DEFLATECONV_MAX_ANY_CODES			288

#define ZSTDHL_DEFLATECONV_MAX_CODE_LENGTH			15

static const int kLog2Shift = 27;

static const uint32_t kLog2Table[513] =
{
	0, 0, 134217728, 212730065, 268435456, 311643913, 346947793, 376796799,
	402653184, 425460131, 445861641, 464317052, 481165521, 496664611, 511014527, 524373979,
	536870912, 548609975, 559677859, 570147179, 580079369, 589526865, 598534780, 607142208,
	615383249, 623287826, 630882339, 638190197, 645232255, 652027171, 658591707, 664940972,
	671088640, 677047117, 682827703, 688440712, 693895587, 699200994, 704364907, 709394677,
	714297097, 719078457, 723744593, 728300926, 732752508, 737104045, 741359936, 745524295,
	749600977, 753593598, 757505554, 761340041, 765100067, 768788469, 772407925, 775960965,
	779449983, 782877245, 786244899, 789554984, 792809435, 796010090, 799158700, 802256930,
	805306368, 808308525, 811264845, 814176708, 817045431, 819872274, 822658440, 825405086,
	828113315, 830784189, 833418722, 836017892, 838582635, 841113851, 843612405, 846079129,
	848514825, 850920263, 853296185, 855643308, 857962321, 860253889, 862518654, 864757237,
	866970236, 869158228, 871321773, 873461410, 875577664, 877671038, 879742023, 881791093,
	883818705, 885825306, 887811326, 889777183, 891723282, 893650017, 895557769, 897446909,
	899317795, 901170778, 903006197, 904824382, 906625653, 908410322, 910178693, 911931060,
	913667711, 915388924, 917094973, 918786121, 920462627, 922124743, 923772712, 925406775,
	927027163, 928634104, 930227818, 931808523, 933376428, 934931740, 936474658, 938005380,
	939524096, 941030992, 942526253, 944010055, 945482573, 946943978, 948394436, 949834110,
	951263159, 952681739, 954090002, 955488096, 956876168, 958254361, 959622814, 960981663,
	962331043, 963671085, 965001917, 966323664, 967636450, 968940396, 970235620, 971522238,
	972800363, 974070107, 975331579, 976584886, 977830133, 979067423, 980296857, 981518535,
	982732553, 983939007, 985137991, 986329596, 987513913, 988691031, 989861036, 991024014,
	992180049, 993329223, 994471617, 995607311, 996736382, 997858909, 998974965, 1000084626,
	1001187964, 1002285050, 1003375956, 1004460750, 1005539501, 1006612275, 1007679138, 1008740156,
	1009795392, 1010844908, 1011888766, 1012927027, 1013959751, 1014986996, 1016008821, 1017025281,
	1018036433, 1019042333, 1020043034, 1021038590, 1022029054, 1023014477, 1023994911, 1024970406,
	1025941010, 1026906774, 1027867745, 1028823971, 1029775497, 1030722371, 1031664637, 1032602339,
	1033535523, 1034464231, 1035388506, 1036308390, 1037223925, 1038135151, 1039042110, 1039944840,
	1040843381, 1041737772, 1042628050, 1043514254, 1044396421, 1045274587, 1046148788, 1047019060,
	1047885439, 1048747958, 1049606652, 1050461556, 1051312701, 1052160121, 1053003849, 1053843917,
	1054680355, 1055513196, 1056342471, 1057168209, 1057990440, 1058809195, 1059624503, 1060436392,
	1061244891, 1062050028, 1062851832, 1063650329, 1064445546, 1065237512, 1066026251, 1066811791,
	1067594156, 1068373374, 1069149468, 1069922464, 1070692386, 1071459260, 1072223108, 1072983955,
	1073741824, 1074496738, 1075248720, 1075997794, 1076743981, 1077487303, 1078227783, 1078965442,
	1079700301, 1080432383, 1081161706, 1081888294, 1082612164, 1083333339, 1084051838, 1084767681,
	1085480887, 1086191476, 1086899467, 1087604878, 1088307730, 1089008039, 1089705824, 1090401104,
	1091093896, 1091784219, 1092472089, 1093157524, 1093840542, 1094521158, 1095199391, 1095875257,
	1096548771, 1097219951, 1097888813, 1098555372, 1099219645, 1099881646, 1100541392, 1101198898,
	1101854178, 1102507249, 1103158124, 1103806819, 1104453348, 1105097726, 1105739966, 1106380083,
	1107018091, 1107654004, 1108287835, 1108919598, 1109549307, 1110176974, 1110802614, 1111426238,
	1112047861, 1112667494, 1113285151, 1113900844, 1114514585, 1115126388, 1115736263, 1116344223,
	1116950281, 1117554448, 1118156735, 1118757155, 1119355719, 1119952438, 1120547324, 1121140388,
	1121731641, 1122321094, 1122908759, 1123494645, 1124078764, 1124661126, 1125241742, 1125820622,
	1126397777, 1126973216, 1127546951, 1128118990, 1128689345, 1129258024, 1129825039, 1130390397,
	1130954110, 1131516187, 1132076637, 1132635469, 1133192693, 1133748318, 1134302354, 1134854809,
	1135405692, 1135955012, 1136502778, 1137048999, 1137593684, 1138136840, 1138678478, 1139218604,
	1139757229, 1140294359, 1140830003, 1141364169, 1141896866, 1142428102, 1142957884, 1143486221,
	1144013120, 1144538589, 1145062636, 1145585268, 1146106494, 1146626321, 1147144755, 1147661806,
	1148177479, 1148691783, 1149204724, 1149716310, 1150226549, 1150735446, 1151243009, 1151749245,
	1152254161, 1152757764, 1153260061, 1153761058, 1154260762, 1154759180, 1155256318, 1155752184,
	1156246782, 1156740121, 1157232205, 1157723043, 1158212639, 1158701001, 1159188134, 1159674044,
	1160158738, 1160642222, 1161124502, 1161605584, 1162085473, 1162564176, 1163041699, 1163518046,
	1163993225, 1164467241, 1164940099, 1165411805, 1165882365, 1166351784, 1166820067, 1167287221,
	1167753251, 1168218162, 1168681959, 1169144648, 1169606234, 1170066722, 1170526118, 1170984427,
	1171441653, 1171897802, 1172352879, 1172806890, 1173259838, 1173711729, 1174162568, 1174612360,
	1175061109, 1175508821, 1175955500, 1176401151, 1176845778, 1177289387, 1177731982, 1178173568,
	1178614149, 1179053730, 1179492315, 1179929909, 1180366516, 1180802141, 1181236788, 1181670462,
	1182103167, 1182534907, 1182965686, 1183395509, 1183824380, 1184252304, 1184679284, 1185105324,
	1185530429, 1185954603, 1186377849, 1186800173, 1187221577, 1187642066, 1188061645, 1188480316,
	1188898083, 1189314952, 1189730924, 1190146005, 1190560199, 1190973508, 1191385937, 1191797489,
	1192208168, 1192617978, 1193026923, 1193435006, 1193842231, 1194248601, 1194654120, 1195058791,
	1195462619, 1195865606, 1196267756, 1196669073, 1197069560, 1197469220, 1197868057, 1198266074,
	1198663274, 1199059662, 1199455240, 1199850011, 1200243979, 1200637147, 1201029519, 1201421097,
	1201811884, 1202201885, 1202591102, 1202979538, 1203367196, 1203754080, 1204140192, 1204525536,
	1204910114, 1205293931, 1205676988, 1206059288, 1206440836, 1206821633, 1207201683, 1207580988,
	1207959552
};

typedef struct zstdhl_DeflateConv_HuffmanTableEntry
{
	uint16_t m_length : 4;
	uint16_t m_symbol : 9;
	uint16_t m_needsL2Bit : 1;
	uint16_t m_unused : 2;
} zstdhl_DeflateConv_HuffmanTableEntry_t;

typedef struct zstdhl_DeflateConv_HuffmanTree
{
	const uint8_t *m_symbolLengths;
	size_t m_numSymbols;
	uint8_t m_longestLength;

	zstdhl_DeflateConv_HuffmanTableEntry_t m_level1Lookup[256];
	zstdhl_DeflateConv_HuffmanTableEntry_t m_level2Lookup[1 << 16];
} zstdhl_DeflateConv_HuffmanTree_t;

typedef struct zstdhl_DeflateConv_ExportedSequenceCode
{
	uint8_t m_litLengthCode;
	uint8_t m_matchLengthCode;
	uint8_t m_offsetCode;
} zstdhl_DeflateConv_ExportedSequenceCode_t;

static zstdhl_ResultCode_t zstdhl_DeflateConv_ReserveCode(zstdhl_DeflateConv_HuffmanTree_t *tree, uint16_t symbol, uint16_t code, uint8_t codeLength)
{
	uint16_t fillBit = 1 << codeLength;
	uint16_t incCode = code;
	uint16_t step = 0;

	if (codeLength <= 8)
	{
		uint8_t spareBits = 8 - codeLength;

		incCode <<= spareBits;

		for (step = 0; step < (1 << spareBits); step++)
		{
			uint32_t flippedCode = ((zstdhl_ReverseBits32(incCode + step) >> 24) & 0xffu);
			zstdhl_DeflateConv_HuffmanTableEntry_t *level1Entry = tree->m_level1Lookup + flippedCode;

			level1Entry->m_length = codeLength;
			level1Entry->m_symbol = symbol;
			level1Entry->m_needsL2Bit = 0;
			level1Entry->m_unused = 0;
		}
	}
	else
	{
		uint8_t spareBits = 16 - codeLength;

		incCode <<= spareBits;

		{
			uint32_t flippedL1Code = ((zstdhl_ReverseBits32(incCode) >> 16) & 0xffu);
			zstdhl_DeflateConv_HuffmanTableEntry_t *level1Entry = tree->m_level1Lookup + flippedL1Code;

			level1Entry->m_length = 0;
			level1Entry->m_symbol = 0;
			level1Entry->m_needsL2Bit = 1;
			level1Entry->m_unused = 0;
		}

		for (step = 0; step < (1 << spareBits); step++)
		{
			uint32_t flippedCode = ((zstdhl_ReverseBits32(incCode + step) >> 16) & 0xffffu);
			zstdhl_DeflateConv_HuffmanTableEntry_t *level2Entry = tree->m_level2Lookup + flippedCode;

			level2Entry->m_length = codeLength;
			level2Entry->m_symbol = symbol;
			level2Entry->m_needsL2Bit = 0;
			level2Entry->m_unused = 0;
		}
	}

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_DeflateConv_ProcessHuffmanTree(zstdhl_DeflateConv_HuffmanTree_t *tree)
{
	uint16_t numCodesOfLength[ZSTDHL_DEFLATECONV_MAX_CODE_LENGTH + 1];
	uint16_t nextCode[ZSTDHL_DEFLATECONV_MAX_CODE_LENGTH + 1];
	uint16_t firstCode[ZSTDHL_DEFLATECONV_MAX_CODE_LENGTH + 1];
	uint16_t lastCode[ZSTDHL_DEFLATECONV_MAX_CODE_LENGTH + 1];
	uint16_t symCode[ZSTDHL_DEFLATECONV_MAX_ANY_CODES];
	size_t i = 0;
	uint32_t badCodeStart = 0x8000u;
	uint16_t unusedSymCode = 0xffffu;
	uint32_t runningCode = 0;
	uint32_t finalCode = 0;
	uint8_t moreCodesAreInvalid = 0;
	uint8_t longestLength = 0;

	for (i = 0; i <= ZSTDHL_DEFLATECONV_MAX_CODE_LENGTH; i++)
		numCodesOfLength[i] = 0;

	for (i = 0; i < tree->m_numSymbols; i++)
		numCodesOfLength[tree->m_symbolLengths[i]]++;

	for (i = 0; i < ZSTDHL_DEFLATECONV_MAX_ANY_CODES; i++)
		symCode[i] = unusedSymCode;

	nextCode[0] = 0;

	numCodesOfLength[0] = 0;
	firstCode[0] = 0;
	lastCode[0] = 0;
	for (i = 1; i <= ZSTDHL_DEFLATECONV_MAX_CODE_LENGTH; i++)
	{
		uint16_t maxRunningCode = 1 << i;

		firstCode[i] = lastCode[i - 1] * 2u;
		lastCode[i] = firstCode[i] + numCodesOfLength[i];

		if (firstCode[i] >= badCodeStart)
			moreCodesAreInvalid = 1;

		if (numCodesOfLength[i] != 0)
		{
			if (moreCodesAreInvalid || lastCode[i] > badCodeStart || lastCode[i] > maxRunningCode)
				return ZSTDHL_RESULT_HUFFMAN_CODE_TOO_LONG;

			finalCode = lastCode[i];
			longestLength = (uint8_t)i;
		}

		nextCode[i] = firstCode[i];
	}

	if (finalCode > badCodeStart)
		return ZSTDHL_RESULT_HUFFMAN_CODE_TOO_LONG;

	if (longestLength == 0)
		return ZSTDHL_RESULT_HUFFMAN_TABLE_EMPTY;

	if (!zstdhl_IsPowerOf2(finalCode))
		return ZSTDHL_RESULT_HUFFMAN_TABLE_IMPLICIT_WEIGHT_UNRESOLVABLE;

	for (i = 0; i < tree->m_numSymbols; i++)
	{
		uint8_t len = tree->m_symbolLengths[i];

		if (len != 0)
		{
			uint16_t code = nextCode[len];

			zstdhl_DeflateConv_ReserveCode(tree, (uint16_t)i, code, len);

			symCode[i] = nextCode[len];
			nextCode[len]++;

			if (len > longestLength)
				return ZSTDHL_RESULT_HUFFMAN_TABLE_DAMAGED;
		}
	}

	tree->m_longestLength = longestLength;

	return ZSTDHL_RESULT_OK;
}

struct zstdhl_DeflateConv_State
{
	zstdhl_MemoryAllocatorObject_t m_memAlloc;
	zstdhl_StreamSourceObject_t m_streamSource;

	zstdhl_DeflateConv_HuffmanTree_t m_litLengthTree;
	zstdhl_DeflateConv_HuffmanTree_t m_distTree;
	zstdhl_DeflateConv_HuffmanTree_t m_codeLengthTree;
	uint8_t m_litLengthDistCombinedSymbolLengths[ZSTDHL_DEFLATECONV_MAX_LIT_LENGTH_CODES + ZSTDHL_DEFLATECONV_MAX_DIST_CODES];
	uint8_t m_codeLengthLengths[ZSTDHL_DEFLATECONV_MAX_CODE_LENGTH_CODES];

	uint32_t m_streamBits;
	uint8_t m_numStreamBits;
	uint8_t m_eof;
	uint8_t m_isLastBlock;
	uint8_t m_haveEncodedCompressedBlockWithSequences;

	zstdhl_Vector_t m_literalsVector;
	zstdhl_Vector_t m_sequencesVector;
	zstdhl_Vector_t m_offsetsVector;
	zstdhl_Vector_t m_exportedSequenceCodesVector;

	zstdhl_Vector_t m_litLengthStatsVector;
	zstdhl_Vector_t m_matchLengthStatsVector;
	zstdhl_Vector_t m_offsetCodeStatsVector;
	zstdhl_Vector_t m_litStatsVector;

	zstdhl_Vector_t m_litLengthProbsVector;
	zstdhl_Vector_t m_matchLengthProbsVector;
	zstdhl_Vector_t m_offsetProbsVector;

	zstdhl_Vector_t m_tempProbsVector;

	uint32_t m_literalsEmittedSinceLastSequence;

	uint32_t m_repeatedOffset1;
	uint32_t m_repeatedOffset2;
	uint32_t m_repeatedOffset3;

	zstdhl_FSETableDef_t m_prevLitLengthsTable;
	zstdhl_FSETableDef_t m_prevMatchLengthTable;
	zstdhl_FSETableDef_t m_prevOffsetsTable;

	zstdhl_SequencesCompressionMode_t m_litLengthMode;
	zstdhl_SequencesCompressionMode_t m_matchLengthMode;
	zstdhl_SequencesCompressionMode_t m_offsetMode;

	zstdhl_HuffmanTreeDesc_t m_trees[2];
	int m_activeTreeIndex;

	zstdhl_StreamSourceObject_t m_litReader;
	size_t m_litReadPos;
	size_t m_sequenceReadPos;
};

static zstdhl_ResultCode_t zstdhl_DeflateConv_PeekBits(zstdhl_DeflateConv_State_t *state, uint8_t numBitsRequested, uint8_t *outNumBits, uint32_t *outBits, zstdhl_ResultCode_t readFailCode)
{
	zstdhl_ResultCode_t result = ZSTDHL_RESULT_OK;

	if (state->m_numStreamBits < numBitsRequested)
	{
		uint8_t refillBytes[3];
		uint8_t neededMoreBits = numBitsRequested - state->m_numStreamBits;
		size_t bytesWanted = (neededMoreBits + 7) / 8;
		size_t bytesRead = 0;
		size_t i = 0;

		if (bytesWanted > 3)
			return ZSTDHL_RESULT_INTERNAL_ERROR;

		if (!state->m_eof)
		{
			bytesRead = state->m_streamSource.m_readBytesFunc(state->m_streamSource.m_userdata, refillBytes, bytesWanted);
			if (bytesRead < bytesWanted)
				state->m_eof = 1;

			for (i = 0; i < bytesRead; i++)
			{
				state->m_streamBits |= ((uint32_t)refillBytes[i]) << state->m_numStreamBits;
				state->m_numStreamBits += 8;
			}
		}
	}

	{
		uint8_t actualBits = numBitsRequested;
		uint32_t mask = 0;

		if (state->m_numStreamBits < numBitsRequested)
		{
			actualBits = state->m_numStreamBits;
			result = readFailCode;
		}

		mask = (((uint32_t)1u) << actualBits) - 1u;

		*outBits = (state->m_streamBits & mask);
		*outNumBits = actualBits;
	}

	return result;
}

static zstdhl_ResultCode_t zstdhl_DeflateConv_DiscardBits(zstdhl_DeflateConv_State_t *state, uint8_t numBits)
{
	if (state->m_numStreamBits < numBits)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	state->m_streamBits >>= numBits;
	state->m_numStreamBits -= numBits;

	return ZSTDHL_RESULT_OK;
}

static zstdhl_ResultCode_t zstdhl_DeflateConv_ReadBits(zstdhl_DeflateConv_State_t *state, uint8_t numBits, uint32_t *outBits)
{
	uint8_t numBitsPeeked = 0;

	ZSTDHL_CHECKED(zstdhl_DeflateConv_PeekBits(state, numBits, &numBitsPeeked, outBits, ZSTDHL_RESULT_INPUT_FAILED));
	ZSTDHL_CHECKED(zstdhl_DeflateConv_DiscardBits(state, numBits));

	return ZSTDHL_RESULT_OK;
}


zstdhl_ResultCode_t zstdhl_DeflateConv_CreateState(const zstdhl_MemoryAllocatorObject_t *alloc, const zstdhl_StreamSourceObject_t *streamSource, zstdhl_DeflateConv_State_t **outState)
{
	zstdhl_DeflateConv_State_t *state = alloc->m_reallocFunc(alloc->m_userdata, NULL, sizeof(zstdhl_DeflateConv_State_t));
	if (!state)
		return ZSTDHL_RESULT_OUT_OF_MEMORY;

	state->m_memAlloc.m_reallocFunc = alloc->m_reallocFunc;
	state->m_memAlloc.m_userdata = alloc->m_userdata;
	state->m_streamSource.m_readBytesFunc = streamSource->m_readBytesFunc;
	state->m_streamSource.m_userdata = streamSource->m_userdata;

	state->m_streamBits = 0;
	state->m_numStreamBits = 0;
	state->m_eof = 0;
	state->m_isLastBlock = 0;
	state->m_literalsEmittedSinceLastSequence = 0;
	state->m_haveEncodedCompressedBlockWithSequences = 0;

	state->m_repeatedOffset1 = 1;
	state->m_repeatedOffset2 = 4;
	state->m_repeatedOffset3 = 8;

	state->m_trees[0].m_weightTable.m_probabilities = state->m_trees[0].m_weightTableProbabilities;
	state->m_trees[1].m_weightTable.m_probabilities = state->m_trees[1].m_weightTableProbabilities;
	state->m_activeTreeIndex = -1;

	zstdhl_Vector_Init(&state->m_literalsVector, 1, alloc);
	zstdhl_Vector_Init(&state->m_sequencesVector, sizeof(zstdhl_SequenceDesc_t), alloc);
	zstdhl_Vector_Init(&state->m_offsetsVector, sizeof(uint32_t), alloc);
	zstdhl_Vector_Init(&state->m_exportedSequenceCodesVector, sizeof(zstdhl_DeflateConv_ExportedSequenceCode_t), alloc);

	zstdhl_Vector_Init(&state->m_litLengthStatsVector, sizeof(size_t), alloc);
	zstdhl_Vector_Init(&state->m_matchLengthStatsVector, sizeof(size_t), alloc);
	zstdhl_Vector_Init(&state->m_offsetCodeStatsVector, sizeof(size_t), alloc);
	zstdhl_Vector_Init(&state->m_litStatsVector, sizeof(size_t), alloc);

	zstdhl_Vector_Init(&state->m_litLengthProbsVector, sizeof(uint32_t), alloc);
	zstdhl_Vector_Init(&state->m_matchLengthProbsVector, sizeof(uint32_t), alloc);
	zstdhl_Vector_Init(&state->m_offsetProbsVector, sizeof(uint32_t), alloc);

	zstdhl_Vector_Init(&state->m_tempProbsVector, sizeof(uint32_t), alloc);

	*outState = state;

	return ZSTDHL_RESULT_OK;
}

void zstdhl_DeflateConv_DestroyState(zstdhl_DeflateConv_State_t *state)
{
	zstdhl_Vector_Destroy(&state->m_literalsVector);
	zstdhl_Vector_Destroy(&state->m_sequencesVector);
	zstdhl_Vector_Destroy(&state->m_offsetsVector);
	zstdhl_Vector_Destroy(&state->m_exportedSequenceCodesVector);

	zstdhl_Vector_Destroy(&state->m_litLengthStatsVector);
	zstdhl_Vector_Destroy(&state->m_matchLengthStatsVector);
	zstdhl_Vector_Destroy(&state->m_offsetCodeStatsVector);
	zstdhl_Vector_Destroy(&state->m_litStatsVector);

	zstdhl_Vector_Destroy(&state->m_litLengthProbsVector);
	zstdhl_Vector_Destroy(&state->m_matchLengthProbsVector);
	zstdhl_Vector_Destroy(&state->m_offsetProbsVector);

	zstdhl_Vector_Destroy(&state->m_tempProbsVector);

	state->m_memAlloc.m_reallocFunc(state->m_memAlloc.m_userdata, state, 0);
}

zstdhl_ResultCode_t zstdhl_DeflateConv_ConvertRawBlock(zstdhl_DeflateConv_State_t *state, zstdhl_EncBlockDesc_t *outTempBlockDesc)
{
	uint32_t len = 0;
	uint32_t nlen = 0;
	uint32_t lenRemaining = 0;

	zstdhl_Vector_Clear(&state->m_literalsVector);

	ZSTDHL_CHECKED(zstdhl_DeflateConv_DiscardBits(state, state->m_numStreamBits % 8u));

	ZSTDHL_CHECKED(zstdhl_DeflateConv_ReadBits(state, 16, &len));
	ZSTDHL_CHECKED(zstdhl_DeflateConv_ReadBits(state, 16, &nlen));

	if (len != ((~nlen) & 0xffffu))
		return ZSTDHL_RESULT_INVALID_VALUE;

	lenRemaining = len;

	while (lenRemaining > 0)
	{
		if (state->m_numStreamBits > 0)
		{
			uint32_t preloadedBits = 0;
			uint8_t preloadedByte = 0;

			if (state->m_numStreamBits % 8u != 0)
				return ZSTDHL_RESULT_INTERNAL_ERROR;

			ZSTDHL_CHECKED(zstdhl_DeflateConv_ReadBits(state, 8, &preloadedBits));
			preloadedByte = (uint8_t)preloadedBits;

			ZSTDHL_CHECKED(zstdhl_Vector_Append(&state->m_literalsVector, &preloadedByte, 1));

			lenRemaining--;
			continue;
		}

		{
			uint8_t buffer[1024];
			size_t amountToRead = sizeof(buffer);
			size_t amountRead = 0;

			if (lenRemaining < amountToRead)
				amountToRead = lenRemaining;

			amountRead = state->m_streamSource.m_readBytesFunc(state->m_streamSource.m_userdata, buffer, amountToRead);
			if (amountRead != amountToRead)
				return ZSTDHL_RESULT_INPUT_FAILED;

			ZSTDHL_CHECKED(zstdhl_Vector_Append(&state->m_literalsVector, buffer, amountRead));
			lenRemaining -= amountRead;
		}
	}

	// Export the block
	outTempBlockDesc->m_blockHeader.m_blockSize = len;
	outTempBlockDesc->m_blockHeader.m_blockType = ZSTDHL_BLOCK_TYPE_RAW;
	outTempBlockDesc->m_blockHeader.m_isLastBlock = state->m_isLastBlock;

	outTempBlockDesc->m_autoBlockSizeFlag = 1;
	outTempBlockDesc->m_uncompressedOrRLEData = state->m_literalsVector.m_data;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DeflateConv_ReadHuffmanCode(zstdhl_DeflateConv_State_t *state, const zstdhl_DeflateConv_HuffmanTree_t *tree, uint16_t *outSymbol)
{
	uint8_t numBits = 0;
	uint32_t bits = 0;
	const zstdhl_DeflateConv_HuffmanTableEntry_t *tableEntry = NULL;

	// Read failures are okay here
	ZSTDHL_CHECKED(zstdhl_DeflateConv_PeekBits(state, tree->m_longestLength, &numBits, &bits, ZSTDHL_RESULT_OK));

	tableEntry = &tree->m_level1Lookup[bits & 0xffu];
	if (tableEntry->m_needsL2Bit)
		tableEntry = &tree->m_level2Lookup[bits & 0xffffu];

	if (tableEntry->m_length > numBits)
		return ZSTDHL_RESULT_INPUT_FAILED;

	ZSTDHL_CHECKED(zstdhl_DeflateConv_DiscardBits(state, (uint8_t)tableEntry->m_length));

	*outSymbol = tableEntry->m_symbol;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DeflateConv_ReadCompressedTrees(zstdhl_DeflateConv_State_t *state)
{
	const zstdhl_DeflateConv_HuffmanTree_t *lengthTree = &state->m_codeLengthTree;
	uint8_t *symLengths = state->m_litLengthDistCombinedSymbolLengths;
	size_t lengthIndex = 0;
	uint16_t sym = 0;
	size_t numTotalLengths = state->m_litLengthTree.m_numSymbols + state->m_distTree.m_numSymbols;

	while (lengthIndex < numTotalLengths)
	{
		ZSTDHL_CHECKED(zstdhl_DeflateConv_ReadHuffmanCode(state, lengthTree, &sym));

		if (sym < 16)
			symLengths[lengthIndex++] = (uint8_t)sym;
		else
		{
			uint8_t repeatedValue = 0;
			uint8_t repeatCountBits = 0;
			uint8_t repeatCountBase = 0;
			uint32_t repeatCount = 0;

			if (sym == 16)
			{
				if (lengthIndex == 0)
					return ZSTDHL_RESULT_HUFFMAN_TABLE_DAMAGED;

				repeatedValue = symLengths[lengthIndex - 1];
				repeatCountBase = 3;
				repeatCountBits = 2;
			}
			else if (sym == 17)
			{
				repeatCountBase = 3;
				repeatCountBits = 3;
			}
			else if (sym == 18)
			{
				repeatCountBase = 11;
				repeatCountBits = 7;
			}
			else
				return ZSTDHL_RESULT_INTERNAL_ERROR;

			ZSTDHL_CHECKED(zstdhl_DeflateConv_ReadBits(state, repeatCountBits, &repeatCount));

			repeatCount += repeatCountBase;

			if (numTotalLengths - lengthIndex < repeatCount)
				return ZSTDHL_RESULT_INTERNAL_ERROR;

			while (repeatCount > 0)
			{
				symLengths[lengthIndex++] = repeatedValue;
				repeatCount--;
			}
		}
	}

	ZSTDHL_CHECKED(zstdhl_DeflateConv_ProcessHuffmanTree(&state->m_litLengthTree));
	ZSTDHL_CHECKED(zstdhl_DeflateConv_ProcessHuffmanTree(&state->m_distTree));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DeflateConv_UseStaticHuffmanCodes(zstdhl_DeflateConv_State_t *state)
{
	uint8_t litLengthLengths[288];
	uint8_t distLengths[32];
	size_t i = 0;

	for (i = 0; i < 144; i++)
		litLengthLengths[i] = 8;
	for (i = 144; i < 256; i++)
		litLengthLengths[i] = 9;
	for (i = 256; i < 280; i++)
		litLengthLengths[i] = 7;
	for (i = 280; i < 288; i++)
		litLengthLengths[i] = 8;

	for (i = 0; i < 32; i++)
		distLengths[i] = 5;

	state->m_litLengthTree.m_longestLength = 9;
	state->m_litLengthTree.m_numSymbols = 288;
	state->m_litLengthTree.m_symbolLengths = litLengthLengths;

	state->m_distTree.m_longestLength = 5;
	state->m_distTree.m_numSymbols = 32;
	state->m_distTree.m_symbolLengths = distLengths;

	ZSTDHL_CHECKED(zstdhl_DeflateConv_ProcessHuffmanTree(&state->m_litLengthTree));
	ZSTDHL_CHECKED(zstdhl_DeflateConv_ProcessHuffmanTree(&state->m_distTree));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DeflateConv_LoadDynamicHuffmanCodes(zstdhl_DeflateConv_State_t *state)
{
	const uint8_t codeLengthDecodeOrder[ZSTDHL_DEFLATECONV_MAX_CODE_LENGTH_CODES] = { 16, 17, 18,
				  0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
	uint32_t bits = 0;
	uint16_t numLitLengthCodes = 0;
	uint8_t numDistanceCodes = 0;
	uint8_t numCodeLengthCodes = 0;
	size_t i = 0;

	ZSTDHL_CHECKED(zstdhl_DeflateConv_ReadBits(state, 14, &bits));

	numLitLengthCodes = (bits & 0x1f) + 257;
	numDistanceCodes = ((bits >> 5) & 0x1f) + 1;
	numCodeLengthCodes = ((bits >> 10) & 0xf) + 4;

	for (i = 0; i < numCodeLengthCodes; i++)
	{
		ZSTDHL_CHECKED(zstdhl_DeflateConv_ReadBits(state, 3, &bits));
		state->m_codeLengthLengths[codeLengthDecodeOrder[i]] = bits;
	}

	for (i = numCodeLengthCodes; i < ZSTDHL_DEFLATECONV_MAX_CODE_LENGTH_CODES; i++)
	{
		state->m_codeLengthLengths[codeLengthDecodeOrder[i]] = 0;
	}

	state->m_codeLengthTree.m_numSymbols = ZSTDHL_DEFLATECONV_MAX_CODE_LENGTH_CODES;
	state->m_codeLengthTree.m_symbolLengths = state->m_codeLengthLengths;

	ZSTDHL_CHECKED(zstdhl_DeflateConv_ProcessHuffmanTree(&state->m_codeLengthTree));

	state->m_litLengthTree.m_numSymbols = numLitLengthCodes;
	state->m_litLengthTree.m_symbolLengths = state->m_litLengthDistCombinedSymbolLengths;

	state->m_distTree.m_numSymbols = numDistanceCodes;
	state->m_distTree.m_symbolLengths = state->m_litLengthDistCombinedSymbolLengths + numLitLengthCodes;

	ZSTDHL_CHECKED(zstdhl_DeflateConv_ReadCompressedTrees(state));

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DeflateConv_DecodeMatch(zstdhl_DeflateConv_State_t *state, uint16_t litLengthSym)
{
	uint32_t length = 0;
	uint32_t dist = 0;	// This must be uint32_t since it is added to the offsets bignum vector
	uint8_t extraBits = 0;
	uint16_t distSym = 0;
	uint8_t emitNewSequence = 1;

	if (litLengthSym < 261)
		length = litLengthSym - 254;
	else if (litLengthSym == 285)
		length = 258;
	else if (litLengthSym < 285)
	{
		extraBits = (litLengthSym - 261) / 4;
		length = ((4 + ((litLengthSym - 261) & 3)) << extraBits) + 3;
	}
	else
		return ZSTDHL_RESULT_INVALID_VALUE;

	if (extraBits > 0)
	{
		uint32_t bits = 0;
		ZSTDHL_CHECKED(zstdhl_DeflateConv_ReadBits(state, extraBits, &bits));
		length += bits;
	}

	ZSTDHL_CHECKED(zstdhl_DeflateConv_ReadHuffmanCode(state, &state->m_distTree, &distSym));

	if (distSym < 2)
	{
		extraBits = 0;
		dist = distSym + 1;
	}
	else if (distSym < 30)
	{
		extraBits = (distSym - 2) / 2;
		dist = ((2 + (distSym & 1)) << extraBits) + 1;
	}
	else
		return ZSTDHL_RESULT_INVALID_VALUE;

	if (extraBits > 0)
	{
		uint32_t bits = 0;
		ZSTDHL_CHECKED(zstdhl_DeflateConv_ReadBits(state, extraBits, &bits));
		dist += bits;
	}

	if (state->m_literalsEmittedSinceLastSequence == 0 && state->m_sequencesVector.m_count > 0 && dist == state->m_repeatedOffset1)
	{
		zstdhl_SequenceDesc_t *prevSeq = (zstdhl_SequenceDesc_t *)(state->m_sequencesVector.m_data) + (state->m_sequencesVector.m_count - 1u);
		uint32_t extendedMatch = prevSeq->m_matchLength + length;

		if (extendedMatch <= 131074)
		{
			prevSeq->m_matchLength = extendedMatch;
			emitNewSequence = 0;
		}
	}

	if (emitNewSequence)
	{
		zstdhl_SequenceDesc_t seq;
		seq.m_litLength = state->m_literalsEmittedSinceLastSequence;
		seq.m_matchLength = length;
		seq.m_offsetValueBigNum = NULL;
		seq.m_offsetValueNumBits = 0;

		if (seq.m_litLength != 0 && dist == state->m_repeatedOffset1)
			seq.m_offsetType = ZSTDHL_OFFSET_TYPE_REPEAT_1;
		else if (dist == state->m_repeatedOffset2)
		{
			uint32_t temp = state->m_repeatedOffset2;
			state->m_repeatedOffset2 = state->m_repeatedOffset1;
			state->m_repeatedOffset1 = temp;

			seq.m_offsetType = ZSTDHL_OFFSET_TYPE_REPEAT_2;
		}
		else if (dist == state->m_repeatedOffset3)
		{
			uint32_t temp = state->m_repeatedOffset3;
			state->m_repeatedOffset3 = state->m_repeatedOffset2;
			state->m_repeatedOffset2 = state->m_repeatedOffset1;
			state->m_repeatedOffset1 = temp;

			seq.m_offsetType = ZSTDHL_OFFSET_TYPE_REPEAT_3;
		}
		else if (seq.m_litLength == 0 && (dist + 1u) == state->m_repeatedOffset1)
		{
			state->m_repeatedOffset3 = state->m_repeatedOffset2;
			state->m_repeatedOffset2 = state->m_repeatedOffset1;
			state->m_repeatedOffset1--;

			seq.m_offsetType = ZSTDHL_OFFSET_TYPE_REPEAT_1_MINUS_1;
		}
		else
		{
			state->m_repeatedOffset3 = state->m_repeatedOffset2;
			state->m_repeatedOffset2 = state->m_repeatedOffset1;
			state->m_repeatedOffset1 = dist;
			seq.m_offsetType = ZSTDHL_OFFSET_TYPE_SPECIFIED;
		}

		ZSTDHL_CHECKED(zstdhl_Vector_Append(&state->m_sequencesVector, &seq, 1));
		ZSTDHL_CHECKED(zstdhl_Vector_Append(&state->m_offsetsVector, &dist, 1));
	}

	state->m_literalsEmittedSinceLastSequence = 0;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DeflateConv_AddToStats(zstdhl_Vector_t *statsVector, uint32_t symbol)
{
	while (statsVector->m_count <= symbol)
	{
		size_t zero = 0;

		ZSTDHL_CHECKED(zstdhl_Vector_Append(statsVector, &zero, 1));
	}

	((size_t *)statsVector->m_data)[symbol]++;

	return ZSTDHL_RESULT_OK;
}

static void zstdhl_ComputeFSETableUsage(uint32_t fseProb, uint8_t accuracyLog, uint16_t *outNumLargeShares, uint8_t *outLargeShareSize, uint16_t *outNumSmallShares, uint8_t *outSmallShareSize, uint8_t *outShareTotalBits)
{
	uint16_t numLargeShares = 0;
	uint16_t numSmallShares = 0;
	uint8_t largeShareSize = 0;
	uint8_t smallShareSize = 0;
	uint8_t shareTotalBits = 0;
	int log2Prob = zstdhl_Log2_32(fseProb);

	if (fseProb == (uint32_t)(1 << log2Prob))
	{
		smallShareSize = accuracyLog - (uint8_t)log2Prob;
		numSmallShares = fseProb;
	}
	else
	{
		log2Prob++;	// For 5, log2prob == 3

		smallShareSize = accuracyLog - log2Prob;
		largeShareSize = smallShareSize + 1;

		numLargeShares = (uint32_t)(1 << log2Prob) - fseProb;
		numSmallShares = fseProb - numLargeShares;
	}

	*outNumLargeShares = numLargeShares;
	*outNumSmallShares = numSmallShares;
	*outLargeShareSize = largeShareSize;
	*outSmallShareSize = smallShareSize;
	*outShareTotalBits = (uint8_t)log2Prob;
}

zstdhl_ResultCode_t zstdhl_CreateFSEProbsFromStats(const size_t *stats, size_t numStats, uint32_t *outProbs, uint8_t accuracyLog, uint8_t *outProbsFit)
{
	size_t targetProbTotal = (size_t)1 << accuracyLog;
	size_t numNonZeroStats = 0;
	size_t i = 0;
	size_t probsRemaining = targetProbTotal;
	uint64_t score = 0;
	size_t statsTotal = 0;

	for (i = 0; i < numStats; i++)
	{
		if (stats[i])
			numNonZeroStats++;

		statsTotal += stats[i];
	}

	if (numNonZeroStats > targetProbTotal || numNonZeroStats == 0)
	{
		*outProbsFit = 0;
		return ZSTDHL_RESULT_OK;
	}

	for (i = 0; i < numStats; i++)
		outProbs[i] = 0;

	// Detect RLE-like table
	if (numNonZeroStats == 1)
	{
		for (i = 0; i < numStats; i++)
		{
			if (stats[i])
			{
				outProbs[i] = 1 << accuracyLog;
				break;
			}
		}

		*outProbsFit = 1;
		return ZSTDHL_RESULT_OK;
	}

	for (i = 0; i < numStats; i++)
	{
		if (stats[i])
		{
			outProbs[i] = 1;
			probsRemaining--;
		}
	}

	while (probsRemaining > 0)
	{
		uint64_t bestScore = 0;
		size_t bestIndex = numStats;

		for (i = 0; i < numStats; i++)
		{
			uint32_t prob = outProbs[i];
			if (prob)
			{
				uint64_t score = (uint64_t)stats[i] * (uint64_t)(kLog2Table[prob + 1] - kLog2Table[prob]);
				if (bestIndex == numStats || score > bestScore)
				{
					bestScore = score;
					bestIndex = i;
				}
			}
		}

		if (bestIndex == numStats)
			return ZSTDHL_RESULT_INTERNAL_ERROR;

		outProbs[bestIndex]++;

		probsRemaining--;
	}

	for (i = 0; i < numStats; i++)
	{
		// if ((stats[i] / statsTotal) < (1 / (1 << accuracyLog)))
		if (outProbs[i] == 1 && (stats[i] << accuracyLog) < statsTotal)
			outProbs[i] = zstdhl_GetLessThanOneConstant();
	}

	*outProbsFit = 1;
	return ZSTDHL_RESULT_OK;
}

uint64_t zstdhl_DeflateConv_ScoreTable(const size_t *stats, size_t numStats, const zstdhl_FSETableDef_t *tableDef, uint8_t needEncodeTable, uint8_t *outIsValid)
{
	uint8_t accuracyLog = tableDef->m_accuracyLog;
	uint64_t score = 0;
	size_t i = 0;

	*outIsValid = 0;

	for (i = 0; i < numStats; i++)
	{
		size_t count = stats[i];

		if (count != 0)
		{
			uint32_t effectiveProb = 0;
			if (i >= tableDef->m_numProbabilities || tableDef->m_probabilities[i] == 0)
				return 0;

			effectiveProb = tableDef->m_probabilities[i];
			if (effectiveProb == zstdhl_GetLessThanOneConstant())
				effectiveProb = 1;

			score += ((uint64_t)count) * (uint64_t)((9 << kLog2Shift) - kLog2Table[effectiveProb << (9 - accuracyLog)]);
		}
	}

	score += ((uint64_t)1) << kLog2Shift;
	score--;
	score >>= kLog2Shift;

	if (needEncodeTable)
	{
		uint32_t remainingProbPoints = (uint32_t)(1 << tableDef->m_accuracyLog);
		uint32_t probIndex = 0;

		score += 4;	// Accuracy log desc

		while (remainingProbPoints > 0)
		{
			uint32_t maxEncodableValue = remainingProbPoints + 1;
			int maxBitsRequired = zstdhl_Log2_32(maxEncodableValue) + 1;
			uint32_t smallValueCutoff = (uint32_t)((1 << maxBitsRequired) - 1) - maxEncodableValue;
			uint32_t prob = tableDef->m_probabilities[probIndex];
			uint32_t codedProbValue = prob + 1;

			if (prob == zstdhl_GetLessThanOneConstant())
			{
				codedProbValue = 0;
				prob = 1;
			}

			remainingProbPoints -= prob;

			score += (uint32_t)maxBitsRequired;
			if (codedProbValue < smallValueCutoff)
				score--;

			probIndex++;
			if (prob == 0)
			{
				uint32_t repeatCount = 0;
				while (tableDef->m_probabilities[probIndex] == 0)
				{
					repeatCount++;
					probIndex++;
				}

				score += ((repeatCount / 3u) + 1u) * 2u;
			}
		}
	}

	*outIsValid = 1;
	return score;
}

zstdhl_ResultCode_t zstdhl_DeflateConv_TryFSETable(const size_t *stats, size_t numStats, zstdhl_Vector_t *probsVector, const zstdhl_FSETableDef_t *candidateTableDef, zstdhl_FSETableDef_t *outTableDef, uint8_t *outHaveScore, uint64_t *inOutBestScore, zstdhl_SequencesCompressionMode_t *outBestMode, zstdhl_SequencesCompressionMode_t newMode, uint8_t needEncodeTable, uint8_t numExtraBits)
{
	uint8_t isValid = 0;
	uint64_t score = zstdhl_DeflateConv_ScoreTable(stats, numStats, candidateTableDef, needEncodeTable, &isValid) + numExtraBits;

	if (isValid && (((*outHaveScore) == 0) || score < (*inOutBestScore)))
	{
		*outBestMode = newMode;
		*inOutBestScore = score;
		*outHaveScore = 1;

		outTableDef->m_accuracyLog = candidateTableDef->m_accuracyLog;
		outTableDef->m_numProbabilities = candidateTableDef->m_numProbabilities;

		if (candidateTableDef->m_probabilities != probsVector->m_data)
		{
			size_t i = 0;
			const uint32_t *probs = candidateTableDef->m_probabilities;
			size_t numProbs = candidateTableDef->m_numProbabilities;

			zstdhl_Vector_Clear(probsVector);

			ZSTDHL_CHECKED(zstdhl_Vector_Append(probsVector, probs, numProbs));
		}

		outTableDef->m_probabilities = (const uint32_t *)probsVector->m_data;
	}

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DeflateConv_SelectOptimalFSETable(const zstdhl_Vector_t *statsVector, zstdhl_Vector_t *probsVector, zstdhl_Vector_t *tempVectorU32, zstdhl_FSETableDef_t *table, zstdhl_SequencesCompressionMode_t *outMode, const zstdhl_SubstreamCompressionStructureDef_t *sdef, uint8_t isFirstCompressedBlock)
{
	uint8_t haveScore = 0;
	uint64_t bestScore = 0;
	const size_t *stats = (const size_t *)statsVector->m_data;
	size_t numStats = statsVector->m_count;
	size_t accuracyLog = 0;

	// Try reuse
	if (!isFirstCompressedBlock)
	{
		ZSTDHL_CHECKED(zstdhl_DeflateConv_TryFSETable(stats, numStats, probsVector, table, table, &haveScore, &bestScore, outMode, ZSTDHL_SEQ_COMPRESSION_MODE_REUSE, 0, 0));
	}

	// Try predefined
	{
		zstdhl_FSETableDef_t tableDef;
		tableDef.m_accuracyLog = sdef->m_defaultAccuracyLog;
		tableDef.m_numProbabilities = sdef->m_numProbs;
		tableDef.m_probabilities = sdef->m_defaultProbs;

		ZSTDHL_CHECKED(zstdhl_DeflateConv_TryFSETable(stats, numStats, probsVector, &tableDef, table, &haveScore, &bestScore, outMode, ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED, 0, 0));
	}

	// Try RLE
	{
		size_t i = 0;
		size_t rleSym = 0;
		uint8_t numStatsFound = 0;
		uint8_t foundMultipleStat = 0;

		for (i = 0; i < numStats; i++)
		{
			if (stats[i] != 0)
			{
				numStatsFound++;

				if (numStatsFound > 1)
					break;

				rleSym = i;
			}
		}

		if (numStatsFound == 1)
		{
			zstdhl_FSETableDef_t rleTableDef;
			uint32_t fullStat = 1 << ZSTDHL_MIN_ACCURACY_LOG;

			zstdhl_Vector_Clear(tempVectorU32);

			for (i = 0; i < rleSym; i++)
			{
				uint32_t zero = 0;
				ZSTDHL_CHECKED(zstdhl_Vector_Append(tempVectorU32, &zero, 1));
			}

			ZSTDHL_CHECKED(zstdhl_Vector_Append(tempVectorU32, &fullStat, 1));

			rleTableDef.m_accuracyLog = ZSTDHL_MIN_ACCURACY_LOG;
			rleTableDef.m_numProbabilities = rleSym + 1;
			rleTableDef.m_probabilities = (const uint32_t *)tempVectorU32->m_data;

			ZSTDHL_CHECKED(zstdhl_DeflateConv_TryFSETable(stats, numStats, probsVector, &rleTableDef, table, &haveScore, &bestScore, outMode, ZSTDHL_SEQ_COMPRESSION_MODE_RLE, 0, 8));

			zstdhl_Vector_Clear(tempVectorU32);
		}
	}

	// Try FSE
	if (numStats >= 2)
	{
		for (accuracyLog = ZSTDHL_MIN_ACCURACY_LOG; accuracyLog <= sdef->m_maxAccuracyLog; accuracyLog++)
		{
			size_t i = 0;
			zstdhl_FSETableDef_t fseTableDef;
			uint32_t *probs = NULL;
			uint8_t probsFit = 0;

			zstdhl_Vector_Clear(tempVectorU32);
			for (i = 0; i < numStats; i++)
			{
				uint32_t zero = 0;

				ZSTDHL_CHECKED(zstdhl_Vector_Append(tempVectorU32, &zero, 1));
			}

			probs = (uint32_t *)tempVectorU32->m_data;

			ZSTDHL_CHECKED(zstdhl_CreateFSEProbsFromStats(stats, numStats, probs, (uint8_t)accuracyLog, &probsFit));

			if (probsFit)
			{
				fseTableDef.m_numProbabilities = numStats;
				fseTableDef.m_probabilities = probs;
				fseTableDef.m_accuracyLog = (uint8_t)accuracyLog;

				ZSTDHL_CHECKED(zstdhl_DeflateConv_TryFSETable(stats, numStats, probsVector, &fseTableDef, table, &haveScore, &bestScore, outMode, ZSTDHL_SEQ_COMPRESSION_MODE_FSE, 1, 0));

				zstdhl_Vector_Clear(tempVectorU32);
			}
		}
	}

	return ZSTDHL_RESULT_OK;
}


size_t zstdhl_DeflateConv_ReadLits(void *userdata, void *dest, size_t numBytes)
{
	zstdhl_DeflateConv_State_t *state = (zstdhl_DeflateConv_State_t *)userdata;
	size_t firstLit = state->m_litReadPos;
	size_t maxLits = state->m_literalsVector.m_count - firstLit;
	const uint8_t *lits = (const uint8_t *)state->m_literalsVector.m_data;
	size_t i = 0;

	if (numBytes > maxLits)
		numBytes = maxLits;

	for (i = 0; i < numBytes; i++)
		((uint8_t *)dest)[i] = lits[firstLit + i];

	state->m_litReadPos += numBytes;
	return numBytes;
}

zstdhl_ResultCode_t zstdhl_DeflateConv_ReadSequence(void *userdata, zstdhl_SequenceDesc_t *sequence)
{
	zstdhl_DeflateConv_State_t *state = (zstdhl_DeflateConv_State_t *)userdata;
	const zstdhl_SequenceDesc_t *inSeq = ((const zstdhl_SequenceDesc_t *)state->m_sequencesVector.m_data) + state->m_sequenceReadPos;
	size_t i = 0;

	if (state->m_sequenceReadPos == state->m_sequencesVector.m_count)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	sequence->m_litLength = inSeq->m_litLength;
	sequence->m_matchLength = inSeq->m_matchLength;
	sequence->m_offsetValueBigNum = inSeq->m_offsetValueBigNum;
	sequence->m_offsetValueNumBits = inSeq->m_offsetValueNumBits;
	sequence->m_offsetType = inSeq->m_offsetType;

	state->m_sequenceReadPos++;

	return ZSTDHL_RESULT_OK;
}

typedef struct zstdhl_DeflateConv_HuffmanTreeNode
{
	size_t m_count;
	uint16_t m_depth;
	uint16_t m_id;
	struct zstdhl_DeflateConv_HuffmanTreeNode *m_parent;
	struct zstdhl_DeflateConv_HuffmanTreeNode *m_children[2];
	uint8_t m_symbol;
} zstdhl_DeflateConv_HuffmanTreeNode_t;

static void zstdhl_DeflateConv_RecursiveComputeHuffmanDepths(zstdhl_DeflateConv_HuffmanTreeNode_t *tree, size_t depth)
{
	tree->m_depth = (uint16_t)depth;

	if (tree->m_children[0])
		zstdhl_DeflateConv_RecursiveComputeHuffmanDepths(tree->m_children[0], depth + 1);
	if (tree->m_children[1])
		zstdhl_DeflateConv_RecursiveComputeHuffmanDepths(tree->m_children[1], depth + 1);
}

static zstdhl_ResultCode_t zstdhl_DeflateConv_CreateHuffmanTreeForStats(zstdhl_HuffmanTreeDesc_t *tree, const size_t *stats, size_t numStats)
{
	zstdhl_DeflateConv_HuffmanTreeNode_t treeNodes[512];
	zstdhl_DeflateConv_HuffmanTreeNode_t *unprocessedNodes[257];
	size_t numNodes = 0;
	size_t numUnprocessedNodes = 0;
	size_t i = 0;
	size_t numLeafNodes = 0;
	size_t largestDepth = 0;
	size_t numSpecifiedWeights = 0;
	size_t weightStats[ZSTDHL_MAX_HUFFMAN_CODE_LENGTH + 1];
	uint32_t weightProbs[2][ZSTDHL_MAX_HUFFMAN_CODE_LENGTH + 1];
	size_t leafsWithBitCount[256];
	size_t sortedLeafs[256];
	uint8_t weightProbsFit[3];
	uint64_t scores[3];
	uint8_t haveValidScore = 0;
	uint64_t bestScoreIndex = 0;
	uint64_t weightRepeatsScore = 0;
	uint8_t haveBadLeafs = 0;

	if (numStats < 2)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	{
		size_t sortedLeafIndex = 0;

		for (i = 0; i < 256; i++)
			sortedLeafs[i] = 0;

		for (i = 0; i < sizeof(leafsWithBitCount) / sizeof(leafsWithBitCount[0]); i++)
			leafsWithBitCount[i] = 0;

		numNodes = 0;
		numUnprocessedNodes = 0;
		numLeafNodes = 0;
		largestDepth = 0;

		for (i = 0; i < numStats; i++)
		{
			size_t count = stats[i];
			if (count)
			{
				zstdhl_DeflateConv_HuffmanTreeNode_t *node = treeNodes + numLeafNodes;

				node->m_symbol = (uint8_t)i;
				node->m_count = count;
				node->m_children[0] = NULL;
				node->m_children[1] = NULL;
				node->m_parent = NULL;
				node->m_id = (uint16_t)numLeafNodes;

				unprocessedNodes[numLeafNodes] = &treeNodes[numLeafNodes];

				numLeafNodes++;
			}
		}

		numUnprocessedNodes = numLeafNodes;
		numNodes = numLeafNodes;

		while (numUnprocessedNodes > 1)
		{
			size_t smallestIndex = 0;
			size_t secondSmallestIndex = 0;
			zstdhl_DeflateConv_HuffmanTreeNode_t *up0 = NULL;
			zstdhl_DeflateConv_HuffmanTreeNode_t *up1 = NULL;
			zstdhl_DeflateConv_HuffmanTreeNode_t *newParent = &treeNodes[numNodes];
			size_t firstIndexToRemove = 0;
			size_t secondIndexToRemove = 0;

			for (i = 1; i < numUnprocessedNodes; i++)
			{
				if (unprocessedNodes[i]->m_count < unprocessedNodes[smallestIndex]->m_count)
					smallestIndex = i;
			}

			if (smallestIndex == 0)
				secondSmallestIndex = 1;

			for (i = 0; i < numUnprocessedNodes; i++)
			{
				if (i != smallestIndex && unprocessedNodes[i]->m_count < unprocessedNodes[secondSmallestIndex]->m_count)
					secondSmallestIndex = i;
			}

			up0 = &treeNodes[smallestIndex];
			up1 = &treeNodes[secondSmallestIndex];

			if (unprocessedNodes[smallestIndex]->m_children[0] == NULL)
			{
				size_t nodeID = unprocessedNodes[smallestIndex]->m_id;
				if (nodeID < numLeafNodes)
					sortedLeafs[sortedLeafIndex++] = nodeID;
			}
			if (unprocessedNodes[secondSmallestIndex]->m_children[0] == NULL)
			{
				size_t nodeID = unprocessedNodes[secondSmallestIndex]->m_id;
				if (nodeID < numLeafNodes)
					sortedLeafs[sortedLeafIndex++] = nodeID;
			}

			firstIndexToRemove = smallestIndex;
			secondIndexToRemove = secondSmallestIndex;
			if (secondSmallestIndex > firstIndexToRemove)
			{
				firstIndexToRemove = secondSmallestIndex;
				secondIndexToRemove = smallestIndex;
			}

			up0 = unprocessedNodes[smallestIndex];
			up1 = unprocessedNodes[secondSmallestIndex];

			newParent->m_children[0] = up0;
			newParent->m_children[1] = up1;
			newParent->m_count = up0->m_count + up1->m_count;
			newParent->m_symbol = 0;
			newParent->m_id = (uint16_t)numNodes;
			newParent->m_parent = NULL;
			up0->m_parent = newParent;
			up1->m_parent = newParent;

			unprocessedNodes[firstIndexToRemove] = unprocessedNodes[--numUnprocessedNodes];
			unprocessedNodes[secondIndexToRemove] = unprocessedNodes[--numUnprocessedNodes];
			unprocessedNodes[numUnprocessedNodes++] = newParent;

			numNodes++;
		}

		if (sortedLeafIndex != numLeafNodes)
			return ZSTDHL_RESULT_INTERNAL_ERROR;

		zstdhl_DeflateConv_RecursiveComputeHuffmanDepths(unprocessedNodes[0], 0);

		for (i = 0; i < numLeafNodes; i++)
		{
			uint16_t depth = treeNodes[i].m_depth;
			if (depth > ZSTDHL_MAX_HUFFMAN_CODE_LENGTH)
				haveBadLeafs = 1;

			leafsWithBitCount[depth]++;

			if (depth > largestDepth)
				largestDepth = depth;
		}
	}

	// Fix oversized codes
	if (haveBadLeafs)
	{
		size_t depthToRemove = largestDepth;
		size_t numLeafsToReinsert = 0;

		// Go through layers bottom-up and collapse them upward by removing 1 leaf
		while (depthToRemove > ZSTDHL_MAX_HUFFMAN_CODE_LENGTH)
		{
			size_t leafsRemoved = leafsWithBitCount[depthToRemove] / 2u;

			if (leafsWithBitCount[depthToRemove] & 1)
				return ZSTDHL_RESULT_INTERNAL_ERROR;

			numLeafsToReinsert += leafsRemoved;
			leafsWithBitCount[depthToRemove - 1] += leafsRemoved;
			leafsWithBitCount[depthToRemove] = 0;

			depthToRemove--;
		}

		// Re-add removed leafs
		while (numLeafsToReinsert > 0)
		{
			uint8_t skippedALevel = 0;

			size_t depthToSplit = ZSTDHL_MAX_HUFFMAN_CODE_LENGTH - 1;

			// Find a splittable level
			while (leafsWithBitCount[depthToSplit] == 0)
			{
				skippedALevel = 1;
				depthToSplit--;
			}

			// Split leafs at this level.  If there are deeper levels than this, only split 1 leaf and cycle the loop again
			{

				size_t leafsToSplit = leafsWithBitCount[depthToSplit];
				if (leafsToSplit > numLeafsToReinsert)
					leafsToSplit = numLeafsToReinsert;

				if (skippedALevel)
					leafsToSplit = 1;

				leafsWithBitCount[depthToSplit] -= leafsToSplit;
				leafsWithBitCount[depthToSplit + 1] += leafsToSplit * 2u;

				numLeafsToReinsert -= leafsToSplit;
			}
		}
	}

	largestDepth = 0;

	for (i = 0; i <= ZSTDHL_MAX_HUFFMAN_CODE_LENGTH; i++)
		if (leafsWithBitCount[i])
			largestDepth = i;

	// Redistribute leafs if there were bad nodes
	if (haveBadLeafs)
	{
		size_t bitCount = ZSTDHL_MAX_HUFFMAN_CODE_LENGTH;

		for (i = 0; i < numLeafNodes; i++)
		{
			while (leafsWithBitCount[bitCount] == 0)
				bitCount--;

			leafsWithBitCount[bitCount]--;

			treeNodes[sortedLeafs[i]].m_depth = (uint16_t)bitCount;
		}
	}

	if (numLeafNodes == 0)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	tree->m_partialWeightDesc.m_numSpecifiedWeights = 0;
	for (i = 0; i < 256; i++)
		tree->m_weightTableProbabilities[i] = 0;

	for (i = 0; i < 255; i++)
		tree->m_partialWeightDesc.m_specifiedWeights[i] = 0;

	for (i = 0; i < (ZSTDHL_MAX_HUFFMAN_CODE_LENGTH + 1); i++)
	{
		weightStats[i] = 0;
		weightProbs[0][i] = 0;
		weightProbs[1][i] = 0;
	}

	for (i = 0; i < numLeafNodes; i++)
	{
		uint8_t symbol = treeNodes[i].m_symbol;
		uint8_t weight = 0;

		weight = (uint8_t)(largestDepth + 1 - treeNodes[i].m_depth);

		if (symbol < 255)
			tree->m_partialWeightDesc.m_specifiedWeights[symbol] = weight;

		if (symbol > numSpecifiedWeights)
			numSpecifiedWeights = symbol;

		if (weight > ZSTDHL_MAX_HUFFMAN_WEIGHT)
			return ZSTDHL_RESULT_INTERNAL_ERROR;
	}

	tree->m_partialWeightDesc.m_numSpecifiedWeights = (uint8_t)numSpecifiedWeights;

	if (numSpecifiedWeights < 1)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	for (i = 0; i < numSpecifiedWeights; i++)
		weightStats[tree->m_partialWeightDesc.m_specifiedWeights[i]]++;

	if (numSpecifiedWeights < 2)
	{
		weightProbsFit[0] = 0;
		weightProbsFit[1] = 0;
	}
	else
	{
		for (i = 0; i < 2; i++)
		{
			uint8_t accuracyLog = (uint8_t)(i + ZSTDHL_MIN_ACCURACY_LOG);
			uint8_t probsFit = 0;

			ZSTDHL_CHECKED(zstdhl_CreateFSEProbsFromStats(weightStats, ZSTDHL_MAX_HUFFMAN_CODE_LENGTH + 1, weightProbs[i], accuracyLog, &weightProbsFit[i]));

			if (weightProbsFit[i])
			{
				zstdhl_FSETableDef_t tableDef;
				tableDef.m_accuracyLog = accuracyLog;
				tableDef.m_numProbabilities = ZSTDHL_MAX_HUFFMAN_CODE_LENGTH + 1;
				tableDef.m_probabilities = weightProbs[i];

				scores[i] = zstdhl_DeflateConv_ScoreTable(weightStats, ZSTDHL_MAX_HUFFMAN_CODE_LENGTH + 1, &tableDef, 1, &weightProbsFit[i]);
			}
		}
	}

	if (numSpecifiedWeights <= 128)
	{
		size_t numBytes = (numSpecifiedWeights + 1) / 2;

		weightProbsFit[2] = 1;
		scores[2] = numBytes * 8u;
	}
	else
		weightProbsFit[2] = 0;

	for (i = 0; i < 3; i++)
	{
		if (weightProbsFit[i])
		{
			if (haveValidScore == 0 || scores[i] < scores[bestScoreIndex])
			{
				bestScoreIndex = i;
				haveValidScore = 1;
			}
		}
	}

	if (!haveValidScore)
		return ZSTDHL_RESULT_INTERNAL_ERROR;

	if (bestScoreIndex == 0 || bestScoreIndex == 1)
	{
		tree->m_huffmanWeightFormat = ZSTDHL_HUFFMAN_WEIGHT_ENCODING_FSE;
		tree->m_weightTable.m_probabilities = tree->m_weightTableProbabilities;
		tree->m_weightTable.m_accuracyLog = (uint8_t)(bestScoreIndex + ZSTDHL_MIN_ACCURACY_LOG);

		for (i = 0; i <= ZSTDHL_MAX_HUFFMAN_CODE_LENGTH; i++)
		{
			uint32_t prob = weightProbs[bestScoreIndex][i];
			if (prob != 0)
				tree->m_weightTable.m_numProbabilities = i + 1;

			tree->m_weightTableProbabilities[i] = prob;
		}
	}
	else
		tree->m_huffmanWeightFormat = ZSTDHL_HUFFMAN_WEIGHT_ENCODING_UNCOMPRESSED;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DeflateConv_ScoreHuffmanTree(zstdhl_HuffmanTreeDesc_t *tree, const size_t *stats, size_t numStats, uint8_t encodeTree, uint64_t *outScore, uint8_t *outIsValid)
{
	uint32_t runningTotal = 0;
	size_t i = 0;
	uint8_t maxBits = 0;
	uint32_t nextPO2 = 0;
	uint8_t lastWeight = 0;
	uint8_t codeLengths[256];
	size_t weightStats[ZSTDHL_MAX_HUFFMAN_CODE_LENGTH + 1];
	uint64_t score = 0;
	
	for (i = 0; i < (ZSTDHL_MAX_HUFFMAN_CODE_LENGTH + 1); i++)
		weightStats[i] = 0;

	for (i = 0; i < tree->m_partialWeightDesc.m_numSpecifiedWeights; i++)
	{
		uint8_t weight = tree->m_partialWeightDesc.m_specifiedWeights[i];
		if (weight != 0)
			runningTotal += (uint32_t)1 << (weight - 1);

		weightStats[weight]++;
	}

	for (i = 0; i < 256; i++)
		codeLengths[i] = 0;

	maxBits = zstdhl_Log2_32(runningTotal) + 1;
	nextPO2 = (uint32_t)1 << maxBits;

	lastWeight = zstdhl_Log2_32(nextPO2 - runningTotal) + 1;

	for (i = 0; i < tree->m_partialWeightDesc.m_numSpecifiedWeights; i++)
	{
		uint8_t weight = tree->m_partialWeightDesc.m_specifiedWeights[i];
		if (weight != 0)
			codeLengths[i] = maxBits + 1 - weight;
	}

	codeLengths[tree->m_partialWeightDesc.m_numSpecifiedWeights] = maxBits + 1 - lastWeight;

	for (i = 0; i < numStats; i++)
	{
		if (stats[i])
		{
			score += (uint64_t)codeLengths[i] * stats[i];
			if (codeLengths[i] == 0)
			{
				*outIsValid = 0;
				return ZSTDHL_RESULT_OK;
			}
		}
	}

	if (encodeTree)
	{
		if (tree->m_huffmanWeightFormat == ZSTDHL_HUFFMAN_WEIGHT_ENCODING_FSE)
		{
			uint64_t fseTabScore;
			uint8_t isValid = 0;

			score += 4;	// For accuracy log

			fseTabScore = zstdhl_DeflateConv_ScoreTable(weightStats, ZSTDHL_MAX_HUFFMAN_CODE_LENGTH + 1, &tree->m_weightTable, 1, &isValid);

			if (!isValid)
			{
				*outIsValid = 0;
				return ZSTDHL_RESULT_OK;
			}
			score += fseTabScore;
		}
		else if (tree->m_huffmanWeightFormat == ZSTDHL_HUFFMAN_WEIGHT_ENCODING_UNCOMPRESSED)
		{
			uint16_t numWeightBytes = (((uint16_t)tree->m_partialWeightDesc.m_numSpecifiedWeights) + 1u) / 2u;
			score += numWeightBytes * 8u;
		}
		else
			return ZSTDHL_RESULT_INTERNAL_ERROR;
	}

	*outIsValid = 1;
	*outScore = score;
	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DeflateConv_FindRLEByte(const zstdhl_FSETableDef_t *table, uint8_t *rleByte)
{
	size_t i = 0;
	uint8_t numSyms = 0;

	for (i = 0; i < table->m_numProbabilities; i++)
	{
		if (table->m_probabilities[i] != 0)
		{
			numSyms++;
			if (numSyms == 2)
				return ZSTDHL_RESULT_INTERNAL_ERROR;

			*rleByte = (uint8_t)i;
		}
	}

	return ZSTDHL_RESULT_OK;
}


zstdhl_ResultCode_t zstdhl_DeflateConv_ConvertHuffmanBlock(zstdhl_DeflateConv_State_t *state, zstdhl_EncBlockDesc_t *outTempBlockDesc, uint8_t usePredefined)
{
	uint8_t isFirstCompressedBlockWithSequences = !state->m_haveEncodedCompressedBlockWithSequences;
	uint8_t isRLELitBlock = 1;
	uint8_t useNewHuffmanTree = 0;
	uint8_t useRawLits = 0;

	state->m_literalsEmittedSinceLastSequence = 0;
	zstdhl_Vector_Clear(&state->m_literalsVector);
	zstdhl_Vector_Clear(&state->m_sequencesVector);
	zstdhl_Vector_Clear(&state->m_offsetsVector);

	if (usePredefined)
	{
		ZSTDHL_CHECKED(zstdhl_DeflateConv_UseStaticHuffmanCodes(state));
	}
	else
	{
		ZSTDHL_CHECKED(zstdhl_DeflateConv_LoadDynamicHuffmanCodes(state));
	}

	for (;;)
	{
		uint16_t litLengthSym = 0;

		ZSTDHL_CHECKED(zstdhl_DeflateConv_ReadHuffmanCode(state, &state->m_litLengthTree, &litLengthSym));

		if (litLengthSym < 256)
		{
			uint8_t lit = (uint8_t)litLengthSym;

			ZSTDHL_CHECKED(zstdhl_Vector_Append(&state->m_literalsVector, &lit, 1));
			state->m_literalsEmittedSinceLastSequence++;
		}
		else
		{
			if (litLengthSym == 256)
				break;

			ZSTDHL_CHECKED(zstdhl_DeflateConv_DecodeMatch(state, litLengthSym));
		}
	}

	// Link up offsets
	{
		size_t numSequences = state->m_sequencesVector.m_count;
		uint32_t *offsets = (uint32_t *)(state->m_offsetsVector.m_data);
		zstdhl_SequenceDesc_t *sequences = (zstdhl_SequenceDesc_t *)(state->m_sequencesVector.m_data);
		size_t i = 0;

		for (i = 0; i < numSequences; i++)
		{
			zstdhl_SequenceDesc_t *sequence = sequences + i;
			uint32_t *offset = offsets + i;

			if (sequence->m_offsetType == ZSTDHL_OFFSET_TYPE_SPECIFIED)
			{
				sequence->m_offsetValueBigNum = offset;
				sequence->m_offsetValueNumBits = zstdhl_Log2_32(*offset) + 1;
			}
		}
	}

	// Collect stats
	{
		size_t i = 0;
		size_t numSequences = state->m_sequencesVector.m_count;
		const zstdhl_SequenceDesc_t *sequences = (const zstdhl_SequenceDesc_t *)state->m_sequencesVector.m_data;
		size_t numLits = state->m_literalsVector.m_count;
		const uint8_t *lits = (const uint8_t *)state->m_literalsVector.m_data;

		zstdhl_Vector_Clear(&state->m_exportedSequenceCodesVector);
		zstdhl_Vector_Clear(&state->m_litStatsVector);
		zstdhl_Vector_Clear(&state->m_litLengthStatsVector);
		zstdhl_Vector_Clear(&state->m_matchLengthStatsVector);
		zstdhl_Vector_Clear(&state->m_offsetCodeStatsVector);

		for (i = 0; i < numSequences; i++)
		{
			uint32_t litLengthCode = 0;
			uint32_t matchLengthCode = 0;
			uint32_t offsetCode = 0;
			uint32_t extraBits = 0;
			uint8_t extraNumBits = 0;
			uint32_t offsetSpecifiedValue = 0;
			uint32_t offsetCodeEncoded = 0;

			if (sequences[i].m_offsetType == ZSTDHL_OFFSET_TYPE_SPECIFIED)
				offsetSpecifiedValue = sequences[i].m_offsetValueBigNum[0];

			ZSTDHL_CHECKED(zstdhl_EncodeLitLength(sequences[i].m_litLength, &litLengthCode, &extraBits, &extraNumBits));
			ZSTDHL_CHECKED(zstdhl_EncodeMatchLength(sequences[i].m_matchLength, &matchLengthCode, &extraBits, &extraNumBits));

			ZSTDHL_CHECKED(zstdhl_ResolveOffsetCode32(sequences[i].m_offsetType, sequences[i].m_litLength, offsetSpecifiedValue, &offsetCode));
			ZSTDHL_CHECKED(zstdhl_EncodeOffsetCode(offsetCode, &offsetCodeEncoded, &extraBits, &extraNumBits));

			ZSTDHL_CHECKED(zstdhl_DeflateConv_AddToStats(&state->m_litLengthStatsVector, litLengthCode));
			ZSTDHL_CHECKED(zstdhl_DeflateConv_AddToStats(&state->m_matchLengthStatsVector, matchLengthCode));
			ZSTDHL_CHECKED(zstdhl_DeflateConv_AddToStats(&state->m_offsetCodeStatsVector, offsetCodeEncoded));
		}

		for (i = 0; i < numLits; i++)
		{
			ZSTDHL_CHECKED(zstdhl_DeflateConv_AddToStats(&state->m_litStatsVector, lits[i]));
			if (isRLELitBlock && lits[i] != lits[0])
				isRLELitBlock = 0;
		}

		if (numLits == 0)
		{
			useRawLits = 1;
			isRLELitBlock = 0;
		}
	}

	if (state->m_sequencesVector.m_count > 0)
	{
		ZSTDHL_CHECKED(zstdhl_DeflateConv_SelectOptimalFSETable(&state->m_litLengthStatsVector, &state->m_litLengthProbsVector, &state->m_tempProbsVector, &state->m_prevLitLengthsTable, &state->m_litLengthMode, zstdhl_GetDefaultLitLengthFSEProperties(), isFirstCompressedBlockWithSequences));
		ZSTDHL_CHECKED(zstdhl_DeflateConv_SelectOptimalFSETable(&state->m_matchLengthStatsVector, &state->m_matchLengthProbsVector, &state->m_tempProbsVector, &state->m_prevMatchLengthTable, &state->m_matchLengthMode, zstdhl_GetDefaultMatchLengthFSEProperties(), isFirstCompressedBlockWithSequences));
		ZSTDHL_CHECKED(zstdhl_DeflateConv_SelectOptimalFSETable(&state->m_offsetCodeStatsVector, &state->m_offsetProbsVector, &state->m_tempProbsVector, &state->m_prevOffsetsTable, &state->m_offsetMode, zstdhl_GetDefaultOffsetFSEProperties(), isFirstCompressedBlockWithSequences));

		state->m_haveEncodedCompressedBlockWithSequences = 1;
	}
	else
	{
		state->m_litLengthMode = ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED;
		state->m_matchLengthMode = ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED;
		state->m_offsetMode = ZSTDHL_SEQ_COMPRESSION_MODE_PREDEFINED;
	}

	if (!isRLELitBlock && !useRawLits)
	{
		uint8_t newTreeIsValid = 0;
		uint64_t newTreeScore = 0;
		uint64_t rawScore = 0;
		const size_t *litStats = (const size_t *)state->m_litStatsVector.m_data;
		size_t numLitStats = state->m_litStatsVector.m_count;
		int newTreeIndex = !state->m_activeTreeIndex;
		size_t i = 0;

		ZSTDHL_CHECKED(zstdhl_DeflateConv_CreateHuffmanTreeForStats(&state->m_trees[newTreeIndex], litStats, numLitStats));
		ZSTDHL_CHECKED(zstdhl_DeflateConv_ScoreHuffmanTree(&state->m_trees[newTreeIndex], litStats, numLitStats, 1, &newTreeScore, &newTreeIsValid));

		if (!newTreeIsValid)
			return ZSTDHL_RESULT_INTERNAL_ERROR;

		rawScore = (uint64_t)state->m_literalsVector.m_count * 8u;

		if (state->m_activeTreeIndex < 0)
		{
			// No existing tree, can only use new tree or raw
			if (rawScore > newTreeScore)
			{
				state->m_activeTreeIndex = !state->m_activeTreeIndex;
				useNewHuffmanTree = 1;
			}
			else
				useRawLits = 1;
		}
		else
		{
			uint8_t oldTreeIsValid = 0;
			uint64_t oldTreeScore = 0;
			size_t i = 0;

			ZSTDHL_CHECKED(zstdhl_DeflateConv_ScoreHuffmanTree(&state->m_trees[state->m_activeTreeIndex], litStats, numLitStats, 0, &oldTreeScore, &oldTreeIsValid));

			if (rawScore <= newTreeScore)
			{
				// Best is either raw or old tree
				if (rawScore <= oldTreeScore || !oldTreeIsValid)
					useRawLits = 1;
			}
			else
			{
				// Best is either new tree or old tree
				if (newTreeScore <= oldTreeScore || !oldTreeIsValid)
				{
					state->m_activeTreeIndex = !state->m_activeTreeIndex;
					useNewHuffmanTree = 1;
				}
			}
		}
	}

	// Export the block
	outTempBlockDesc->m_blockHeader.m_blockType = ZSTDHL_BLOCK_TYPE_COMPRESSED;
	outTempBlockDesc->m_blockHeader.m_isLastBlock = state->m_isLastBlock;
	outTempBlockDesc->m_blockHeader.m_blockSize = 0;

	if (useRawLits)
		outTempBlockDesc->m_litSectionHeader.m_sectionType = ZSTDHL_LITERALS_SECTION_TYPE_RAW;
	else if (useNewHuffmanTree)
		outTempBlockDesc->m_litSectionHeader.m_sectionType = ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN;
	else if (isRLELitBlock)
		outTempBlockDesc->m_litSectionHeader.m_sectionType = ZSTDHL_LITERALS_SECTION_TYPE_RLE;
	else
		outTempBlockDesc->m_litSectionHeader.m_sectionType = ZSTDHL_LITERALS_SECTION_TYPE_HUFFMAN_REUSE;

	outTempBlockDesc->m_litSectionHeader.m_regeneratedSize = (uint32_t)state->m_literalsVector.m_count;
	outTempBlockDesc->m_litSectionHeader.m_compressedSize = 0;

	if (state->m_literalsVector.m_count >= 256)
		outTempBlockDesc->m_litSectionDesc.m_huffmanStreamMode = ZSTDHL_HUFFMAN_STREAM_MODE_4_STREAMS;
	else
		outTempBlockDesc->m_litSectionDesc.m_huffmanStreamMode = ZSTDHL_HUFFMAN_STREAM_MODE_1_STREAM;

	outTempBlockDesc->m_litSectionDesc.m_huffmanStreamSizes[0] = 0;
	outTempBlockDesc->m_litSectionDesc.m_huffmanStreamSizes[1] = 0;
	outTempBlockDesc->m_litSectionDesc.m_huffmanStreamSizes[2] = 0;
	outTempBlockDesc->m_litSectionDesc.m_huffmanStreamSizes[3] = 0;

	if (isRLELitBlock)
		outTempBlockDesc->m_litSectionDesc.m_numValues = 1;
	else
		outTempBlockDesc->m_litSectionDesc.m_numValues = state->m_literalsVector.m_count;

	outTempBlockDesc->m_litSectionDesc.m_decompressedLiteralsStream = &state->m_litReader;
	state->m_litReader.m_readBytesFunc = zstdhl_DeflateConv_ReadLits;
	state->m_litReader.m_userdata = state;
	state->m_litReadPos = 0;

	outTempBlockDesc->m_seqSectionDesc.m_numSequences = (uint32_t)state->m_sequencesVector.m_count;

	outTempBlockDesc->m_seqSectionDesc.m_offsetsMode = state->m_offsetMode;
	outTempBlockDesc->m_seqSectionDesc.m_matchLengthsMode = state->m_matchLengthMode;
	outTempBlockDesc->m_seqSectionDesc.m_literalLengthsMode = state->m_litLengthMode;

	if (!useRawLits && !isRLELitBlock)
	{
		size_t i = 0;
		const zstdhl_HuffmanTreeDesc_t *activeTree = &state->m_trees[state->m_activeTreeIndex];

		for (i = 0; i < sizeof(zstdhl_HuffmanTreeDesc_t); i++)
			((uint8_t *)&outTempBlockDesc->m_huffmanTreeDesc)[i] = ((const uint8_t *)activeTree)[i];

		outTempBlockDesc->m_huffmanTreeDesc.m_weightTable.m_probabilities = outTempBlockDesc->m_huffmanTreeDesc.m_weightTableProbabilities;
	}

	outTempBlockDesc->m_literalLengthsCompressionDesc.m_fseProbs = &state->m_prevLitLengthsTable;
	if (state->m_litLengthMode == ZSTDHL_SEQ_COMPRESSION_MODE_RLE)
	{
		ZSTDHL_CHECKED(zstdhl_DeflateConv_FindRLEByte(&state->m_prevLitLengthsTable, &outTempBlockDesc->m_literalLengthsCompressionDesc.m_rleByte));
	}

	outTempBlockDesc->m_offsetsModeCompressionDesc.m_fseProbs = &state->m_prevOffsetsTable;
	if (state->m_offsetMode == ZSTDHL_SEQ_COMPRESSION_MODE_RLE)
	{
		ZSTDHL_CHECKED(zstdhl_DeflateConv_FindRLEByte(&state->m_prevOffsetsTable, &outTempBlockDesc->m_offsetsModeCompressionDesc.m_rleByte));
	}

	outTempBlockDesc->m_matchLengthsCompressionDesc.m_fseProbs = &state->m_prevMatchLengthTable;
	if (state->m_matchLengthMode == ZSTDHL_SEQ_COMPRESSION_MODE_RLE)
	{
		ZSTDHL_CHECKED(zstdhl_DeflateConv_FindRLEByte(&state->m_prevMatchLengthTable, &outTempBlockDesc->m_matchLengthsCompressionDesc.m_rleByte));
	}

	outTempBlockDesc->m_seqCollection.m_getNextSequence = zstdhl_DeflateConv_ReadSequence;
	outTempBlockDesc->m_seqCollection.m_userdata = state;
	state->m_sequenceReadPos = 0;

	outTempBlockDesc->m_autoBlockSizeFlag = 1;
	outTempBlockDesc->m_autoLitCompressedSizeFlag = 1;
	outTempBlockDesc->m_autoLitRegeneratedSizeFlag = 1;
	outTempBlockDesc->m_autoHuffmanStreamSizesFlags[0] = 1;
	outTempBlockDesc->m_autoHuffmanStreamSizesFlags[1] = 1;
	outTempBlockDesc->m_autoHuffmanStreamSizesFlags[2] = 1;
	outTempBlockDesc->m_autoHuffmanStreamSizesFlags[3] = 1;

	outTempBlockDesc->m_uncompressedOrRLEData = NULL;

	return ZSTDHL_RESULT_OK;
}

zstdhl_ResultCode_t zstdhl_DeflateConv_Convert(zstdhl_DeflateConv_State_t *state, uint8_t *outEOFFlag, zstdhl_EncBlockDesc_t *outTempBlockDesc)
{
	uint8_t blockType = 0;
	uint32_t bits = 0;

	if (state->m_isLastBlock)
	{
		*outEOFFlag = 1;
		return ZSTDHL_RESULT_OK;
	}

	*outEOFFlag = 0;

	ZSTDHL_CHECKED(zstdhl_DeflateConv_ReadBits(state, 3, &bits));

	state->m_isLastBlock = (bits & 1);
	blockType = (bits >> 1);

	switch (blockType)
	{
	case 0:
		ZSTDHL_CHECKED(zstdhl_DeflateConv_ConvertRawBlock(state, outTempBlockDesc));
		return ZSTDHL_RESULT_OK;
	case 1:
		ZSTDHL_CHECKED(zstdhl_DeflateConv_ConvertHuffmanBlock(state, outTempBlockDesc, 1));
		return ZSTDHL_RESULT_OK;
	case 2:
		ZSTDHL_CHECKED(zstdhl_DeflateConv_ConvertHuffmanBlock(state, outTempBlockDesc, 0));
		return ZSTDHL_RESULT_OK;
	case 3:
	default:
		return ZSTDHL_RESULT_INVALID_VALUE;
	}
}
