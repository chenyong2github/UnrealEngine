// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#if PLATFORM_WINDOWS

#include "Containers/Array.h"
#include "Containers/Queue.h"
#include "HAL/CriticalSection.h"
#include "HAL/Runnable.h"
#include "Misc/PathViews.h"
#include "Misc/StringBuilder.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Modules.h"
#include <atomic>

/////////////////////////////////////////////////////////////////////
namespace TraceServices {

/////////////////////////////////////////////////////////////////////
class FDbgHelpResolver : public FRunnable
{
public:
	FDbgHelpResolver(IAnalysisSession& InSession);
	~FDbgHelpResolver();
	void QueueModuleLoad(const FStringView& ModulePath, uint64 Base, uint32 Size, const uint8* ImageId, uint32 ImageIdSize);
	void QueueSymbolResolve(uint64 Address, FResolvedSymbol* Symbol);
	void GetStats(IModuleProvider::FStats* OutStats) const;
	void OnAnalysisComplete();

private:	
	struct FModuleEntry
	{
		uint64 Base;
		uint32 Size;
		const TCHAR* Name;
		const TCHAR* Path;
		bool bSymbolsLoaded;
	};

	struct FQueuedAddress
	{
		uint64 Address;
		FResolvedSymbol* Target;
	};

	struct FQueuedModule
	{
		uint64 Base;
		uint64 Size;
		const TCHAR* ImagePath;
	};

	enum : uint32 {
		MaxNameLen = 512,
	};


	bool SetupSyms();
	void FreeSyms() const;
	virtual uint32 Run() override;
	virtual void Stop() override;
	static void UpdateResolvedSymbol(FResolvedSymbol* Symbol, ESymbolQueryResult Result, const TCHAR* Module, const TCHAR* Name, const TCHAR* File, uint16 Line);

	void ResolveSymbol(uint64 Address, FResolvedSymbol* Target);
	bool LoadModuleSymbols(uint64 Base, uint64 Size, const TCHAR* Path);
	const FModuleEntry* GetModuleForAddress(uint64 Address) const;

	FCriticalSection ModulesCs;
	TArray<FModuleEntry> LoadedModules;
	TQueue<FQueuedModule, EQueueMode::Mpsc> LoadSymbolsQueue;
	TQueue<FQueuedAddress, EQueueMode::Mpsc> ResolveQueue;

	std::atomic<uint32> ModulesDiscovered;
	std::atomic<uint32> ModulesFailed;
	std::atomic<uint32> ModulesLoaded;
	std::atomic<uint32> SymbolsDiscovered;
	std::atomic<uint32> SymbolsFailed;
	std::atomic<uint32> SymbolsResolved;

	bool bRunWorkerThread;
	bool bDrainThenStop;
	bool bInitialized;
	UPTRINT Handle;
	IAnalysisSession& Session;
	FRunnableThread* Thread;
};

/////////////////////////////////////////////////////////////////////

} // namespace TraceServices

#endif // PLATFORM_WINDOWS
