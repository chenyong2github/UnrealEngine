// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "MemoryTrace.inl"
#include "Trace/Trace.inl"

#include "Windows/AllowWindowsPlatformTypes.h"
#	include <winternl.h>
#include "Windows/HideWindowsPlatformTypes.h"

////////////////////////////////////////////////////////////////////////////////
struct FNtDllFunction
{
	FARPROC Addr;

	FNtDllFunction(const char* Name)
	{
		HMODULE NtDll = LoadLibraryW(L"ntdll.dll");
		check(NtDll);
		Addr = GetProcAddress(NtDll, Name);
	}

	template <typename... ArgTypes>
	unsigned int operator () (ArgTypes... Args)
	{
		typedef unsigned int (NTAPI *Prototype)(ArgTypes...);
		return (Prototype((void*)Addr))(Args...);
	}
};



////////////////////////////////////////////////////////////////////////////////
UE_TRACE_CHANNEL(ModuleChannel, "Module information needed for symbols resolution", true)

UE_TRACE_EVENT_BEGIN(Diagnostics, ModuleLoad, NoSync|Important)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Name)
	UE_TRACE_EVENT_FIELD(uint32, Base)
	UE_TRACE_EVENT_FIELD(uint32, Size)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Diagnostics, ModuleUnload, NoSync|Important)
	UE_TRACE_EVENT_FIELD(uint32, Base)
UE_TRACE_EVENT_END()

////////////////////////////////////////////////////////////////////////////////
class FModuleTrace
{
public:
	typedef void				(*SubscribeFunc)(bool, void*, const TCHAR*);

								FModuleTrace(FMalloc* InMalloc);
								~FModuleTrace();
	static FModuleTrace*		Get();
	void						Initialize();
	void						Subscribe(SubscribeFunc Function);

private:
	void						OnDllLoaded(const UNICODE_STRING& Name, UPTRINT Base);
	void						OnDllUnloaded(UPTRINT Base);
	void						OnDllNotification(unsigned int Reason, const void* DataPtr);
	static FModuleTrace*		Instance;
	TMiniArray<SubscribeFunc>	Subscribers;
	void*						CallbackCookie;
};

////////////////////////////////////////////////////////////////////////////////
FModuleTrace* FModuleTrace::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////
FModuleTrace::FModuleTrace(FMalloc* InMalloc)
: Subscribers(InMalloc)
{
	Instance = this;
}

////////////////////////////////////////////////////////////////////////////////
FModuleTrace::~FModuleTrace()
{
	FNtDllFunction UnregisterFunc("LdrUnregisterDllNotification");
	UnregisterFunc(CallbackCookie);
}

////////////////////////////////////////////////////////////////////////////////
FModuleTrace* FModuleTrace::Get()
{
	return Instance;
}

////////////////////////////////////////////////////////////////////////////////
void FModuleTrace::Initialize()
{
	using namespace UE::Trace;

	// Register for DLL load/unload notifications.
	auto Thunk = [] (ULONG Reason, const void* Data, void* Context)
	{
		auto* Self = (FModuleTrace*)Context;
		Self->OnDllNotification(Reason, Data);
	};

	typedef void (CALLBACK *ThunkType)(ULONG, const void*, void*);
	auto ThunkImpl = ThunkType(Thunk);

	FNtDllFunction RegisterFunc("LdrRegisterDllNotification");
	RegisterFunc(0, ThunkImpl, this, &CallbackCookie);

	// Enumerate already loaded modules.
	const TEB* ThreadEnvBlock = NtCurrentTeb();
	const PEB* ProcessEnvBlock = ThreadEnvBlock->ProcessEnvironmentBlock;
	const LIST_ENTRY* ModuleIter = ProcessEnvBlock->Ldr->InMemoryOrderModuleList.Flink;
	const LIST_ENTRY* ModuleIterEnd = ModuleIter->Blink;
	do
	{
		const auto& ModuleData = *(LDR_DATA_TABLE_ENTRY*)(ModuleIter - 1);
		if (ModuleData.DllBase == 0)
		{
			break;
		}

		OnDllLoaded(ModuleData.FullDllName, UPTRINT(ModuleData.DllBase));
		ModuleIter = ModuleIter->Flink;
	}
	while (ModuleIter != ModuleIterEnd);
}

////////////////////////////////////////////////////////////////////////////////
void FModuleTrace::Subscribe(SubscribeFunc Function)
{
	Subscribers.Add(Function);
}

////////////////////////////////////////////////////////////////////////////////
void FModuleTrace::OnDllNotification(unsigned int Reason, const void* DataPtr)
{
	enum
	{
		LDR_DLL_NOTIFICATION_REASON_LOADED = 1,
		LDR_DLL_NOTIFICATION_REASON_UNLOADED = 2,
	};

	struct FNotificationData
	{
		uint32					Flags;
		const UNICODE_STRING&	FullPath;
		const UNICODE_STRING&	BaseName;
		UPTRINT					Base;
	};
	const auto& Data = *(FNotificationData*)DataPtr;

	switch (Reason)
	{
	case LDR_DLL_NOTIFICATION_REASON_LOADED:	OnDllLoaded(Data.FullPath, Data.Base);	break;
	case LDR_DLL_NOTIFICATION_REASON_UNLOADED:	OnDllUnloaded(Data.Base);				break;
	}
}

////////////////////////////////////////////////////////////////////////////////
void FModuleTrace::OnDllLoaded(const UNICODE_STRING& Name, UPTRINT Base)
{
	const auto* DosHeader = (IMAGE_DOS_HEADER*)Base;
	const auto* NtHeaders = (IMAGE_NT_HEADERS*)(Base + DosHeader->e_lfanew);
	const IMAGE_OPTIONAL_HEADER& OptionalHeader = NtHeaders->OptionalHeader;

	UE_TRACE_LOG(Diagnostics, ModuleLoad, ModuleChannel, Name.Length * sizeof(TCHAR))
		<< ModuleLoad.Name(Name.Buffer, Name.Length / 2)
		<< ModuleLoad.Base(uint32(Base >> 16)) // Windows' DLLs are on 64K page boundaries
		<< ModuleLoad.Size(OptionalHeader.SizeOfImage);

	for (SubscribeFunc Subscriber : Subscribers)
	{
		Subscriber(true, (void*)Base, Name.Buffer);
	}
}

////////////////////////////////////////////////////////////////////////////////
void FModuleTrace::OnDllUnloaded(UPTRINT Base)
{
	UE_TRACE_LOG(Diagnostics, ModuleUnload, ModuleChannel)
		<< ModuleUnload.Base(uint32(Base >> 16));

	for (SubscribeFunc Subscriber : Subscribers)
	{
		Subscriber(false, (void*)Base, nullptr);
	}
}



////////////////////////////////////////////////////////////////////////////////
void Modules_Create(FMalloc* Malloc)
{
	if (FModuleTrace::Get() != nullptr)
	{
		return;
	}

	static FModuleTrace Instance(Malloc);
}

////////////////////////////////////////////////////////////////////////////////
void Modules_Initialize()
{
	if (FModuleTrace* Instance = FModuleTrace::Get())
	{
		Instance->Initialize();
	}
}

////////////////////////////////////////////////////////////////////////////////
void Modules_Subscribe(void (*Function)(bool, void*, const TCHAR*))
{
	if (FModuleTrace* Instance = FModuleTrace::Get())
	{
		Instance->Subscribe(Function);
	}
}
