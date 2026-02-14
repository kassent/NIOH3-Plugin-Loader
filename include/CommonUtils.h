#pragma once

#include <string>
#include <cstdint>
#include <string_view>
#include "Relocation.h"

#define FORCE_INLINE  __forceinline


#define DEF_MEMBER_FN(fnName, retnType, addr, ...)								\
	template <class... Params>													\
	FORCE_INLINE retnType fnName(Params&&... params) {							\
		struct empty_struct {};													\
		typedef retnType(empty_struct::*_##fnName##_type)(__VA_ARGS__);			\
		const static uintptr_t address = _##fnName##_GetFnPtr();				\
		_##fnName##_type fn = *(_##fnName##_type*)&address;						\
		return (reinterpret_cast<empty_struct*>(this)->*fn)(params...);			\
	}																			\
	static uintptr_t & _##fnName##_GetFnPtr()	{								\
		static uintptr_t relMem = addr + RelocationManager::s_baseAddr;			\
		return relMem;															\
	}

#define DEF_MEMBER_FN_CONST(fnName, retnType, addr, ...)						\
	template <class... Params>													\
	FORCE_INLINE retnType fnName(Params&&... params) const {					\
		struct empty_struct {};													\
		typedef retnType(empty_struct::*_##fnName##_type)(__VA_ARGS__) const;	\
		const static uintptr_t address = _##fnName##_GetFnPtr();				\
		_##fnName##_type fn = *(_##fnName##_type*)&address;						\
		return (reinterpret_cast<const empty_struct*>(this)->*fn)(params...);	\
	}																			\
	static uintptr_t & _##fnName##_GetFnPtr()	{								\
		static uintptr_t relMem = addr + RelocationManager::s_baseAddr;			\
		return relMem;															\
	}
namespace CommonUtils {
    std::string ConvertWStringToCString(std::wstring_view wstr);
    void DumpClass(void * theClassPtr, uint64_t nIntsToDump);
    std::string ToLowerAscii(std::string_view s);
    std::wstring ToLowerAscii(std::wstring_view s);
}