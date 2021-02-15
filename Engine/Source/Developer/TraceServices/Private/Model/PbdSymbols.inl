// Copyright Epic Games, Inc. All Rights Reserved.

#if PLATFORM_WINDOWS

#include "Algo/Sort.h"
#include "Containers/StringView.h"
#include "Containers/Queue.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformProcess.h"
#include "Logging/LogMacros.h"
#include "Misc/PathViews.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "TraceServices/Model/AnalysisSession.h"
#include <atomic>

#include "Windows/AllowWindowsPlatformTypes.h"
#include <dbghelp.h>

/////////////////////////////////////////////////////////////////////
DEFINE_LOG_CATEGORY_STATIC(LogPdbSymbols, Log, All);

/////////////////////////////////////////////////////////////////////
namespace TraceServices {

/////////////////////////////////////////////////////////////////////

static const TCHAR* UNKNOWN_MODULE_TEXT = TEXT("Unknown");

/////////////////////////////////////////////////////////////////////
class FPdbSymbols : public FRunnable
{
public:
	FPdbSymbols(IAnalysisSession& InSession)
		: bInitialized(false)
		, Session(InSession)
	{
		static uint64 BaseHandle = 0x493;
		Handle = (HANDLE)(++BaseHandle);

		// Load DbgHelp interface
		ULONG SymOpts = 0;
		SymOpts |= SYMOPT_LOAD_LINES;
		SymOpts |= SYMOPT_OMAP_FIND_NEAREST;
		SymOpts |= SYMOPT_DEFERRED_LOADS;
		SymOpts |= SYMOPT_EXACT_SYMBOLS;
		SymOpts |= SYMOPT_IGNORE_NT_SYMPATH;
		SymOpts |= SYMOPT_UNDNAME;

		SymSetOptions(SymOpts);
		bInitialized = SymInitialize(Handle, NULL, FALSE); 

		if (bInitialized)
		{
			// Start the worker thread
			bRunWorkerThread = true;
			Thread = FRunnableThread::Create(this, TEXT("PbdSymbolWorker"), 0, TPri_Normal);
		}
	}

	~FPdbSymbols()
	{
		bRunWorkerThread = false;
		Thread->WaitForCompletion();

		SymCleanup(Handle);
	}

	void QueueModuleLoad(const FStringView& ModulePath, uint64 Base, uint32 Size)
	{
		FScopeLock _(&ModulesCs);

		FStringView ModuleName = FPathViews::GetCleanFilename(ModulePath);

		// Add module and sort list according to base address
		int32 Index = LoadedModules.Add(ModuleEntry{ Base, Size, Session.StoreString(ModuleName), Session.StoreString(ModulePath), false });

		// Queue up module to have symbols loaded
		LoadSymbolsQueue.Enqueue(QueuedModule{ Base, Size, LoadedModules[Index].Path });

		// Sort list according to base address
		Algo::Sort(LoadedModules, [](const ModuleEntry& Lhs, const ModuleEntry& Rhs) { return Lhs.Base < Rhs.Base; });

		ModulesDiscovered++;
	}

	void QueueSymbolResolve(uint64 Address, FResolvedSymbol* Symbol)
	{
		SymbolsDiscovered++;
		ResolveQueue.Enqueue(QueuedAddress{Address, Symbol});
	}

	void GetStats(IModuleProvider::FStats* OutStats) const
	{
		OutStats->ModulesDiscovered = ModulesDiscovered.load();
		OutStats->ModulesFailed = ModulesFailed.load();
		OutStats->ModulesLoaded = ModulesLoaded.load();
		OutStats->SymbolsDiscovered = SymbolsDiscovered.load();
		OutStats->SymbolsFailed = SymbolsFailed.load();
		OutStats->SymbolsResolved = SymbolsResolved.load();
	}

private:

	struct ModuleEntry
	{
		uint64 Base;
		uint32 Size;
		const TCHAR* Name;
		const TCHAR* Path;
		bool bSymbolsLoaded;
	};

	struct QueuedAddress
	{
		uint64 Address;
		FResolvedSymbol* Target;
	};

	struct QueuedModule
	{
		uint64 Base;
		uint64 Size;
		const TCHAR* ImagePath;
	};

	enum : uint32 {
		MaxNameLen = 512,
	};


	uint32 Run() override 
	{
		while (bRunWorkerThread)
		{
			// Prioritize queued module loads
			while (!LoadSymbolsQueue.IsEmpty() && bRunWorkerThread)
			{
				QueuedModule Item;
				if (LoadSymbolsQueue.Dequeue(Item))
				{
					LoadModuleSymbols(Item.Base, Item.Size, Item.ImagePath);
				}
			}

			// Resolve one symbol at a time to give way for modules
			while (!ResolveQueue.IsEmpty() && LoadSymbolsQueue.IsEmpty() && bRunWorkerThread)
			{
				QueuedAddress Item;
				if (ResolveQueue.Dequeue(Item))
				{
					ResolveSymbol(Item.Address, Item.Target);
				}
			}
			
			// ...and breathe...
			FPlatformProcess::Sleep(0.2f);
		}
		
		return 0;
	}

	void Stop() override 
	{
		bRunWorkerThread = false; 
	}

	void UpdateResolvedSymbol(FResolvedSymbol* Symbol, QueryResult Result, const TCHAR* Name, const TCHAR* FileAndLine)
	{
		Symbol->Name = Name;
		Symbol->FileAndLine = FileAndLine;
		Symbol->Result.store(Result, std::memory_order_release);
	}

	void ResolveSymbol(uint64 Address, FResolvedSymbol* Target)
	{
		check(Target);

		uint8 InfoBuffer[sizeof(SYMBOL_INFO) + (MaxNameLen * sizeof(char) + 1)];
		SYMBOL_INFO* Info = (SYMBOL_INFO*)InfoBuffer;
		Info->SizeOfStruct = sizeof(SYMBOL_INFO);
		Info->MaxNameLen = MaxNameLen;

		// Find and build the symbol name
		if (!SymFromAddr(Handle, Address, NULL, Info))
		{
			DWORD Err = GetLastError();
			SymbolsFailed++;
			UpdateResolvedSymbol(Target, QueryResult::NotFound, UNKNOWN_MODULE_TEXT, UNKNOWN_MODULE_TEXT);
			return;
		}

		TStringBuilder<256> SymbolName;
		SymbolName << ANSI_TO_TCHAR(Info->Name);
		const TCHAR* SymbolNameStr = Session.StoreString(FStringView(SymbolName));

		// Find the source file and line
		DWORD  dwDisplacement;
		IMAGEHLP_LINE Line;
		Line.SizeOfStruct = sizeof(IMAGEHLP_LINE);

		if (!SymGetLineFromAddr(Handle, Address, &dwDisplacement, &Line))
		{
			SymbolsFailed++;
			UpdateResolvedSymbol(Target, QueryResult::OK, SymbolNameStr, UNKNOWN_MODULE_TEXT);
			return;
		}

		TStringBuilder<256> FileAndLine;
		FileAndLine << ANSI_TO_TCHAR(Line.FileName) << TEXT(" (") << uint32(Line.LineNumber) << TEXT(")");
		const TCHAR* FileAndLineStr = Session.StoreString(FStringView(FileAndLine));
		
		SymbolsResolved++;
		UpdateResolvedSymbol(Target, QueryResult::OK, SymbolNameStr, FileAndLineStr);
	}

	bool LoadModuleSymbols(uint64 Base, uint64 Size, const TCHAR* Path)
	{
		// Attempt to load symbols
		const DWORD64 LoadedBaseAddress = SymLoadModuleEx(Handle, NULL, TCHAR_TO_ANSI(Path), NULL, Base, Size, NULL, 0);
		const bool bSymbolsLoaded = Base == LoadedBaseAddress;

		if (!bSymbolsLoaded)
		{
			UE_LOG(LogPdbSymbols, Warning, TEXT("Unable to load symbols for %s at %p"), Path, Base);
			ModulesFailed++;
		}
		else
		{
			UE_LOG(LogPdbSymbols, Display, TEXT("Loaded symbols for %s at %p."), Path, Base);
			ModulesLoaded++;
		}

		// Update the module entry with the result
		FScopeLock _(&ModulesCs);
		const int32 EntryIdx = Algo::BinarySearchBy(LoadedModules, Base, [](const ModuleEntry& Entry) { return Entry.Base; });
		check(EntryIdx >= 0 && EntryIdx < LoadedModules.Num());
		LoadedModules[EntryIdx].bSymbolsLoaded = bSymbolsLoaded;
		
		return bSymbolsLoaded;
	}


	const TCHAR* GetModuleNameForAddress(uint64 Address) const
	{
		const int32 EntryIdx = Algo::LowerBoundBy(LoadedModules, Address, [](const ModuleEntry& Entry) { return Entry.Base; }) - 1;
		if (EntryIdx < 0 || EntryIdx >= LoadedModules.Num())
		{
			return UNKNOWN_MODULE_TEXT;
		}

		const ModuleEntry& Entry = LoadedModules[EntryIdx];
		if (Address > (Entry.Base + Entry.Size))
		{
			return UNKNOWN_MODULE_TEXT;
		}

		return Entry.Name;
	}

	FCriticalSection ModulesCs;
	TArray<ModuleEntry> LoadedModules;
	TQueue<QueuedModule, EQueueMode::Mpsc> LoadSymbolsQueue;
	TQueue<QueuedAddress, EQueueMode::Mpsc> ResolveQueue;

	std::atomic<uint32> ModulesDiscovered;
	std::atomic<uint32> ModulesFailed;
	std::atomic<uint32> ModulesLoaded;
	std::atomic<uint32> SymbolsDiscovered;
	std::atomic<uint32> SymbolsFailed;
	std::atomic<uint32> SymbolsResolved;

	bool bRunWorkerThread;
	bool bInitialized;
	HANDLE Handle;
	IAnalysisSession& Session;
	FRunnableThread* Thread;
};

#include "Windows/HideWindowsPlatformTypes.h"

/////////////////////////////////////////////////////////////////////

} // namespace TraceServices

#endif // PLATFORM_WINDOWS
