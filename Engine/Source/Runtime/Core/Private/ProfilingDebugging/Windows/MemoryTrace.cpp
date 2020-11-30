// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProfilingDebugging/MemoryTrace.h"

#if UE_MEMORY_TRACE_ENABLED

#include "Containers/StringView.h"
#include "CoreTypes.h"
#include "HAL/MemoryBase.h"
#include "HAL/Platform.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CString.h"
#include "Trace/Trace.inl"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <winternl.h>

#include <atomic>
#include <intrin.h>

#define USE_OVERVIEW_TRACE 0 // Disabled for now, as it occasionally conflicts with MallocBinned2's canary poison logic.

////////////////////////////////////////////////////////////////////////////////
namespace UE {
namespace Trace {

TRACELOG_API void Update();

} // namespace Trace
} // namespace UE

////////////////////////////////////////////////////////////////////////////////
void	Backtracer_Create(FMalloc*);
void	Backtracer_Initialize();
void*	Backtracer_GetBacktraceId(void*);

////////////////////////////////////////////////////////////////////////////////
#if defined(_MSC_VER)
#	define UE_RETURN_ADDRESS_ADDRESS()	(void*)_AddressOfReturnAddress()
#elif defined(__clang__) || defined(__GNUC__)
#	define UE_RETURN_ADDRESS_ADDRESS()	__builtin_frame_address(0)
#endif

FORCEINLINE void* GetOwner(bool bLight)
{
	void* RetAddrAddr = UE_RETURN_ADDRESS_ADDRESS();
	if (bLight)
	{
		return *(void**)RetAddrAddr;
	}

	return Backtracer_GetBacktraceId(RetAddrAddr);
}

////////////////////////////////////////////////////////////////////////////////
template <class T>
class alignas(alignof(T)) FUndestructed
{
public:
	template <typename... ArgTypes>
	void Construct(ArgTypes... Args)
	{
		::new (Buffer) T(Args...);
	}

	T* operator & ()	{ return (T*)Buffer; }
	T* operator -> ()	{ return (T*)Buffer; }

protected:
	uint8 Buffer[sizeof(T)];
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
UE_TRACE_CHANNEL(MemSummaryChannel)
UE_TRACE_CHANNEL_DEFINE(MemAllocChannel)

UE_TRACE_EVENT_BEGIN(Memory, Init)
	UE_TRACE_EVENT_FIELD(uint8, MinAlignment)
	UE_TRACE_EVENT_FIELD(uint8, SizeShift)
#if USE_OVERVIEW_TRACE
	UE_TRACE_EVENT_FIELD(uint8, SummarySizeShift)
#endif
	UE_TRACE_EVENT_FIELD(uint8, Mode)
UE_TRACE_EVENT_END()

#if USE_OVERVIEW_TRACE
UE_TRACE_EVENT_BEGIN(Memory, Summary)
	UE_TRACE_EVENT_FIELD(uint32, Bytes)
	UE_TRACE_EVENT_FIELD(uint32, ActiveAllocs)
	UE_TRACE_EVENT_FIELD(uint32, TotalAllocs)
	UE_TRACE_EVENT_FIELD(uint32, TotalReallocs)
	UE_TRACE_EVENT_FIELD(uint32, TotalFrees)
UE_TRACE_EVENT_END()
#endif

UE_TRACE_EVENT_BEGIN(Memory, CoreAdd)
	UE_TRACE_EVENT_FIELD(uint64, Owner)
	UE_TRACE_EVENT_FIELD(void*, Base)
	UE_TRACE_EVENT_FIELD(uint32, Size)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Memory, CoreRemove)
	UE_TRACE_EVENT_FIELD(uint64, Owner)
	UE_TRACE_EVENT_FIELD(void*, Base)
	UE_TRACE_EVENT_FIELD(uint32, Size)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Memory, Alloc)
	UE_TRACE_EVENT_FIELD(uint64, Owner)
	UE_TRACE_EVENT_FIELD(void*, Address)
	UE_TRACE_EVENT_FIELD(uint32, Size)
	UE_TRACE_EVENT_FIELD(uint8, Alignment_SizeLower)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Memory, Free)
	UE_TRACE_EVENT_FIELD(void*, Address)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Memory, ReallocAlloc)
	UE_TRACE_EVENT_FIELD(uint64, Owner)
	UE_TRACE_EVENT_FIELD(void*, Address)
	UE_TRACE_EVENT_FIELD(uint32, Size)
	UE_TRACE_EVENT_FIELD(uint8, Alignment_SizeLower)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Memory, ReallocFree)
	UE_TRACE_EVENT_FIELD(void*, Address)
UE_TRACE_EVENT_END()



#if USE_OVERVIEW_TRACE

////////////////////////////////////////////////////////////////////////////////
class FSummaryTrace
{
public:
	static const uint32			SizeShift = 4;
	void						Initialize();
	uint32						SetTag(uint32 Tag);
	uint32						Alloc(SIZE_T Size, bool IsRealloc);
	void						Free(uint32 Tag, SIZE_T Size);
	void						Flush();

private:
	template <typename T>
	struct FItem
	{
		std::atomic<T>			Current = 0;
		T						Max = 0;
		T						Min = 0;
		void					Add(T Operand);
	};

	struct alignas(PLATFORM_CACHE_LINE_SIZE) FInfo
	{
		uint32					Tag;
		FItem<int64>			Bytes;
		FItem<int32>			ActiveAllocs;
		std::atomic<uint32>		TotalAllocs = 0;
		std::atomic<uint32>		TotalReallocs = 0;
		std::atomic<uint32>		TotalFrees = 0;
	};

	FInfo&						GetInfo();
	static thread_local FInfo*	InfoPtr;
	std::atomic<uint32>			InfoIndex;
	FInfo						InfoPool[1];
};

////////////////////////////////////////////////////////////////////////////////
static FUndestructed<FSummaryTrace>	GSummaryTrace;
thread_local FSummaryTrace::FInfo*	FSummaryTrace::InfoPtr = nullptr;

////////////////////////////////////////////////////////////////////////////////
template <typename T>
void FSummaryTrace::FItem<T>::Add(T Size)
{
	T Value = Current.fetch_add(T(Size), std::memory_order_relaxed);
	Value += Size;
	Max = (Current > Max) ? Value : Max;
	Min = (Current < Min) ? Value : Min;
}

////////////////////////////////////////////////////////////////////////////////
void FSummaryTrace::Initialize()
{
	FCoreDelegates::OnEndFrame.AddStatic([] () { GSummaryTrace->Flush(); });
}

////////////////////////////////////////////////////////////////////////////////
uint32 FSummaryTrace::SetTag(uint32 Tag)
{
	FInfo& Info = GetInfo();
	uint32 PrevTag = Info.Tag;
	Info.Tag = Tag;
	return PrevTag;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FSummaryTrace::Alloc(SIZE_T Size, bool IsRealloc)
{
	FInfo& Info = GetInfo();
	Info.Bytes.Add(Size);
	Info.ActiveAllocs.Add(1);
	Info.TotalAllocs.fetch_add(1, std::memory_order_relaxed);
	if (IsRealloc)
	{
		Info.TotalReallocs.fetch_add(1, std::memory_order_relaxed);
	}
	return Info.Tag;
}

////////////////////////////////////////////////////////////////////////////////
void FSummaryTrace::Free(uint32 Tag, SIZE_T Size)
{
	FInfo& Info = GetInfo();
	Info.Bytes.Add(-int64(Size));
	Info.ActiveAllocs.Add(-1);
	Info.TotalFrees.fetch_add(1, std::memory_order_relaxed);
}

////////////////////////////////////////////////////////////////////////////////
FSummaryTrace::FInfo& FSummaryTrace::GetInfo()
{
	FInfo* Info = InfoPtr;
	if (Info == nullptr)
	{
		uint32 Index = InfoIndex.fetch_add(1, std::memory_order_relaxed);
		Index &= UE_ARRAY_COUNT(InfoPool) - 1;
		Info = InfoPtr = InfoPool + Index;
	}
	return *Info;
}

////////////////////////////////////////////////////////////////////////////////
void FSummaryTrace::Flush()
{
	int64 Bytes = 0;
	int32 Active = 0;
	int32 TotalAllocs = 0;
	int32 TotalReallocs = 0;
	int32 TotalFrees = 0;
	for (const FInfo& Info : InfoPool)
	{
		Bytes += Info.Bytes.Current.load(std::memory_order_relaxed);
		Active += Info.ActiveAllocs.Current.load(std::memory_order_relaxed);
		TotalAllocs += Info.TotalAllocs.load(std::memory_order_relaxed);
		TotalReallocs += Info.TotalReallocs.load(std::memory_order_relaxed);
		TotalFrees += Info.TotalFrees.load(std::memory_order_relaxed);
	}
	
	UE_TRACE_LOG(Memory, Summary, MemSummaryChannel)
		<< Summary.Bytes(uint32(Bytes >> SizeShift))
		<< Summary.ActiveAllocs(Active)
		<< Summary.TotalAllocs(TotalAllocs)
		<< Summary.TotalReallocs(TotalReallocs)
		<< Summary.TotalFrees(TotalFrees);
}

#endif // USE_OVERVIEW_TRACE



////////////////////////////////////////////////////////////////////////////////
class FAllocationTrace
{
public:
	void	Initialize();
	void	EnableTracePump();
	void	CoreAdd(void* Base, size_t Size, void* Owner);
	void	CoreRemove(void* Base, size_t Size, void* Owner);
	void	Alloc(void* Address, size_t Size, uint32 Alignment, void* Owner);
	void	Free(void* Address);
	void	ReallocAlloc(void* Address, size_t Size, uint32 Alignment, void* Owner);
	void	ReallocFree(void* Address);

private:
	void				Update();
	bool				bPumpTrace = false;
	static const uint32 SizeShift = 3;
	static_assert(MIN_ALIGNMENT >= (1 << SizeShift), "");
};

////////////////////////////////////////////////////////////////////////////////
static FUndestructed<FAllocationTrace> GAllocationTrace;

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::Initialize()
{
	UE_TRACE_LOG(Memory, Init, MemAllocChannel)
		<< Init.MinAlignment(uint8(MIN_ALIGNMENT))
#if USE_OVERVIEW_TRACE
		<< Init.SummarySizeShift(uint8(FSummaryTrace::SizeShift))
#endif
		<< Init.SizeShift(uint8(SizeShift));

	static_assert((1 < SizeShift) - 1 <= MIN_ALIGNMENT, "Not enough bits to pack size fields");
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::EnableTracePump()
{
	bPumpTrace = true;
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::Update()
{
	if (bPumpTrace)
	{
		UE::Trace::Update();
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::CoreAdd(void* Base, size_t Size, void* Owner)
{
	UE_TRACE_LOG(Memory, CoreAdd, MemAllocChannel)
		<< CoreAdd.Owner(uint64(Owner))
		<< CoreAdd.Base(Base)
		<< CoreAdd.Size(uint32(Size >> SizeShift));

	Update();
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::CoreRemove(void* Base, size_t Size, void* Owner)
{
	UE_TRACE_LOG(Memory, CoreRemove, MemAllocChannel)
		<< CoreRemove.Owner(uint64(Owner))
		<< CoreRemove.Base(Base)
		<< CoreRemove.Size(uint32(Size >> SizeShift));

	Update();
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::Alloc(void* Address, size_t Size, uint32 Alignment, void* Owner)
{
	uint32 ActualAlignment = Alignment > uint32(MIN_ALIGNMENT) ? Alignment : uint32(MIN_ALIGNMENT);
	uint32 Alignment_SizeLower = ActualAlignment | (Size & ((1 << SizeShift) - 1));

	UE_TRACE_LOG(Memory, Alloc, MemAllocChannel)
		<< Alloc.Owner(uint64(Owner))
		<< Alloc.Address(Address)
		<< Alloc.Size(uint32(Size >> SizeShift))
		<< Alloc.Alignment_SizeLower(uint8(Alignment_SizeLower));

	Update();
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::Free(void* Address)
{
	UE_TRACE_LOG(Memory, Free, MemAllocChannel)
		<< Free.Address(Address);

	Update();
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::ReallocAlloc(void* Address, size_t Size, uint32 Alignment, void* Owner)
{
	uint32 ActualAlignment = Alignment > uint32(MIN_ALIGNMENT) ? Alignment : uint32(MIN_ALIGNMENT);
	uint32 Alignment_SizeLower = ActualAlignment | (Size & ((1 << SizeShift) - 1));

	UE_TRACE_LOG(Memory, ReallocAlloc, MemAllocChannel)
		<< ReallocAlloc.Owner(uint64(Owner))
		<< ReallocAlloc.Address(Address)
		<< ReallocAlloc.Size(uint32(Size >> SizeShift))
		<< ReallocAlloc.Alignment_SizeLower(uint8(Alignment_SizeLower));

	Update();
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::ReallocFree(void* Address)
{
	UE_TRACE_LOG(Memory, ReallocFree, MemAllocChannel)
		<< ReallocFree.Address(Address);

	Update();
}



////////////////////////////////////////////////////////////////////////////////
class FMallocWrapper
	: public FMalloc
{
public:
							FMallocWrapper(FMalloc* InMalloc, bool bInLight);

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
	FMalloc*				InnerMalloc;
	bool					bLight;
};

////////////////////////////////////////////////////////////////////////////////
FMallocWrapper::FMallocWrapper(FMalloc* InMalloc, bool bInLight)
: InnerMalloc(InMalloc)
, bLight(bInLight)
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

	SIZE_T InnerSize = Size;
#if USE_OVERVIEW_TRACE
	static_assert(sizeof(FCookie) <= 8, "Unexpected FCookie size");
	InnerSize += ActualAlignment;
#endif 

	void* Address = InnerMalloc->Malloc(InnerSize, Alignment);

#if USE_OVERVIEW_TRACE
	Address = (uint8*)Address + ActualAlignment;
	FCookie* Cookie = (FCookie*)Address - 1;
	Cookie->Size = Size;
	Cookie->Tag = GSummaryTrace->Alloc(Size, false);
	Cookie->Bias = ActualAlignment >> 3;
#endif

	void* Owner = GetOwner(bLight);
	GAllocationTrace->Alloc(Address, Size, ActualAlignment, Owner);

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

	// Track the block that will (or might) get freed
	uint32 HeaderSize = 0;
	void* InnerAddress = PrevAddress;
#if USE_OVERVIEW_TRACE
	FCookie* Cookie = (FCookie*)PrevAddress - 1;
	HeaderSize = uint32(Cookie->Bias << 3);
	InnerAddress = (uint8*)PrevAddress - HeaderSize;
	GSummaryTrace->Free(Cookie->Tag, Cookie->Size);
#endif

	GAllocationTrace->ReallocFree(PrevAddress);

	// Do the actual malloc
	SIZE_T InnerSize = NewSize + HeaderSize;
	void* RetAddress = InnerMalloc->Realloc(InnerAddress, InnerSize, Alignment);

	// Track the block that was allocated
#if USE_OVERVIEW_TRACE
	RetAddress = (uint8*)RetAddress + HeaderSize;
	Cookie = (FCookie*)RetAddress - 1;
	Cookie->Size = NewSize;
	GSummaryTrace->Alloc(NewSize, true);
#endif

	void* Owner = GetOwner(bLight);
	Alignment = GetActualAlignment(NewSize, Alignment);
	GAllocationTrace->ReallocAlloc(RetAddress, NewSize, Alignment, Owner);

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
#if USE_OVERVIEW_TRACE
	const FCookie* Cookie = (const FCookie*)Address - 1;
	InnerAddress = (uint8*)Address - (Cookie->Bias << 3);
	GSummaryTrace->Free(Cookie->Tag, Cookie->Size);
#endif

	return InnerMalloc->Free(InnerAddress);
}



////////////////////////////////////////////////////////////////////////////////
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
	if (Ret != nullptr && (Type & MEM_COMMIT))
	{
		void* Owner = GetOwner(bLight);
		GAllocationTrace->CoreAdd(Ret, Size, Owner);
	}

	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
BOOL WINAPI FVirtualWinApiHooks::VmFree(LPVOID Address, SIZE_T Size, DWORD Type)
{
	void* Owner = GetOwner(bLight);
	GAllocationTrace->CoreRemove(Address, Size, Owner);
	return VmFreeOrig(Address, Size, Type);
}

////////////////////////////////////////////////////////////////////////////////
LPVOID WINAPI FVirtualWinApiHooks::VmAllocEx(HANDLE Process, LPVOID Address, SIZE_T Size, DWORD Type, DWORD Protect)
{
	LPVOID Ret = VmAllocExOrig(Process, Address, Size, Type, Protect);
	if (Process == GetCurrentProcess() && Ret != nullptr && (Type & MEM_COMMIT))
	{
		void* Owner = GetOwner(bLight);
		GAllocationTrace->CoreAdd(Ret, Size, Owner);
	}

	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
BOOL WINAPI FVirtualWinApiHooks::VmFreeEx(HANDLE Process, LPVOID Address, SIZE_T Size, DWORD Type)
{
	if (Process == GetCurrentProcess())
	{
		void* Owner = GetOwner(bLight);
		GAllocationTrace->CoreRemove(Address, Size, Owner);
	}

	return VmFreeExOrig(Process, Address, Size, Type);
}



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
				if (View.Equals(TEXT("memalloc")))
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

		GAllocationTrace->Initialize();
#if USE_OVERVIEW_TRACE
		GSummaryTrace->Initialize();
#endif

		bool bLight = (Mode == 1);

		if (!bLight)
		{
			Backtracer_Create(InMalloc);
		}

		FVirtualWinApiHooks::Initialize(bLight);

		static FUndestructed<FMallocWrapper> MemoryTrace;
		MemoryTrace.Construct(InMalloc, bLight);

		GAllocationTrace.Construct();
#if USE_OVERVIEW_TRACE
		GSummaryTrace.Construct();
#endif

		return &MemoryTrace;
	}

	return InMalloc;
}

////////////////////////////////////////////////////////////////////////////////
void MemoryTrace_Initialize()
{
	// Allocators aren't completely ready in _Create() so we have an extra step
	// where any initialisation that may allocate can go.
	Backtracer_Initialize();
}

#if 0
////////////////////////////////////////////////////////////////////////////////
void MemoryTrace_CoreAdd(void* Base, SIZE_T Size)
{
	void* Owner = GetOwner(bLight);
	GAllocationTrace->CoreAdd(Base, Size, Owner);
}

////////////////////////////////////////////////////////////////////////////////
void MemoryTrace_CoreRemove(void* Base, SIZE_T Size)
{
	void* Owner = GetOwner(bLight);
	GAllocationTrace->CoreRemove(Base, Size, Owner);
}
#endif // 0

#undef USE_OVERVIEW_TRACE

#include "Windows/HideWindowsPlatformTypes.h"

#endif // UE_MEMORY_TRACE_ENABLED
