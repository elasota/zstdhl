/*
Copyright (c) 2023 Eric Lasota

This software is available under the terms of the MIT license
or the Apache License, Version 2.0.  For more information, see
the included LICENSE.txt file.
*/
#define GSTD_CONTROL_DECOMPRESSED_SIZE_OFFSET	0
#define GSTD_CONTROL_DECOMPRESSED_SIZE_MASK		0xfffff

#define GSTD_CONTROL_BLOCK_TYPE_OFFSET			20
#define GSTD_CONTROL_BLOCK_TYPE_MASK			3

#define GSTD_CONTROL_MORE_BLOCKS_BIT_OFFSET		22

#define GSTD_CONTROL_AUX_BIT_OFFSET				23
#define GSTD_CONTROL_AUX_BIT_MASK				1

// Compressed blocks
#define GSTD_CONTROL_LIT_SECTION_TYPE_OFFSET	24
#define GSTD_CONTROL_LIT_SECTION_TYPE_MASK		3

#define GSTD_CONTROL_LIT_LENGTH_MODE_OFFSET		26
#define GSTD_CONTROL_LIT_LENGTH_MODE_MASK		3

#define GSTD_CONTROL_OFFSET_MODE_OFFSET			28
#define GSTD_CONTROL_OFFSET_MODE_MASK			3

#define GSTD_CONTROL_MATCH_LENGTH_MODE_OFFSET	30
#define GSTD_CONTROL_MATCH_LENGTH_MODE_MASK		3

// Raw blocks
#define GSTD_CONTROL_RAW_FIRST_BYTE_OFFSET		24
#define GSTD_CONTROL_RAW_FIRST_BYTE_MASK		255


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

#define GSTD_SEQ_COMPRESSION_MODE_PREDEFINED	0
#define GSTD_SEQ_COMPRESSION_MODE_RLE			1
#define GSTD_SEQ_COMPRESSION_MODE_FSE			2
#define GSTD_SEQ_COMPRESSION_MODE_REUSE			3

#define GSTD_BLOCK_TYPE_RAW						0
#define GSTD_BLOCK_TYPE_RLE						1
#define GSTD_BLOCK_TYPE_COMPRESSED				2
#define GSTD_BLOCK_TYPE_RESERVED				3
