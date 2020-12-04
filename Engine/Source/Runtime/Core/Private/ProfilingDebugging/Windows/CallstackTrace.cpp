// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Experimental/Containers/SherwoodHashTable.h"
#include "CoreTypes.h"
#include "HAL/CriticalSection.h"
#include "HAL/MemoryBase.h"
#include "MemoryTrace.inl"
#include "Misc/ScopeRWLock.h"
#include "Containers/CircularQueue.h"
#include "HAL/RunnableThread.h"
#include "HAL/LowLevelMemTracker.h"
#include "ProfilingDebugging/MemoryTrace.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#	include <winnt.h>
#	include <winternl.h>
#include "Windows/HideWindowsPlatformTypes.h"
#include "Misc\CoreDelegates.h"
#include "Trace/Trace.inl"
#include "AtomicQueue/AtomicQueue.h"
#include "HAL/Runnable.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"

// 0=off, 1=stats, 2=validation, 3=truth_compare
#define BACKTRACE_DBGLVL 0

/*
 * Windows' x64 binaries contain a ".pdata" section that describes the location
 * and size of its functions and details on how to unwind them. The unwind
 * information includes descriptions about a function's stack frame size and
 * the non-volatile registers it pushes onto the stack. From this we can
 * calculate where a call instruction wrote its return address. This is enough
 * to walk the callstack and by caching this information it can be done
 * efficiently.
 *
 * Some functions need a variable amount of stack (such as those that use
 * alloc() for example) will use a frame pointer. Frame pointers involve saving
 * and restoring the stack pointer in the function's prologue/epilogue. This
 * frees the function up to modify the stack pointer arbitrarily. This
 * significantly complicates establishing where a return address is, so this
 * pdata scheme of walking the stack just doesn't support functions like this.
 * Walking stops if it encounters such a function. Fortunately there are
 * usually very few such functions, saving us from having to read and track
 * non-volatile registers which adds a significant amount of work.
 *
 * A further optimisation is to to assume we are only interested methods that
 * are part of engine or game code. As such we only build lookup tables for
 * such modules and never accept OS or third party modules. Backtracing stops
 * if an address is encountered which doesn't map to a known module.
 */

////////////////////////////////////////////////////////////////////////////////
static uint32 AddressToId(UPTRINT Address)
{
	return uint32(Address >> 16);
}

static UPTRINT IdToAddress(uint32 Id)
{
	return static_cast<uint32>(UPTRINT(Id) << 16);
}

struct FIdPredicate
{
	template <class T> bool operator () (uint32 Id, const T& Item) const { return Id < Item.Id; }
	template <class T> bool operator () (const T& Item, uint32 Id) const { return Item.Id < Id; }
};

////////////////////////////////////////////////////////////////////////////////
struct FUnwindInfo
{
	uint8	Version : 3;
	uint8	Flags : 5;
	uint8	PrologBytes;
	uint8	NumUnwindCodes;
	uint8	FrameReg	: 4;
	uint8	FrameRspBias : 4;
};

struct FUnwindCode
{
	uint8	PrologOffset;
	uint8	OpCode : 4;
	uint8	OpInfo : 4;
	uint16	Params[];
};

enum
{
	UWOP_PUSH_NONVOL		= 0,	// 1 node
	UWOP_ALLOC_LARGE		= 1,	// 2 or 3 nodes
	UWOP_ALLOC_SMALL		= 2,	// 1 node
	UWOP_SET_FPREG			= 3,	// 1 node
	UWOP_SAVE_NONVOL		= 4,	// 2 nodes
	UWOP_SAVE_NONVOL_FAR	= 5,	// 3 nodes
	UWOP_SAVE_XMM128		= 8,	// 2 nodes
	UWOP_SAVE_XMM128_FAR	= 9,	// 3 nodes
	UWOP_PUSH_MACHFRAME		= 10,	// 1 node
};

////////////////////////////////////////////////////////////////////////////////
namespace {

	using namespace Experimental;
	using namespace atomic_queue;

	class FCallstackProcWorker : public FRunnable 
	{
		public:
		struct FBacktraceEntry
		{
			enum {MaxStackDepth = 256};
			uint64	Id = 0;
			uint32	FrameCount = 0;
			uint64	Frames[MaxStackDepth] = { 0 };
		};

		FCallstackProcWorker()
			: bRun(true)
		{
			KnownSet.Reserve(1024 * 1024 * 2);
		}
		
		uint32 Run() override;
		void Stop() override { bRun = false; }
		void AddCallstack(const FBacktraceEntry& Entry);
		void AddWork(const FBacktraceEntry& Entry);

		TSherwoodSet<uint64> 				KnownSet;
		FCriticalSection					ProducerCs;
		AtomicQueue2<FBacktraceEntry, 256>	Queue;
		TAtomic<bool>						bRun;
		TAtomic<bool>						bStarted;
		static FRunnableThread*				Thread;
	};
}

FRunnableThread* FCallstackProcWorker::Thread = nullptr;

////////////////////////////////////////////////////////////////////////////////
class FBacktracer
{
public:
							FBacktracer(FMalloc* InMalloc);
							~FBacktracer();
	static FBacktracer*		Get();
	static void				StartWorker();
	void					AddModule(UPTRINT Base, const TCHAR* Name);
	void					RemoveModule(UPTRINT Base);
	void*					GetBacktraceId(void* AddressOfReturnAddress) const;
	int32					GetFrameSize(void* FunctionAddress) const;

private:
	struct FFunction
	{
		uint32				Id;
		int32				RspBias;
#if BACKTRACE_DBGLVL >= 2
		uint32				Size;
		const FUnwindInfo*	UnwindInfo;
#endif
	};

	struct FModule
	{
		uint32				Id;
		uint32				IdSize = 0;
		uint32				NumFunctions;
#if BACKTRACE_DBGLVL >= 1
		uint16				NumFpTypes;
		//uint16			*padding*
#else
		//uint32			*padding*
#endif
		FFunction*			Functions;
	};

	struct FLookupState
	{
		FModule				Module;
	};

	void					StartWorkerThread();
	const FFunction*		LookupFunction(UPTRINT Address, FLookupState& State) const;
	static FBacktracer*		Instance;
	mutable FRWLock			Lock;
	TMiniArray<FModule>		Modules;
	FMalloc*				Malloc;
	FCallstackProcWorker*	ProcessingThreadRunnable;
#if BACKTRACE_DBGLVL >= 1
	mutable uint32			NumFpTruncations = 0;
	mutable uint32			TotalFunctions = 0;
#endif
};

////////////////////////////////////////////////////////////////////////////////
FBacktracer* FBacktracer::Instance = nullptr;

////////////////////////////////////////////////////////////////////////////////
FBacktracer::FBacktracer(FMalloc* InMalloc)
: Modules(InMalloc)
, Malloc(InMalloc)
{
	Modules.MakeRoom();
	Instance = this;
	ProcessingThreadRunnable = (FCallstackProcWorker*) Malloc->Malloc(sizeof(FCallstackProcWorker));
	ProcessingThreadRunnable = new(ProcessingThreadRunnable) FCallstackProcWorker();

	// We cannot start worker threads directly on creation. Delay them until the
	// engine has gotten a little bit further.
	FCoreDelegates::GetPreMainInitDelegate().AddStatic(FBacktracer::StartWorker);

}

////////////////////////////////////////////////////////////////////////////////
FBacktracer::~FBacktracer()
{
	for (FModule& Module : Modules)
	{
		Malloc->Free(Module.Functions);
	}

	ProcessingThreadRunnable->Stop();
	if (FCallstackProcWorker::Thread)
	{
		FCallstackProcWorker::Thread->WaitForCompletion();
	} 
}

////////////////////////////////////////////////////////////////////////////////
FBacktracer* FBacktracer::Get()
{
	return Instance;
}
////////////////////////////////////////////////////////////////////////////////
void FBacktracer::StartWorker()
{
	if (Instance)
	{
		Instance->StartWorkerThread();
	}
}

////////////////////////////////////////////////////////////////////////////////
void FBacktracer::StartWorkerThread()
{
	if (ProcessingThreadRunnable)
	{
		FCallstackProcWorker::Thread = FRunnableThread::Create(ProcessingThreadRunnable, TEXT("TraceMemCallstacks"), 0, TPri_BelowNormal);
	}
}

////////////////////////////////////////////////////////////////////////////////
void FBacktracer::AddModule(UPTRINT ModuleBase, const TCHAR* Name)
{
	const auto* DosHeader = (IMAGE_DOS_HEADER*)ModuleBase;
	const auto* NtHeader = (IMAGE_NT_HEADERS*)(ModuleBase + DosHeader->e_lfanew);
	const IMAGE_FILE_HEADER* FileHeader = &(NtHeader->FileHeader);

	if (FCString::Strfind(Name, TEXT("Binaries")) == nullptr || FCString::Strfind(Name, TEXT("ThirdParty")) != nullptr)
	{
		return;
	}

	uint32 NumSections = FileHeader->NumberOfSections;
	const auto* Sections = (IMAGE_SECTION_HEADER*)(UPTRINT(&(NtHeader->OptionalHeader)) + FileHeader->SizeOfOptionalHeader);

	// Find ".pdata" section
	UPTRINT PdataBase = 0;
	for (uint32 i = 0; i < NumSections; ++i)
	{
		const IMAGE_SECTION_HEADER* Section = Sections + i;
		if (*(uint64*)(Section->Name) == 0x61'74'61'64'70'2eull) // Sections names are eight bytes and zero padded. This constant is '.pdata'
		{
			PdataBase = ModuleBase + Section->VirtualAddress;
			break;
		}
	}

	if (PdataBase == 0)
	{
		return;
	}

	// Count the number of functions.
	const auto* FunctionTables = (RUNTIME_FUNCTION*)PdataBase;
	uint32 NumFunctions = 0;
	do
	{
		++NumFunctions;
	}
	while (FunctionTables[NumFunctions].BeginAddress);

	// Allocate some space for the module's function-to-frame-size table
	auto* OutTable = (FFunction*)Malloc->Malloc(sizeof(FFunction) * NumFunctions);
	FFunction* OutTableCursor = OutTable;

	// Extract frame size for each function from pdata's unwind codes.
	uint32 NumFpFuncs = 0;
	for (uint32 i = 0; i < NumFunctions; ++i)
	{
		const RUNTIME_FUNCTION* FunctionTable = FunctionTables + i;

		UPTRINT UnwindInfoAddr = ModuleBase + FunctionTable->UnwindInfoAddress;
		const auto* UnwindInfo = (FUnwindInfo*)UnwindInfoAddr;

		if (UnwindInfo->Version != 1)
		{
			/* some v2s have been seen in msvc. Always seem to be assembly
			 * routines (memset, memcpy, etc) */
			continue;
		}

		int32 FpInfo = 0;
		int32 RspBias = 0;

#if BACKTRACE_DBGLVL >= 2
		uint32 PrologVerify = UnwindInfo->PrologBytes;
#endif

		const auto* Code = (FUnwindCode*)(UnwindInfo + 1);
		const auto* EndCode = Code + UnwindInfo->NumUnwindCodes;
		while (Code < EndCode)
		{
#if BACKTRACE_DBGLVL >= 2
			if (Code->PrologOffset > PrologVerify)
			{
				PLATFORM_BREAK();
			}
			PrologVerify = Code->PrologOffset;
#endif

			switch (Code->OpCode)
			{
			case UWOP_PUSH_NONVOL:
				RspBias += 8;
				Code += 1;
				break;

			case UWOP_ALLOC_LARGE:
				if (Code->OpInfo)
				{
					RspBias += *(uint32*)(Code->Params);
					Code += 3;
				}
				else
				{
					RspBias += Code->Params[0] * 8;
					Code += 2;
				}
				break;

			case UWOP_ALLOC_SMALL:
				RspBias += (Code->OpInfo * 8) + 8;
				Code += 1;
				break;

			case UWOP_SET_FPREG:
				// Function will adjust RSP (e.g. through use of alloca()) so it
				// uses a frame pointer register. There's instructions like;
				//
				//   push FRAME_REG
				//   lea FRAME_REG, [rsp + (FRAME_RSP_BIAS * 16)]
				//   ...
				//   add rsp, rax
				//   ...
				//   sub rsp, FRAME_RSP_BIAS * 16
				//   pop FRAME_REG
				//   ret
				//
				// To recover the stack frame we would need to track non-volatile
				// registers which adds a lot of overhead for a small subset of
				// functions. Instead we'll end backtraces at these functions.


				// MSB is set to detect variable sized frames that we can't proceed
				// past when back-tracing.
				NumFpFuncs++;
				FpInfo |= 0x80000000 | (uint32(UnwindInfo->FrameReg) << 27) | (uint32(UnwindInfo->FrameRspBias) << 23);
				Code += 1;
				break;

			case UWOP_PUSH_MACHFRAME:
				RspBias = Code->OpInfo ? 48 : 40;
				Code += 1;
				break;

			case UWOP_SAVE_NONVOL:		Code += 2; break; /* saves are movs instead of pushes */
			case UWOP_SAVE_NONVOL_FAR:	Code += 3; break;
			case UWOP_SAVE_XMM128:		Code += 2; break;
			case UWOP_SAVE_XMM128_FAR:	Code += 3; break;

			default:
#if BACKTRACE_DBGLVL >= 2
				PLATFORM_BREAK();
#endif
				break;
			}
		}

		// "Chained" simply means that multiple RUNTIME_FUNCTIONs pertains to a
		// single actual function in the .text segment.
		bool bIsChained = (UnwindInfo->Flags & UNW_FLAG_CHAININFO);

		RspBias /= sizeof(void*);	// stack push/popds in units of one machine word
		RspBias += !bIsChained;		// and one extra push for the ret address
		RspBias |= FpInfo;			// pack in details about possible frame pointer

		if (bIsChained)
		{
			OutTableCursor[-1].RspBias += RspBias;
#if BACKTRACE_DBGLVL >= 2
			OutTableCursor[-1].Size += (FunctionTable->EndAddress - FunctionTable->BeginAddress);
#endif
		}
		else
		{
			*OutTableCursor = {
				FunctionTable->BeginAddress,
				RspBias,
#if BACKTRACE_DBGLVL >= 2
				FunctionTable->EndAddress - FunctionTable->BeginAddress,
				UnwindInfo,
#endif
			};

			++OutTableCursor;
		}
	}

	UPTRINT ModuleSize = NtHeader->OptionalHeader.SizeOfImage;
	ModuleSize += 0xffff; // to align up to next 64K page. it'll get shifted by AddressToId()

	FModule Module = {
		AddressToId(ModuleBase),
		AddressToId(ModuleSize),
		uint32(UPTRINT(OutTableCursor - OutTable)),
#if BACKTRACE_DBGLVL >= 1
		uint16(NumFpFuncs),
#endif
		OutTable,
	};

	TArrayView<FModule> ModulesView(Modules.Data, Modules.Num);
	int32 Index = Algo::UpperBound(ModulesView, Module.Id, FIdPredicate());
	{
		FWriteScopeLock _(Lock);
		Modules.Insert(Module, Index);
	}

#if BACKTRACE_DBGLVL >= 1
	NumFpTruncations += NumFpFuncs;
	TotalFunctions += NumFunctions;
#endif
}

////////////////////////////////////////////////////////////////////////////////
void FBacktracer::RemoveModule(UPTRINT ModuleBase)
{
	uint32 ModuleId = AddressToId(ModuleBase);
	TArrayView<FModule> ModulesView(Modules.Data, Modules.Num);
	int32 Index = Algo::LowerBound(ModulesView, ModuleId, FIdPredicate());
	if (Index >= Modules.Num)
	{
		return;
	}

	const FModule& Module = Modules.Data[Index];
	if (Module.Id != ModuleId)
	{
		return;
	}

#if BACKTRACE_DBGLVL >= 1
	NumFpTruncations -= Module.NumFpTypes;
	TotalFunctions -= Module.NumFunctions;
#endif

	// no code should be executing at this point so we can safely free the
	// table knowing know one is looking at it.
	Malloc->Free(Module.Functions);

	{
		FWriteScopeLock _(Lock);
		Modules.RemoveAt(Index);
	}
}

////////////////////////////////////////////////////////////////////////////////
const FBacktracer::FFunction* FBacktracer::LookupFunction(UPTRINT Address, FLookupState& State) const
{
	// This function caches the previous module look up. The theory here is that
	// a series of return address in a backtrace often cluster around one module

	FIdPredicate IdPredicate;

	// Look up the module that Address belongs to.
	uint32 AddressId = AddressToId(Address);
	if ((AddressId - State.Module.Id) >= State.Module.IdSize)
	{
		TArrayView<FModule> ModulesView(Modules.Data, Modules.Num);
		uint32 Index = Algo::UpperBound(ModulesView, AddressId, IdPredicate);
		if (Index == 0)
		{
			return nullptr;
		}

		State.Module = Modules.Data[Index - 1];
	}

	// Check that the address is within the address space of the best-found module
	const FModule* Module = &(State.Module);
	if ((AddressId - Module->Id) >= Module->IdSize)
	{
		return nullptr;
	}

	// Now we've a module we have a table of functions and their stack sizes so
	// we can get the frame size for Address
	uint32 FuncId = uint32(Address - IdToAddress(Module->Id)); 
	TArrayView<FFunction> FuncsView(Module->Functions, Module->NumFunctions);
	uint32 Index = Algo::UpperBound(FuncsView, FuncId, IdPredicate);

	const FFunction* Function = Module->Functions + (Index - 1);
#if BACKTRACE_DBGLVL >= 2
	if ((FuncId - Function->Id) >= Function->Size)
	{
		PLATFORM_BREAK();
		return nullptr;
	}
#endif
	return Function;
}

////////////////////////////////////////////////////////////////////////////////
void* FBacktracer::GetBacktraceId(void* AddressOfReturnAddress) const
{
	FLookupState LookupState;
	FCallstackProcWorker::FBacktraceEntry BacktraceEntry;

	UPTRINT* StackPointer = (UPTRINT*)AddressOfReturnAddress;

#if BACKTRACE_DBGLVL >= 3
	UPTRINT TruthBacktrace[1024];
	uint32 NumTruth = RtlCaptureStackBackTrace(0, 1024, (void**)TruthBacktrace, nullptr);
	UPTRINT* TruthCursor = TruthBacktrace;
	for (; *TruthCursor != *StackPointer; ++TruthCursor);
#endif

#if BACKTRACE_DBGLVL >= 2
	struct { void* Sp; void* Ip; const FFunction* Function; } Backtrace[1024] = {};
	uint32 NumBacktrace = 0;
#endif

	uint64 BacktraceId = 0;
	uint32 FrameIdx = 0;

	FReadScopeLock _(Lock);
	do
	{
		UPTRINT RetAddr = *StackPointer;
		
		BacktraceEntry.Frames[FrameIdx++] = RetAddr;
		BacktraceEntry.FrameCount = FrameIdx;

		// This is a simple order-dependent LCG. Should be sufficient enough
		BacktraceId += RetAddr;
		BacktraceId *= 0x30be8efa499c249dull;

		const FFunction* Function = LookupFunction(RetAddr, LookupState);
		if (Function == nullptr)
		{
			break;
		}

#if BACKTRACE_DBGLVL >= 2
		if (NumBacktrace < 1024)
		{
			Backtrace[NumBacktrace++] = {
				StackPointer,
				(void*)RetAddr,
				Function,
			};
		}
#endif

		if (Function->RspBias < 0)
		{
			// This is a frame with a variable-sized stack pointer. We don't
			// track enough information to proceed.
#if BACKTRACE_DBGLVL >= 1
			NumFpTruncations++;
#endif
			break;
		}

		StackPointer += Function->RspBias;
	}
	// Trunkate callstacks longer than MaxStackDepth
	while (*StackPointer && FrameIdx < FCallstackProcWorker::FBacktraceEntry::MaxStackDepth);

	// Save the collected id
	BacktraceEntry.Id = BacktraceId;

#if BACKTRACE_DBGLVL >= 3
	for (uint32 i = 0; i < NumBacktrace; ++i)
	{
		if ((void*)TruthCursor[i] != Backtrace[i].Ip)
		{
			PLATFORM_BREAK();
			break;
		}
	}
#endif

	// Add to queue to be processed. This might block until there is room in the
	// queue (i.e. the processing thread has caught up processing).
	ProcessingThreadRunnable->AddWork(BacktraceEntry);
	return (void*)(BacktraceId & ((1ull << 47) - 1));
}

////////////////////////////////////////////////////////////////////////////////
int32 FBacktracer::GetFrameSize(void* FunctionAddress) const
{
	FReadScopeLock _(Lock);

	FLookupState LookupState;
	const FFunction* Function = LookupFunction(UPTRINT(FunctionAddress), LookupState);
	if (FunctionAddress == nullptr)
	{
		return -1;
	}

	if (Function->RspBias < 0)
	{
		return -1;
	}

	return Function->RspBias;
}

////////////////////////////////////////////////////////////////////////////////
UE_TRACE_CHANNEL(CallstackChannel);

UE_TRACE_EVENT_BEGIN(Memory, CallstackSpec, NoSync)
	UE_TRACE_EVENT_FIELD(uint64, Id)
	UE_TRACE_EVENT_FIELD(uint64[], Frames)
UE_TRACE_EVENT_END()

////////////////////////////////////////////////////////////////////////////////
uint32 FCallstackProcWorker::Run()
{
	while (bRun)
	{
		while (!Queue.was_empty()) 
		{
			FBacktraceEntry Entry = Queue.pop();
			AddCallstack(Entry);
		}
 		FPlatformProcess::Yield();
	}
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
void FCallstackProcWorker::AddCallstack(const FBacktraceEntry& Entry)
{
	bool bAlreadyAdded = false;
	KnownSet.Add(Entry.Id, &bAlreadyAdded);
	if (!bAlreadyAdded)
	{
		UE_TRACE_LOG(Memory, CallstackSpec, CallstackChannel)
			<< CallstackSpec.Id(Entry.Id)
			<< CallstackSpec.Frames(Entry.Frames, Entry.FrameCount);
	}
}

////////////////////////////////////////////////////////////////////////////////
void FCallstackProcWorker::AddWork(const FBacktraceEntry& Entry)
{
	// The queue only supports single producer, single consumer. So we use a lock
	// on the producer side.
	FScopeLock _(&ProducerCs);

	if (Thread) 
	{
		while(!Queue.try_push(Entry))
		{
			FPlatformProcess::Yield();
		}
	}
	else
	{
		// Worker thread has not yet been started, manually process callstack for now.
		AddCallstack(Entry);
	}
}

////////////////////////////////////////////////////////////////////////////////
void Modules_Create(FMalloc*);
void Modules_Subscribe(void (*)(bool, void*, const TCHAR*));
void Modules_Initialize();

////////////////////////////////////////////////////////////////////////////////
void Backtracer_Create(FMalloc* Malloc)
{
	if (FBacktracer::Get() != nullptr)
	{
		return;
	}

	static FBacktracer Instance(Malloc);

	Modules_Create(Malloc);
	Modules_Subscribe(
		[] (bool bLoad, void* Module, const TCHAR* Name)
		{
			bLoad
				? Instance.AddModule(UPTRINT(Module), Name)
				: Instance.RemoveModule(UPTRINT(Module));
		}
	);
}

////////////////////////////////////////////////////////////////////////////////
void Backtracer_Initialize()
{
	Modules_Initialize();
}

////////////////////////////////////////////////////////////////////////////////
int32 Backtracer_GetFrameSize(void* FunctionAddress)
{
	if (FBacktracer* Instance = FBacktracer::Get())
	{
		return Instance->GetFrameSize(FunctionAddress);
	}

	return -1;
}

////////////////////////////////////////////////////////////////////////////////
void* Backtracer_GetBacktraceId(void* AddressOfReturnAddress)
{
	if (FBacktracer* Instance = FBacktracer::Get())
	{
		return Instance->GetBacktraceId(AddressOfReturnAddress);
	}

	return nullptr;
}

