#include "CommonUtils.h"
#include <windows.h>
#include <LogUtils.h>


namespace CommonUtils {
	std::string ToLowerAscii(std::string_view s) {
		std::string out(s);
		for (char& ch : out) {
			if (ch >= 'A' && ch <= 'Z') {
				ch = static_cast<char>(ch - 'A' + 'a');
			}
		}
		return out;
	}

	std::wstring ToLowerAscii(std::wstring_view s) {
		std::wstring out(s);
		for (wchar_t& ch : out) {
			if (ch >= L'A' && ch <= L'Z') {
				ch = static_cast<wchar_t>(ch - L'A' + L'a');
			}
		}
		return out;
	}

	void DumpClass(void * theClassPtr, uint64_t nIntsToDump)
	{
		uint64_t* basePtr = (uint64_t*)theClassPtr;

		_MESSAGE("DumpClass: %016I64X", basePtr);

		if (!theClassPtr) return;
		for (uint64_t ix = 0; ix < nIntsToDump; ix++ ) {
			uint64_t* curPtr = basePtr+ix;
			uint64_t otherPtr = 0;
			uint32_t lowerFloat = 0;
			uint32_t upperFloat = 0;
			float otherFloat1 = 0.0;
			float otherFloat2 = 0.0;

			if (curPtr) {
				__try
				{
					otherPtr = *curPtr;
					lowerFloat = otherPtr & 0xFFFFFFFF;
					upperFloat = (otherPtr >> 32) & 0xFFFFFFFF;
					otherFloat1 = *(float*)&lowerFloat;
					otherFloat2 = *(float*)&upperFloat;
				}
				__except(EXCEPTION_EXECUTE_HANDLER)
				{
					//
				}
			}
			_MESSAGE("%3d +%03X ptr: 0x%016I64X: *ptr: 0x%016I64x | %u, %u, %f, %f", ix, ix*8, curPtr, otherPtr, lowerFloat, upperFloat, otherFloat2, otherFloat1);
		}
	}

	std::string ConvertWStringToCString(std::wstring_view wstr) {
		if (wstr.empty()) {
			return "";
		}
		int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
		std::string result(size_needed, 0);
		WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &result[0], size_needed, NULL, NULL);
		return result;
	}
}