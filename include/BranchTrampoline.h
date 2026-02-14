#pragma once
#include <cstdint>
#include <safetyhook.hpp>
class BranchTrampoline
{
public:
	BranchTrampoline();
	~BranchTrampoline();

	bool Create(size_t len, void * module = nullptr);
	void Destroy();

	// allocate unsized 
	void * StartAlloc();
	void EndAlloc(const void * end);

	void * Allocate(size_t size = sizeof(void *));

	size_t Remain()	{ return m_len - m_allocated; }

	// takes 6 bytes of space at src, 8 bytes in trampoline
	bool Write6Branch(uintptr_t src, uintptr_t dst);
	bool Write6Call(uintptr_t src, uintptr_t dst);

	// takes 5 bytes of space at src, 14 bytes in trampoline
	uintptr_t Write5Branch(uintptr_t src, uintptr_t dst);
	uintptr_t Write5Call(uintptr_t src, uintptr_t dst);

	template <class TRet, class... TArg>
	inline auto Write5Call(uintptr_t src, TRet(*fn)(TArg...)) -> decltype(fn) {
		typedef decltype(fn) Fn;
		return (Fn)Write5Call(src, (uintptr_t)fn);
	}

private:
	// takes 6 bytes of space at src, 8 bytes in trampoline
	bool Write6Branch_Internal(uintptr_t src, uintptr_t dst, uint8_t op);

	// takes 5 bytes of space at src, 14 bytes in trampoline
	uintptr_t Write5Branch_Internal(uintptr_t src, uintptr_t dst, uint8_t op);

	void	* m_base;
	size_t	m_len;			// bytes
	size_t	m_allocated;	// bytes

	void	* m_curAlloc;	// currently active StartAlloc base

	safetyhook::Allocation m_trampoline;

	std::shared_ptr<safetyhook::Allocator> m_allocator;
};

extern BranchTrampoline g_branchTrampoline;
