// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProfilingDebugging/MemoryTrace.h"

#include "HAL/MallocMimalloc.h"
#include "ProfilingDebugging/CallstackTrace.h"
#include "ProfilingDebugging/TraceMalloc.h"

#if UE_MEMORY_TRACE_ENABLED

#include "ProfilingDebugging/MemoryAllocationTrace.h"
#include "Containers/StringView.h"
#include "CoreTypes.h"
#include "HAL/MemoryBase.h"
#include "HAL/Platform.h"
#include "HAL/PlatformTime.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CString.h"
#include "Trace/Trace.inl"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <winternl.h>

#include <intrin.h>


////////////////////////////////////////////////////////////////////////////////
void 	MemoryTrace_InitTags(FMalloc*);

////////////////////////////////////////////////////////////////////////////////
template <class T>
class alignas(alignof(T)) FUndestructed
{
public:
	template <typename... ArgTypes>
	void Construct(ArgTypes... Args)
	{
		::new (Buffer) T(Args...);
		bIsConstructed = true;
	}

	bool IsConstructed() const
	{
		return bIsConstructed;
	}

	T* operator & ()	{ return (T*)Buffer; }
	T* operator -> ()	{ return (T*)Buffer; }

protected:
	uint8 Buffer[sizeof(T)];
	bool bIsConstructed;
};

////////////////////////////////////////////////////////////////////////////////
struct FAddrPack
{
			FAddrPack() = default;
			FAddrPack(UPTRINT Addr, uint16 Value) { Set(Addr, Value); }
	void	Set(UPTRINT Addr, uint16 Value) { Inner = uint64(Addr) | (uint64(Value) << 48ull); }
	uint64	Inner;
};
static_assert(sizeof(FAddrPack) == sizeof(uint64), "");


////////////////////////////////////////////////////////////////////////////////
static FUndestructed<FAllocationTrace> GAllocationTrace;
static FUndestructed<FTraceMalloc> GTraceMalloc;

////////////////////////////////////////////////////////////////////////////////
class FMallocWrapper
	: public FMalloc
{
public:
							FMallocWrapper(FMalloc* InMalloc);

private:
	struct FCookie
	{
		uint64				Tag  : 16;
		uint64				Bias : 8;
		uint64				Size : 40;
	};

	static uint32			GetActualAlignment(SIZE_T Size, uint32 Alignment);
	virtual void*			Malloc(SIZE_T Size, uint32 Alignment) override;
	virtual void*			Realloc(void* PrevAddress, SIZE_T NewSize, uint32 Alignment) override;
	virtual void			Free(void* Address) override;
	virtual bool			IsInternallyThreadSafe() const override						{ return InnerMalloc->IsInternallyThreadSafe(); }
	virtual void			UpdateStats() override										{ InnerMalloc->UpdateStats(); }
	virtual void			GetAllocatorStats(FGenericMemoryStats& Out) override		{ InnerMalloc->GetAllocatorStats(Out); }
	virtual void			DumpAllocatorStats(FOutputDevice& Ar) override				{ InnerMalloc->DumpAllocatorStats(Ar); }
	virtual bool			ValidateHeap() override										{ return InnerMalloc->ValidateHeap(); }
	virtual bool			GetAllocationSize(void* Address, SIZE_T &SizeOut) override	{ return InnerMalloc->GetAllocationSize(Address, SizeOut); }
	virtual void			SetupTLSCachesOnCurrentThread() override					{ return InnerMalloc->SetupTLSCachesOnCurrentThread(); }

	FMalloc*				InnerMalloc;
};

////////////////////////////////////////////////////////////////////////////////
FMallocWrapper::FMallocWrapper(FMalloc* InMalloc)
: InnerMalloc(InMalloc)
{
}

////////////////////////////////////////////////////////////////////////////////
uint32 FMallocWrapper::GetActualAlignment(SIZE_T Size, uint32 Alignment)
{
	// Defaults; if size is < 16 then alignment is 8 else 16.
	uint32 DefaultAlignment = 8 << uint32(Size >= 16);
	return (Alignment < DefaultAlignment) ? DefaultAlignment : Alignment;
}

////////////////////////////////////////////////////////////////////////////////
void* FMallocWrapper::Malloc(SIZE_T Size, uint32 Alignment)
{
	if (Size == 0)
	{
		return nullptr;
	}

	uint32 ActualAlignment = GetActualAlignment(Size, Alignment);
	void* Address = InnerMalloc->Malloc(Size, Alignment);

	const uint32 Callstack = CallstackTrace_GetCurrentId();
	GAllocationTrace->Alloc(Address, Size, ActualAlignment, Callstack);

	return Address;
}

////////////////////////////////////////////////////////////////////////////////
void* FMallocWrapper::Realloc(void* PrevAddress, SIZE_T NewSize, uint32 Alignment)
{
	// This simplifies things and means reallocs trace events are true reallocs
	if (PrevAddress == nullptr)
	{
		return Malloc(NewSize, Alignment);
	}

	if (NewSize == 0)
	{
		Free(PrevAddress);
		return nullptr;
	}

	GAllocationTrace->ReallocFree(PrevAddress);

	void* RetAddress = InnerMalloc->Realloc(PrevAddress, NewSize, Alignment);

	const uint32 Callstack = CallstackTrace_GetCurrentId();
	Alignment = GetActualAlignment(NewSize, Alignment);
	GAllocationTrace->ReallocAlloc(RetAddress, NewSize, Alignment, Callstack);

	return RetAddress;
}

////////////////////////////////////////////////////////////////////////////////
void FMallocWrapper::Free(void* Address)
{
	if (Address == nullptr)
	{
		return;
	}

	GAllocationTrace->Free(Address);

	void* InnerAddress = Address;

	return InnerMalloc->Free(InnerAddress);
}



////////////////////////////////////////////////////////////////////////////////
#if defined(PLATFORM_SUPPORTS_TRACE_WIN32_VIRTUAL_MEMORY_HOOKS)
class FTextSectionEditor
{
public:
							FTextSectionEditor(void* InBase);
							~FTextSectionEditor();
	template <typename T>
	T*						Hook(T* Target, T* HookFunction);

private:
	static void*			GetActualAddress(void* Function);
	uint8*					AllocateTrampoline(unsigned int Size);
	void*					HookImpl(void* Target, void* HookFunction);
	uint8*					TrampolineTail;
	void*					Base;
	size_t					Size;
	DWORD					Protection;
};

////////////////////////////////////////////////////////////////////////////////
FTextSectionEditor::FTextSectionEditor(void* InBase)
{
	InBase = GetActualAddress(InBase);

	MEMORY_BASIC_INFORMATION MemInfo;
	VirtualQuery(InBase, &MemInfo, sizeof(MemInfo));
	Base = MemInfo.BaseAddress;
	Size = MemInfo.RegionSize;

	VirtualProtect(Base, Size, PAGE_EXECUTE_READWRITE, &Protection);

	TrampolineTail = (uint8*)Base + Size;
}

////////////////////////////////////////////////////////////////////////////////
FTextSectionEditor::~FTextSectionEditor()
{
	VirtualProtect(Base, Size, Protection, &Protection);
	FlushInstructionCache(GetCurrentProcess(), Base, Size);
}

////////////////////////////////////////////////////////////////////////////////
void* FTextSectionEditor::GetActualAddress(void* Function)
{
	uint8* Addr = (uint8*)Function;
	int Offset = unsigned(Addr[0] & 0xf0) == 0x40; // REX prefix
	if (Addr[Offset + 0] == 0xff && Addr[Offset + 1] == 0x25)
	{
		Addr += Offset;
		Addr = *(uint8**)(Addr + 6 + *(uint32*)(Addr + 2));
	}
	return Addr;
}

////////////////////////////////////////////////////////////////////////////////
uint8* FTextSectionEditor::AllocateTrampoline(unsigned int PatchSize)
{
	static const int TrampolineSize = 24;
	uint8* NextTail = TrampolineTail - TrampolineSize;
	do 
	{
		check(*NextTail == 0);
		++NextTail;
	}
	while (NextTail != TrampolineTail);

	TrampolineTail -= TrampolineSize;
	return TrampolineTail;
}

////////////////////////////////////////////////////////////////////////////////
template <typename T>
T* FTextSectionEditor::Hook(T* Target, T* HookFunction)
{
	return (T*)HookImpl((void*)Target, (void*)HookFunction);
}

////////////////////////////////////////////////////////////////////////////////
void* FTextSectionEditor::HookImpl(void* Target, void* HookFunction)
{
	Target = GetActualAddress(Target);

	const uint8* Start = (uint8*)Target;
	const uint8* Read = Start;
	do
	{
		Read += (Read[0] & 0xf0) == 0x40; // REX prefix
		uint8 Inst = *Read++;
		if (unsigned(Inst - 0x80) < 0x0cu)
		{
			uint8 ModRm = *Read++;
			Read += ((ModRm & 0300) < 0300) & ((ModRm & 0007) == 0004); // SIB
			switch (ModRm & 0300) // Disp[8|32]
			{
			case 0100: Read += 1; break;
			case 0200: Read += 5; break;
			}
			Read += (Inst == 0x83);
		}
		else if (unsigned(Inst - 0x50) >= 0x10u)
			check(!"Unknown instruction");
	}
	while (Read - Start < 6);

	int PatchSize = int(Read - Start);
	uint8* TrampolinePtr = AllocateTrampoline(PatchSize);

	*(void**)TrampolinePtr = HookFunction;

	uint8* PatchJmp = TrampolinePtr + sizeof(void*);
	memcpy(PatchJmp, Start, PatchSize);

	PatchJmp += PatchSize;
	*PatchJmp = 0xe9;
	*(int32*)(PatchJmp + 1) = int32(intptr_t(Start + PatchSize) - intptr_t(PatchJmp)) - 5;

	uint16* HookJmp = (uint16*)Target;
	HookJmp[0] = 0x25ff;
	*(int32*)(HookJmp + 1) = int32(intptr_t(TrampolinePtr) - intptr_t(HookJmp + 3));

	return PatchJmp - PatchSize;
}



////////////////////////////////////////////////////////////////////////////////
class FVirtualWinApiHooks
{
public:
	static void				Initialize(bool bInLight);

private:
							FVirtualWinApiHooks();
	static bool				bLight;
	static LPVOID WINAPI	VmAlloc(LPVOID Address, SIZE_T Size, DWORD Type, DWORD Protect);
	static BOOL WINAPI		VmFree(LPVOID Address, SIZE_T Size, DWORD Type);
	static BOOL WINAPI		VmFreeEx(HANDLE Process, LPVOID Address, SIZE_T Size, DWORD Type);
	static LPVOID WINAPI	VmAllocEx(HANDLE Process, LPVOID Address, SIZE_T Size, DWORD Type, DWORD Protect);
	static LPVOID			(WINAPI *VmAllocOrig)(LPVOID, SIZE_T, DWORD, DWORD);
	static LPVOID			(WINAPI *VmAllocExOrig)(HANDLE, LPVOID, SIZE_T, DWORD, DWORD);
	static BOOL				(WINAPI *VmFreeOrig)(LPVOID, SIZE_T, DWORD);
	static BOOL				(WINAPI *VmFreeExOrig)(HANDLE, LPVOID, SIZE_T, DWORD);
};

////////////////////////////////////////////////////////////////////////////////
bool	FVirtualWinApiHooks::bLight;
LPVOID	(WINAPI *FVirtualWinApiHooks::VmAllocOrig)(LPVOID, SIZE_T, DWORD, DWORD);
LPVOID	(WINAPI *FVirtualWinApiHooks::VmAllocExOrig)(HANDLE, LPVOID, SIZE_T, DWORD, DWORD);
BOOL	(WINAPI *FVirtualWinApiHooks::VmFreeOrig)(LPVOID, SIZE_T, DWORD);
BOOL	(WINAPI *FVirtualWinApiHooks::VmFreeExOrig)(HANDLE, LPVOID, SIZE_T, DWORD);

////////////////////////////////////////////////////////////////////////////////
void FVirtualWinApiHooks::Initialize(bool bInLight)
{
	bLight = bInLight;

	{
		FTextSectionEditor Editor((void*)VirtualAlloc);
		VmAllocOrig = Editor.Hook(VirtualAlloc, &FVirtualWinApiHooks::VmAlloc);
		VmFreeOrig = Editor.Hook(VirtualFree, &FVirtualWinApiHooks::VmFree); 
		
		{
			FTextSectionEditor EditorEx((void*)VirtualAllocEx);
			VmAllocExOrig = Editor.Hook(VirtualAllocEx, &FVirtualWinApiHooks::VmAllocEx);
			VmFreeExOrig = Editor.Hook(VirtualFreeEx, &FVirtualWinApiHooks::VmFreeEx);
		}
	}

	
}

////////////////////////////////////////////////////////////////////////////////
LPVOID WINAPI FVirtualWinApiHooks::VmAlloc(LPVOID Address, SIZE_T Size, DWORD Type, DWORD Protect)
{
	LPVOID Ret = VmAllocOrig(Address, Size, Type, Protect);
	// Track any reserve for now. Going forward we need events to differentiate reserves/commits and
	// corresponding information on frees.
	if (Ret != nullptr && (Type & MEM_RESERVE))
	{
		const uint32 Callstack = FTraceMalloc::ShouldTrace() ? CallstackTrace_GetCurrentId() : 0;
		GAllocationTrace->Alloc(Ret, Size, 0, Callstack, EMemoryTraceRootHeap::SystemMemory);
		GAllocationTrace->MarkAllocAsHeap(Ret, EMemoryTraceRootHeap::SystemMemory);
	}

	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
BOOL WINAPI FVirtualWinApiHooks::VmFree(LPVOID Address, SIZE_T Size, DWORD Type)
{
	// Currently tracking any free event
	if (Type & MEM_RELEASE)
	{
		GAllocationTrace->Free(Address, EMemoryTraceRootHeap::SystemMemory);
	}
	return VmFreeOrig(Address, Size, Type);
}

////////////////////////////////////////////////////////////////////////////////
LPVOID WINAPI FVirtualWinApiHooks::VmAllocEx(HANDLE Process, LPVOID Address, SIZE_T Size, DWORD Type, DWORD Protect)
{
	LPVOID Ret = VmAllocExOrig(Process, Address, Size, Type, Protect);
	if (Process == GetCurrentProcess() && Ret != nullptr && (Type & MEM_RESERVE))
	{
		const uint32 Callstack = FTraceMalloc::ShouldTrace() ? CallstackTrace_GetCurrentId() : 0;
		GAllocationTrace->Alloc(Ret, Size, 0, Callstack);
		GAllocationTrace->MarkAllocAsHeap(Ret, EMemoryTraceRootHeap::SystemMemory);
	}

	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
BOOL WINAPI FVirtualWinApiHooks::VmFreeEx(HANDLE Process, LPVOID Address, SIZE_T Size, DWORD Type)
{
	if (Process == GetCurrentProcess() && (Type & MEM_RELEASE))
	{
		GAllocationTrace->Free(Address, EMemoryTraceRootHeap::SystemMemory);
	}

	return VmFreeExOrig(Process, Address, Size, Type);
}
#endif // defined(PLATFORM_SUPPORTS_TRACE_WIN32_VIRTUAL_MEMORY_HOOKS)


////////////////////////////////////////////////////////////////////////////////
FMalloc* MemoryTrace_Create(FMalloc* InMalloc)
{
	int32 Mode = 0;
	const TCHAR* CmdLine = ::GetCommandLineW();
	if (const TCHAR* TraceArg = FCString::Strstr(CmdLine, TEXT("-trace=")))
	{
		const TCHAR* Start = TraceArg + 7;
		const TCHAR* End = Start;
		for (; *End != ' ' && *End != '\0'; ++End);

		for (const TCHAR* c = Start; c < End + 1; ++c)
		{
			if (*c == ' ' || *c == '\0' || *c == ',')
			{
				FStringView View(Start, uint32(c - Start));
				if (View.Equals(TEXT("memalloc"), ESearchCase::IgnoreCase) || View.Equals(TEXT("memory"), ESearchCase::IgnoreCase))
				{
					Mode = 2;
					break;
				}

				Start = c + 1;
			}
		}
	}

	if (Mode > 0)
	{
		// Some OSes (i.e. Windows) will terminate all threads except the main
		// one as part of static deinit. However we may receive more memory
		// trace events that would get lost as Trace's worker thread has been
		// terminated. So flush the last remaining memory events trace needs
		// to be updated which we will do that in response to to memory events.
		// We'll use an atexit can to know when Trace is probably no longer
		// getting ticked.
		atexit([] () { GAllocationTrace->EnableTracePump(); });

		GAllocationTrace.Construct();
		GAllocationTrace->Initialize();

		GTraceMalloc.Construct(InMalloc);

		// Both tag and callstack tracing need to use the wrapped trace malloc
		// so we can break out tracing memory overhead (and not cause recursive behaviour).
		MemoryTrace_InitTags(&GTraceMalloc);
		CallstackTrace_Create(&GTraceMalloc);

#if defined(PLATFORM_SUPPORTS_TRACE_WIN32_VIRTUAL_MEMORY_HOOKS)
		FVirtualWinApiHooks::Initialize(false);
#endif // defined(PLATFORM_SUPPORTS_TRACE_WIN32_VIRTUAL_MEMORY_HOOKS)

		static FUndestructed<FMallocWrapper> SMallocWrapper;
		SMallocWrapper.Construct(InMalloc);

		return &SMallocWrapper;
	}

	return InMalloc;
}

////////////////////////////////////////////////////////////////////////////////
void MemoryTrace_Initialize()
{
	// Allocators aren't completely ready in _Create() so we have an extra step
	// where any initialisation that may allocate can go.
	CallstackTrace_Initialize();
}


////////////////////////////////////////////////////////////////////////////////
HeapId MemoryTrace_HeapSpec(HeapId ParentId, const TCHAR* Name, EMemoryTraceHeapFlags Flags)
{
	if (GAllocationTrace.IsConstructed())
	{
		return GAllocationTrace->HeapSpec(ParentId, Name, Flags);
	}
	return ~0;
}

////////////////////////////////////////////////////////////////////////////////
HeapId MemoryTrace_RootHeapSpec(const TCHAR* Name, EMemoryTraceHeapFlags Flags)
{
	if (GAllocationTrace.IsConstructed())
	{
		return GAllocationTrace->RootHeapSpec(Name, Flags);
	}
	return ~0;
}

////////////////////////////////////////////////////////////////////////////////
void MemoryTrace_MarkAllocAsHeap(uint64 Address, HeapId Heap, EMemoryTraceHeapAllocationFlags Flags)
{
	if (GAllocationTrace.IsConstructed())
	{
		GAllocationTrace->MarkAllocAsHeap((void*)Address, Heap, Flags);
	}
}

////////////////////////////////////////////////////////////////////////////////
void MemoryTrace_UnmarkAllocAsHeap(uint64 Address, HeapId Heap)
{
	if (GAllocationTrace.IsConstructed())
	{
		GAllocationTrace->UnmarkAllocAsHeap((void*)Address, Heap);
	}
}

////////////////////////////////////////////////////////////////////////////////
void MemoryTrace_Alloc(uint64 Address, uint64 Size, uint32 Alignment, HeapId RootHeap /*= EMemoryTraceRootHeap::SystemMemory*/)
{
	if (GAllocationTrace.IsConstructed())
	{
		GAllocationTrace->Alloc((void*)Address, Size, Alignment, CallstackTrace_GetCurrentId(), RootHeap);
	}
}

////////////////////////////////////////////////////////////////////////////////
void MemoryTrace_Free(uint64 Address, HeapId RootHeap /*= EMemoryTraceRootHeap::SystemMemory*/)
{
	if (GAllocationTrace.IsConstructed())
	{
		GAllocationTrace->Free((void*)Address, RootHeap);
	}
}

////////////////////////////////////////////////////////////////////////////////
void MemoryTrace_ReallocFree(uint64 Address, HeapId RootHeap /*= EMemoryTraceRootHeap::SystemMemory*/)
{
	if (GAllocationTrace.IsConstructed())
	{
		GAllocationTrace->ReallocFree((void*)Address, RootHeap);
	}
}

////////////////////////////////////////////////////////////////////////////////
void MemoryTrace_ReallocAlloc(uint64 Address, uint64 NewSize, uint32 Alignment, HeapId RootHeap /*= EMemoryTraceRootHeap::SystemMemory*/)
{
	if (GAllocationTrace.IsConstructed())
	{
		GAllocationTrace->ReallocAlloc((void*)Address, NewSize, Alignment, CallstackTrace_GetCurrentId(), RootHeap);
	}
}

#include "Windows/HideWindowsPlatformTypes.h"

#endif // UE_MEMORY_TRACE_ENABLED
