#pragma once
#include <safetyhook.hpp>
#include <Windows.h>
#include <cstdint>

template<typename R, typename ...Args>
SafetyHookInline CreateHookFunction(R(*target)(Args...), R(*hook)(Args...)) {
	return safetyhook::create_inline(reinterpret_cast<void*>(target), reinterpret_cast<void*>(hook));
}

#define HookLambda(TARGET, LAMBDA) do {\
	static decltype(TARGET) original = nullptr;\
	static auto trampoline = CreateHookFunction(TARGET, (decltype(TARGET))LAMBDA); \
	if (trampoline) { \
		original = (decltype(TARGET))trampoline.trampoline().address();\
	} else { \
		_MESSAGE("Failed to create hook with addr: %p, file: %s, line: %d", TARGET, __FILE__, __LINE__); \
	} \
} while(false)


namespace HookUtils {
    // Find the address of the call instruction in the caller function that calls the callee function
    // Returns the address of the call instruction (pointer to E8), or nullptr if not found
    // stopAtCC: Whether to stop searching when encountering an int 3 (0xCC) instruction
    // maxSearch: Maximum search range to prevent overflow
    void* LookupCallee(void* caller, void* callee, bool stopAtCC = true, size_t maxSearch = 1024);

	// Find the address of the function pattern in the caller function
	// Returns the address of the function pattern, or nullptr if not found
	// func: The caller function
	// signature: The function pattern
	// scanSize: The size of the scan range
	uintptr_t LookupFunctionPattern(void *func, std::string_view signature, uint32_t scanSize);


	void SafeWriteBuf(uintptr_t addr, void * data, size_t len);
	bool SafeReadBuf(uintptr_t addr, void* data, size_t len);
	void SafeWrite8(uintptr_t addr, uint8_t data);
	void SafeWrite16(uintptr_t addr, uint16_t data);
	void SafeWrite32(uintptr_t addr, uint32_t data);
	uint64_t SafeWrite64(uintptr_t addr, uint64_t data);

	// ### warning: if you try to branch more than +/- 2GB with these, they will fail and return false
	// ### this is a limitation of the 'jmp' instruction and more generally the x64 ISA
	// 5 bytes written to src
	bool SafeWriteJump(uintptr_t src, uintptr_t dst);
	bool SafeWriteCall(uintptr_t src, uintptr_t dst);

	template <class Ty, class TRet, class... TArg>
	inline auto SafeWrite64(uintptr_t jumpSrc, TRet(Ty::* fn)(TArg...)) -> decltype(fn)
	{
		typedef decltype(fn) Fn;
		union
		{
			uintptr_t	u64;
			Fn			fn;
		} data;

		data.fn = fn;

		data.u64 = SafeWrite64(jumpSrc, data.u64);
		return data.fn;
	}

	template <class TRet, class... TArg>
	inline auto SafeWrite64(uintptr_t jumpSrc, TRet(*fn)(TArg...)) -> decltype(fn)
	{
		typedef decltype(fn) Fn;
		return (Fn)SafeWrite64(jumpSrc, (uintptr_t)fn);
	}

	uintptr_t ReadOffsetData(uintptr_t location, int32_t relOffset, uint32_t instructionLen);

	// Get the address of an import in a DLL
	void *GetIATAddr(void* module, const char *searchDllName, const char *searchImportName);

	std::optional<size_t> GetModuleSize(HMODULE module);

	uintptr_t ScanIDAPattern(std::string_view signature, int32_t offset = 0, int32_t relOffset = 0, int32_t instructionLength = 0);
}