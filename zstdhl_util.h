/*
Copyright (c) 2023 Eric Lasota

This software is available under the terms of the MIT license
or the Apache License, Version 2.0.  For more information, see
the included LICENSE.txt file.
*/

#define ZSTDHL_CHECKED(n) do {\
		zstdhl_ResultCode_t result = (n);\
		if (result != ZSTDHL_RESULT_OK)\
		{\
			zstdhl_ReportErrorCode(result); \
			return result; \
		}\
	} while(0)

void zstdhl_ReportErrorCode(zstdhl_ResultCode_t errorCode);
