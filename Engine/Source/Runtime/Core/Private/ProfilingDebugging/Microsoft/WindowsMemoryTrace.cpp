// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProfilingDebugging/MemoryTrace.h"

#include "ProfilingDebugging/CallstackTrace.h"
#include "ProfilingDebugging/TraceMalloc.h"

#if UE_MEMORY_TRACE_ENABLED

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
FMalloc* MemoryTrace_CreateInternal(FMalloc*);

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
	static LPVOID WINAPI	VmAllocEx(HANDLE Process, LPVOID Address, SIZE_T Size, DWORD Type, DWORD Protect);
	static LPVOID WINAPI	VmAlloc2(HANDLE Process, LPVOID BaseAddress, SIZE_T Size, ULONG AllocationType, ULONG PageProtection, /*MEM_EXTENDED_PARAMETER* */ void* ExtendedParameters, ULONG ParameterCount);
	static BOOL WINAPI		VmFree(LPVOID Address, SIZE_T Size, DWORD Type);
	static BOOL WINAPI		VmFreeEx(HANDLE Process, LPVOID Address, SIZE_T Size, DWORD Type);
	static LPVOID			(WINAPI *VmAllocOrig)(LPVOID, SIZE_T, DWORD, DWORD);
	static LPVOID			(WINAPI *VmAllocExOrig)(HANDLE, LPVOID, SIZE_T, DWORD, DWORD);
	static LPVOID			(WINAPI *VmAlloc2Orig)(HANDLE, LPVOID, SIZE_T, ULONG, ULONG, /*MEM_EXTENDED_PARAMETER* */ void*, ULONG);
	static BOOL				(WINAPI *VmFreeOrig)(LPVOID, SIZE_T, DWORD);
	static BOOL				(WINAPI *VmFreeExOrig)(HANDLE, LPVOID, SIZE_T, DWORD);

	typedef LPVOID(__stdcall* FnVirtualAlloc2)(HANDLE, LPVOID, SIZE_T, ULONG, ULONG, /* MEM_EXTENDED_PARAMETER* */ void*, ULONG);
};

////////////////////////////////////////////////////////////////////////////////
bool	FVirtualWinApiHooks::bLight;
LPVOID	(WINAPI *FVirtualWinApiHooks::VmAllocOrig)(LPVOID, SIZE_T, DWORD, DWORD);
LPVOID	(WINAPI *FVirtualWinApiHooks::VmAllocExOrig)(HANDLE, LPVOID, SIZE_T, DWORD, DWORD);
LPVOID	(WINAPI *FVirtualWinApiHooks::VmAlloc2Orig)(HANDLE, LPVOID, SIZE_T, ULONG, ULONG, /*MEM_EXTENDED_PARAMETER* */ void*, ULONG);
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

#if PLATFORM_WINDOWS
#if (NTDDI_VERSION >= NTDDI_WIN10_RS4)
		{
			FTextSectionEditor EditorAlloc2((void*)VirtualAlloc2);
			VmAlloc2Orig = Editor.Hook(VirtualAlloc2, &FVirtualWinApiHooks::VmAlloc2);
		}
#else // NTDDI_VERSION
		{
			VmAlloc2Orig = nullptr;
			HINSTANCE DllInstance;
			DllInstance = LoadLibrary(TEXT("kernelbase.dll"));
			if (DllInstance != NULL)
			{
#pragma warning(push)
#pragma warning(disable: 4191) // 'type cast': unsafe conversion from 'FARPROC' to 'FVirtualWinApiHooks::FnVirtualAlloc2'
				VmAlloc2Orig = (FnVirtualAlloc2)GetProcAddress(DllInstance, "VirtualAlloc2");
#pragma warning(pop)
				FreeLibrary(DllInstance);
			}
			if (VmAlloc2Orig)
			{
				FTextSectionEditor EditorAlloc2((void*)VmAlloc2Orig);
				VmAlloc2Orig = Editor.Hook(VmAlloc2Orig, &FVirtualWinApiHooks::VmAlloc2);
			}
		}
#endif // NTDDI_VERSION
#endif // PLATFORM_WINDOWS
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
		MemoryTrace_Alloc((uint64)Ret, Size, 0, EMemoryTraceRootHeap::SystemMemory);
		MemoryTrace_MarkAllocAsHeap((uint64)Ret, EMemoryTraceRootHeap::SystemMemory);
	}

	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
BOOL WINAPI FVirtualWinApiHooks::VmFree(LPVOID Address, SIZE_T Size, DWORD Type)
{
	if (Type & MEM_RELEASE)
	{
		MemoryTrace_Free((uint64)Address, EMemoryTraceRootHeap::SystemMemory);
	}
	return VmFreeOrig(Address, Size, Type);
}

////////////////////////////////////////////////////////////////////////////////
LPVOID WINAPI FVirtualWinApiHooks::VmAllocEx(HANDLE Process, LPVOID Address, SIZE_T Size, DWORD Type, DWORD Protect)
{
	LPVOID Ret = VmAllocExOrig(Process, Address, Size, Type, Protect);
	if (Process == GetCurrentProcess() && Ret != nullptr && (Type & MEM_RESERVE))
	{
		MemoryTrace_Alloc((uint64)Ret, Size, 0);
		MemoryTrace_MarkAllocAsHeap((uint64)Ret, EMemoryTraceRootHeap::SystemMemory);
	}

	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
BOOL WINAPI FVirtualWinApiHooks::VmFreeEx(HANDLE Process, LPVOID Address, SIZE_T Size, DWORD Type)
{
	if (Process == GetCurrentProcess() && (Type & MEM_RELEASE))
	{
		MemoryTrace_Free((uint64)Address, EMemoryTraceRootHeap::SystemMemory);
	}

	return VmFreeExOrig(Process, Address, Size, Type);
}

////////////////////////////////////////////////////////////////////////////////
LPVOID WINAPI FVirtualWinApiHooks::VmAlloc2(HANDLE Process, LPVOID BaseAddress, SIZE_T Size, ULONG Type, ULONG PageProtection, /*MEM_EXTENDED_PARAMETER* */ void* ExtendedParameters, ULONG ParameterCount)
{
	LPVOID Ret = VmAlloc2Orig(Process, BaseAddress, Size, Type, PageProtection, ExtendedParameters, ParameterCount);
	if (Process == GetCurrentProcess() && Ret != nullptr && (Type & MEM_RESERVE))
	{
		MemoryTrace_Alloc((uint64)Ret, Size, 0);
		MemoryTrace_MarkAllocAsHeap((uint64)Ret, EMemoryTraceRootHeap::SystemMemory);
	}

	return Ret;
}
#endif // defined(PLATFORM_SUPPORTS_TRACE_WIN32_VIRTUAL_MEMORY_HOOKS)


////////////////////////////////////////////////////////////////////////////////
FMalloc* MemoryTrace_Create(FMalloc* InMalloc)
{
	bool bEnabled = false;
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
					bEnabled = true;
					break;
				}

				Start = c + 1;
			}
		}
	}

	if (bEnabled)
	{
		FMalloc* OutMalloc = MemoryTrace_CreateInternal(InMalloc);
		
	#if defined(PLATFORM_SUPPORTS_TRACE_WIN32_VIRTUAL_MEMORY_HOOKS)
		FVirtualWinApiHooks::Initialize(false);
	#endif // defined(PLATFORM_SUPPORTS_TRACE_WIN32_VIRTUAL_MEMORY_HOOKS)
		
		return OutMalloc;
	}

	return InMalloc;
}

#include "Windows/HideWindowsPlatformTypes.h"

#endif // UE_MEMORY_TRACE_ENABLED
