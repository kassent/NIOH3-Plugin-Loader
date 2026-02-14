#include "BranchTrampoline.h"
#include "HookUtils.h"
#include "LogUtils.h"

#define ASSERT(condition)

BranchTrampoline g_branchTrampoline;

BranchTrampoline::BranchTrampoline()
	:m_base(nullptr)
	,m_len(0)
	,m_allocated(0)
	,m_curAlloc(nullptr)
{
	//
}

BranchTrampoline::~BranchTrampoline()
{
	Destroy();
}

bool BranchTrampoline::Create(size_t len, void * module)
{
	if (len >= 1000) {
		_MESSAGE("BranchTrampoline::Create: len too large");
		return false;
	}
	if(!module) module = GetModuleHandle(NULL);

	// search backwards from module base
	uintptr_t moduleBase = uintptr_t(module);
	uintptr_t moduleSize = HookUtils::GetModuleSize((HMODULE)module).value_or(0);
	uintptr_t halfwayModule = moduleBase + (moduleSize / 2);
	
	// Need to pin the safetyhook allocator because it destroys itself
	static auto shAllocator = safetyhook::Allocator::global();
	// 1MB
	intptr_t requestedSize = len;
	// Keep attempting to allocate and reduce requested size by 1 page until successful
	// 1MB is just the higher end of the range, it will allocate less if it can
	// even 10kb would be enough for a decent amount of hooks, but we want as much as we can near the middle
	auto trampolineAllocation = shAllocator->allocate_near({(uint8_t*)halfwayModule}, requestedSize);
	if (!trampolineAllocation) {
		_MESSAGE("Failed to allocate trampoline");
		return false;
	}
	m_trampoline = std::move(*trampolineAllocation);

	if (!m_trampoline) {
		return false;
	}

	m_base = m_trampoline.data();
	m_len = m_trampoline.size();
	m_allocated = 0;

	return m_base != nullptr;
}

void BranchTrampoline::Destroy()
{
	if(m_base)
	{
		VirtualFree(m_base, 0, MEM_RELEASE);
		m_base = nullptr;
	}
}

void * BranchTrampoline::StartAlloc()
{
	ASSERT(m_base);
	ASSERT(!m_curAlloc);

	m_curAlloc = ((uint8_t*)m_base) + m_allocated;

	return m_curAlloc;
}

void BranchTrampoline::EndAlloc(const void * end)
{
	ASSERT(m_base);
	ASSERT(m_curAlloc);

	size_t len = uintptr_t(end) - uintptr_t(m_curAlloc);
	ASSERT(len <= Remain());

	m_allocated += len;
	m_curAlloc = nullptr;
}

void * BranchTrampoline::Allocate(size_t size)
{
	ASSERT(m_base);

	void * result = nullptr;

	if(size <= Remain())
	{
		result = ((uint8_t*)m_base) + m_allocated;
		m_allocated += size;
	}

	return result;
}

bool BranchTrampoline::Write6Branch(uintptr_t src, uintptr_t dst)
{
	return Write6Branch_Internal(src, dst, 0x25);
}

bool BranchTrampoline::Write6Call(uintptr_t src, uintptr_t dst)
{
	return Write6Branch_Internal(src, dst, 0x15);
}

uintptr_t BranchTrampoline::Write5Branch(uintptr_t src, uintptr_t dst)
{
	return Write5Branch_Internal(src, dst, 0xE9);
}

uintptr_t BranchTrampoline::Write5Call(uintptr_t src, uintptr_t dst)
{
	return Write5Branch_Internal(src, dst, 0xE8);
}

bool BranchTrampoline::Write6Branch_Internal(uintptr_t src, uintptr_t dst, uint8_t op)
{
	bool result = false;

	uintptr_t * trampoline = (uintptr_t *)Allocate();
	if(trampoline)
	{
		uintptr_t	trampolineAddr = (uintptr_t)trampoline;
		uintptr_t	nextInstr = src + 6;
		ptrdiff_t	trampolineDispl = trampolineAddr - nextInstr;

		if((trampolineDispl >= _I32_MIN) && (trampolineDispl <= _I32_MAX))
		{
			uint8_t	code[6];

			// jmp [rip+imm32]
			code[0] = 0xFF;
			code[1] = op;
			*((int32_t*)&code[2]) = (int32_t)trampolineDispl;

			HookUtils::SafeWriteBuf(src, code, sizeof(code));

			*trampoline = dst;

			result = true;

#if defined(_DEBUG)
			_MESSAGE("Write6Branch: %016I64X %016I64X %016I64X", src, dst, trampoline);
#endif
		}
	}

	// do this for now so it's obvious when something goes wrong
	ASSERT(result);

	return result;
}

uintptr_t BranchTrampoline::Write5Branch_Internal(uintptr_t src, uintptr_t dst, uint8_t op)
{
	uintptr_t result = 0;

#pragma pack(push, 1)
	// code placed in trampoline
	struct TrampolineCode
	{
		// jmp [rip]
		uint8_t	escape;	// FF
		uint8_t	modrm;	// 25
		uint32_t	displ;	// 00000000
		// rip points here
		uint64_t	dst;	// target

		void Init(uintptr_t _dst)
		{
			escape = 0xFF;
			modrm = 0x25;
			displ = 0;
			dst = _dst;
		}
	};

	struct HookCode
	{
		// jmp disp32
		uint8_t	op;		// E9 for jmp, E8 for call
		int32_t	displ;	// 

		void Init(int32_t _displ, uint8_t _op)
		{
			op = _op;
			displ = _displ;
		}
	};
#pragma pack(pop)

	static_assert(sizeof(TrampolineCode) == 14);
	static_assert(sizeof(HookCode) == 5);

	TrampolineCode * trampolineCode = (TrampolineCode *)Allocate(sizeof(TrampolineCode));
	if(trampolineCode)
	{
		trampolineCode->Init(dst);

		HookCode hookCode;

		uintptr_t	trampolineAddr = uintptr_t(trampolineCode);
		uintptr_t	nextInstr = src + sizeof(hookCode);
		ptrdiff_t	trampolineDispl = trampolineAddr - nextInstr;

		// should never fail because we're branching in to the trampoline
		ASSERT((trampolineDispl >= _I32_MIN) && (trampolineDispl <= _I32_MAX));

		hookCode.Init((int32_t)trampolineDispl, op);
		result = HookUtils::ReadOffsetData(src, 1, 5);
		HookUtils::SafeWriteBuf(src, &hookCode, sizeof(hookCode));

#if defined(_DEBUG)
		_MESSAGE("Write5Branch: %016I64X %016I64X %016I64X", src, dst, trampolineCode);
#endif
	}

	// do this for now so it's obvious when something goes wrong
	ASSERT(result);

	return result;
}
