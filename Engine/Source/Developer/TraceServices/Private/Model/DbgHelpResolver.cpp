// Copyright Epic Games, Inc. All Rights Reserved.

#if PLATFORM_WINDOWS

#include "DbgHelpResolver.h"
#include "Algo/Sort.h"
#include "Containers/Queue.h"
#include "Containers/StringView.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "Logging/LogMacros.h"
#include "Misc/PathViews.h"
#include "Misc/ScopeLock.h"
#include "Misc/StringBuilder.h"
#include "TraceServices/Model/AnalysisSession.h"
#include <atomic>

#include "Windows/AllowWindowsPlatformTypes.h"
#include <dbghelp.h>

/////////////////////////////////////////////////////////////////////
DEFINE_LOG_CATEGORY_STATIC(LogDbgHelp, Log, All);

/////////////////////////////////////////////////////////////////////
namespace TraceServices {

/////////////////////////////////////////////////////////////////////

static const TCHAR* GUnknownModuleTextDbgHelp = TEXT("Unknown");

/////////////////////////////////////////////////////////////////////
FDbgHelpResolver::FDbgHelpResolver(IAnalysisSession& InSession)
	: bRunWorkerThread(false)
	, bDrainThenStop(false)
	, bInitialized(false)
	, Session(InSession)
{
	bInitialized = SetupSyms();

	if (bInitialized)
	{
		// Start the worker thread
		bRunWorkerThread = true;
		Thread = FRunnableThread::Create(this, TEXT("DbgHelpWorker"), 0, TPri_Normal);
	}
}

/////////////////////////////////////////////////////////////////////
FDbgHelpResolver::~FDbgHelpResolver()
{
	bRunWorkerThread = false;
	if (Thread)
	{
		Thread->WaitForCompletion();
	}
}

/////////////////////////////////////////////////////////////////////
void FDbgHelpResolver::QueueModuleLoad(const FStringView& ModulePath, uint64 Base, uint32 Size, const uint8* ImageId, uint32 ImageIdSize)
{
	FScopeLock _(&ModulesCs);

	const FStringView ModuleName = FPathViews::GetCleanFilename(ModulePath);

	// Add module and sort list according to base address
	const int32 Index = LoadedModules.Add(FModuleEntry{ Base, Size, Session.StoreString(ModuleName), Session.StoreString(ModulePath), false });

	// Queue up module to have symbols loaded
	LoadSymbolsQueue.Enqueue(FQueuedModule{ Base, Size, LoadedModules[Index].Path });

	// Sort list according to base address
	Algo::Sort(LoadedModules, [](const FModuleEntry& Lhs, const FModuleEntry& Rhs) { return Lhs.Base < Rhs.Base; });

	++ModulesDiscovered;
}

/////////////////////////////////////////////////////////////////////
void FDbgHelpResolver::QueueSymbolResolve(uint64 Address, FResolvedSymbol* Symbol)
{
	++SymbolsDiscovered;
	ResolveQueue.Enqueue(FQueuedAddress{Address, Symbol});
}

/////////////////////////////////////////////////////////////////////
void FDbgHelpResolver::GetStats(IModuleProvider::FStats* OutStats) const
{
	OutStats->ModulesDiscovered = ModulesDiscovered.load();
	OutStats->ModulesFailed = ModulesFailed.load();
	OutStats->ModulesLoaded = ModulesLoaded.load();
	OutStats->SymbolsDiscovered = SymbolsDiscovered.load();
	OutStats->SymbolsFailed = SymbolsFailed.load();
	OutStats->SymbolsResolved = SymbolsResolved.load();
}

/////////////////////////////////////////////////////////////////////
void FDbgHelpResolver::OnAnalysisComplete()
{
	// At this point no more module loads or symbol requests will be queued,
	// we drain the current queue, then release resources and file locks.
	bDrainThenStop = true;
}


/////////////////////////////////////////////////////////////////////
bool FDbgHelpResolver::SetupSyms() 
{
	// Create a unique handle
	static UPTRINT BaseHandle = 0x493;
	Handle = ++BaseHandle;

	// Load DbgHelp interface
	ULONG SymOpts = 0;
	SymOpts |= SYMOPT_LOAD_LINES;
	SymOpts |= SYMOPT_OMAP_FIND_NEAREST;
	SymOpts |= SYMOPT_DEFERRED_LOADS;
	SymOpts |= SYMOPT_EXACT_SYMBOLS;
	SymOpts |= SYMOPT_IGNORE_NT_SYMPATH;
	SymOpts |= SYMOPT_UNDNAME;

	SymSetOptions(SymOpts);
	return SymInitialize((HANDLE)Handle, NULL, FALSE);
}


/////////////////////////////////////////////////////////////////////
void FDbgHelpResolver::FreeSyms() const
{
	// This release file locks on debug files
	SymCleanup((HANDLE)Handle);
}


/////////////////////////////////////////////////////////////////////
uint32 FDbgHelpResolver::Run()
{
	while (bRunWorkerThread)
	{
		// Prioritize queued module loads
		while (!LoadSymbolsQueue.IsEmpty() && bRunWorkerThread)
		{
			FQueuedModule Item;
			if (LoadSymbolsQueue.Dequeue(Item))
			{
				LoadModuleSymbols(Item.Base, Item.Size, Item.ImagePath);
			}
		}

		// Resolve one symbol at a time to give way for modules
		while (!ResolveQueue.IsEmpty() && LoadSymbolsQueue.IsEmpty() && bRunWorkerThread)
		{
			FQueuedAddress Item;
			if (ResolveQueue.Dequeue(Item))
			{
				ResolveSymbol(Item.Address, Item.Target);
			}
		}

		if (bDrainThenStop && ResolveQueue.IsEmpty() && LoadSymbolsQueue.IsEmpty())
		{
			bRunWorkerThread = false;
		}
		
		// ...and breathe...
		FPlatformProcess::Sleep(0.2f);
	}

	// We don't need the syms library anymore
	FreeSyms();
	
	return 0;
}

/////////////////////////////////////////////////////////////////////
void FDbgHelpResolver::Stop()
{
	bRunWorkerThread = false; 
}

/////////////////////////////////////////////////////////////////////
void FDbgHelpResolver::UpdateResolvedSymbol(FResolvedSymbol* Symbol, ESymbolQueryResult Result, const TCHAR* Module, const TCHAR* Name, const TCHAR* File, uint16 Line)
{
	Symbol->Module = Module;
	Symbol->Name = Name;
	Symbol->File = File;
	Symbol->Line = Line;
	Symbol->Result.store(Result, std::memory_order_release);
}

/////////////////////////////////////////////////////////////////////
void FDbgHelpResolver::ResolveSymbol(uint64 Address, FResolvedSymbol* Target)
{
	check(Target);

	const FModuleEntry* Module = GetModuleForAddress(Address);
	if (!Module)
	{
		++SymbolsFailed;
		UpdateResolvedSymbol(Target, ESymbolQueryResult::NotLoaded, GUnknownModuleTextDbgHelp,
		                     GUnknownModuleTextDbgHelp, GUnknownModuleTextDbgHelp, 0);
		return;
	}
	
	uint8 InfoBuffer[sizeof(SYMBOL_INFO) + (MaxNameLen * sizeof(char) + 1)];
	SYMBOL_INFO* Info = (SYMBOL_INFO*)InfoBuffer;
	Info->SizeOfStruct = sizeof(SYMBOL_INFO);
	Info->MaxNameLen = MaxNameLen;

	// Find and build the symbol name
	if (!SymFromAddr((HANDLE)Handle, Address, NULL, Info))
	{
		++SymbolsFailed;
		UpdateResolvedSymbol(Target, ESymbolQueryResult::NotFound, Module->Name, GUnknownModuleTextDbgHelp,
		                     GUnknownModuleTextDbgHelp, 0);
		return;
	}

	const TCHAR* SymbolNameStr = Session.StoreString(ANSI_TO_TCHAR(Info->Name));
	
	// Find the source file and line
	DWORD  dwDisplacement;
	IMAGEHLP_LINE Line;
	Line.SizeOfStruct = sizeof(IMAGEHLP_LINE);

	if (!SymGetLineFromAddr((HANDLE)Handle, Address, &dwDisplacement, &Line))
	{
		++SymbolsFailed;
		UpdateResolvedSymbol(Target, ESymbolQueryResult::OK, Module->Name, SymbolNameStr, GUnknownModuleTextDbgHelp, 0);
		return;
	}
	
	const TCHAR* SymbolFileStr = Session.StoreString(ANSI_TO_TCHAR(Line.FileName));
	
	++SymbolsResolved;
	UpdateResolvedSymbol(Target, ESymbolQueryResult::OK, Module->Name, SymbolNameStr, SymbolFileStr, Line.LineNumber);
}

/////////////////////////////////////////////////////////////////////
bool FDbgHelpResolver::LoadModuleSymbols(uint64 Base, uint64 Size, const TCHAR* Path)
{
	// Attempt to load symbols
	const DWORD64 LoadedBaseAddress = SymLoadModuleEx((HANDLE)Handle, NULL, TCHAR_TO_ANSI(Path), NULL, Base, Size, NULL, 0);
	const bool bSymbolsLoaded = Base == LoadedBaseAddress;

	if (!bSymbolsLoaded)
	{
		UE_LOG(LogDbgHelp, Warning, TEXT("Unable to load symbols for %s at 0x%016llx"), Path, Base);
		++ModulesFailed;
	}
	else
	{
		UE_LOG(LogDbgHelp, Display, TEXT("Loaded symbols for %s at 0x%016llx."), Path, Base);
		++ModulesLoaded;
	}

	// Update the module entry with the result
	FScopeLock _(&ModulesCs);
	const int32 EntryIdx = Algo::BinarySearchBy(LoadedModules, Base, [](const FModuleEntry& Entry) { return Entry.Base; });
	check(EntryIdx >= 0 && EntryIdx < LoadedModules.Num());
	LoadedModules[EntryIdx].bSymbolsLoaded = bSymbolsLoaded;
	
	return bSymbolsLoaded;
}


/////////////////////////////////////////////////////////////////////
const FDbgHelpResolver::FModuleEntry* FDbgHelpResolver::GetModuleForAddress(uint64 Address) const
{
	const int32 EntryIdx = Algo::LowerBoundBy(LoadedModules, Address, [](const FModuleEntry& Entry) { return Entry.Base; }) - 1;
	if (EntryIdx < 0 || EntryIdx >= LoadedModules.Num())
	{
		return nullptr;
	}

	return &LoadedModules[EntryIdx];
}


/////////////////////////////////////////////////////////////////////
#include "Windows/HideWindowsPlatformTypes.h"

/////////////////////////////////////////////////////////////////////

} // namespace TraceServices

#endif // PLATFORM_WINDOWS
