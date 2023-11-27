
#define ZSTDHL_CHECKED(n) do {\
		zstdhl_ResultCode_t result = (n);\
		if (result != ZSTDHL_RESULT_OK)\
		{\
			zstdhl_ReportErrorCode(result); \
			return result; \
		}\
	} while(0)

void zstdhl_ReportErrorCode(zstdhl_ResultCode_t errorCode);
