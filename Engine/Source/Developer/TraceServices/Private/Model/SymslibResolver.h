// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if PLATFORM_WINDOWS

#include "Async/MappedFileHandle.h"
#include "Async/TaskGraphInterfaces.h"
#include "Common/PagedArray.h"
#include "HAL/CriticalSection.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Modules.h"
#include <atomic>

#include "symslib.h"

/////////////////////////////////////////////////////////////////////
namespace TraceServices {

/////////////////////////////////////////////////////////////////////
class FSymslibResolver
{
public:
	FSymslibResolver(IAnalysisSession& InSession);
	~FSymslibResolver();
	void QueueModuleLoad(const FStringView& ModulePath, uint64 Base, uint32 Size, const uint8* ImageId, uint32 ImageIdSize);
	void QueueSymbolResolve(uint64 Address, FResolvedSymbol* Symbol);
	void OnAnalysisComplete();
	void GetStats(IModuleProvider::FStats* OutStats) const;

private:
	enum class EModuleStatus : uint8
	{
		Pending,
		Loaded,
		VersionMismatch,
		Failed
	};

	struct FSymsSymbol
	{
		const char* Name;
	};

	struct FSymsUnit
	{
		SYMS_SpatialMap1D ProcMap;
		SYMS_String8Array FileTable;
		SYMS_LineTable    LineTable;
		SYMS_SpatialMap1D LineMap;
	};

	struct FSymsInstance
	{
		TArray<SYMS_Arena*> Arenas;
		TArray<FSymsUnit>   Units;
		SYMS_SpatialMap1D   UnitMap;
		SYMS_SpatialMap1D   StrippedMap;
		FSymsSymbol*        StrippedSymbols;
		uint64              DefaultBase;
	};

	struct FModuleEntry
	{
		uint64 Base;
		uint32 Size;
		const TCHAR* Name;
		const TCHAR* Path;
		FSymsInstance Instance;
		std::atomic<EModuleStatus> Status;
		TArray<uint8> ImageId;
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
		QueuedAddressLength = 2048,
		SymbolTasksInParallel = 8
	};

	/**
	 * Checks if there are no modules in flight and that the queue has reached
	 * the threshold for dispatching. Note that is up to the caller to synchronize.
	 */
	void MaybeDispatchQueuedAddresses();
	/**
	 * Dispatches the currently queued addresses to be resolved. Note that is up to the caller to synchronize.
	 */
	void DispatchQueuedAddresses();
	void ResolveSymbols(TArrayView<FQueuedAddress>& QueuedWork);
	FModuleEntry* GetModuleForAddress(uint64 Address);
	static void UpdateResolvedSymbol(FResolvedSymbol* Symbol, ESymbolQueryResult Result, const TCHAR* Module, const TCHAR* Name, const TCHAR* File, uint16 Line);
	void LoadModuleTracked(FModuleEntry* Module);
	EModuleStatus LoadModule(FModuleEntry* Module);
	void ResolveSymbolTracked(uint64 Address, FResolvedSymbol* Target, class FSymbolStringAllocator& StringAllocator);
	bool ResolveSymbol(uint64 Address, FResolvedSymbol* Target, class FSymbolStringAllocator& StringAllocator);

	FRWLock ModulesLock;
	TPagedArray<FModuleEntry> Modules;
	TArray<FModuleEntry*> SortedModules;
	FCriticalSection SymbolsQueueLock;
	TArray<FQueuedAddress, TInlineAllocator<QueuedAddressLength>> ResolveQueue;
	std::atomic<uint32> TasksInFlight;
	FGraphEventRef CleanupTask;
	std::atomic<bool> CancelTasks;

	std::atomic<uint32> ModulesDiscovered;
	std::atomic<uint32> ModulesFailed;
	std::atomic<uint32> ModulesLoaded;
	std::atomic<uint32> SymbolsDiscovered;
	std::atomic<uint32> SymbolsFailed;
	std::atomic<uint32> SymbolsResolved;
	std::atomic<uint64> SymbolBytesAllocated;
	std::atomic<uint64> SymbolBytesWasted;

	IAnalysisSession& Session;
};

/////////////////////////////////////////////////////////////////////

} // namespace TraceServices

#endif // PLATFORM_WINDOWS
