// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModuleProvider.h"
#include "Containers/Map.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Diagnostics.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include <atomic>

// Symbol files implementations
#include "PbdSymbols.inl"

namespace TraceServices {

/////////////////////////////////////////////////////////////////////
struct FModuleEntry
{
	const TCHAR*		Name;
	const TCHAR*		Path;
	uint64				Base;
	uint32				Size;
	bool				bLoaded;
	std::atomic<bool>	bReady;

	FModuleEntry(const TCHAR* InName, const TCHAR* InPath, uint64 InBase, uint32 InSize, bool InLoaded)
		: Name(InName)
		, Path(InPath)
		, Base(InBase)
		, Size(InSize)
		, bLoaded(InLoaded)
		, bReady(false)
	{

	}
};

/////////////////////////////////////////////////////////////////////
template<typename SymbolResolver>
class TModuleProvider : public IModuleAnalysisProvider
{
public:
								TModuleProvider(IAnalysisSession& Session);
	virtual 					~TModuleProvider() {}

	//Query interface
	const FResolvedSymbol*		GetSymbol(uint64 Address) override;
	void						GetStats(FStats* OutStats) const override;
	
	
	//Analysis interface
	void						OnModuleLoad(const FStringView& Module, uint64 Base, uint32 Size) override;
	void 						OnModuleUnload(uint64 Base) override;

private:

	FRWLock						ModulesLock;
	TPagedArray<FModuleEntry>	Modules;

	FRWLock						SymbolsLock;
	TPagedArray<FResolvedSymbol> Symbols;
	TMap<uint64, const FResolvedSymbol*> SymbolCache;

	IAnalysisSession&			Session;
	FString						Platform;
	SymbolResolver 				Resolver;
};

/////////////////////////////////////////////////////////////////////
template<typename SymbolProvider>
TModuleProvider<SymbolProvider>::TModuleProvider(IAnalysisSession& Session)
	: Modules(Session.GetLinearAllocator(), 128)
	, Symbols(Session.GetLinearAllocator(), 1024*1024)
	, Session(Session)
	, Resolver(Session)
{
}

/////////////////////////////////////////////////////////////////////
template<typename SymbolProvider>
const FResolvedSymbol* TModuleProvider<SymbolProvider>::GetSymbol(uint64 Address)
{
	{
		// Attempt to read from the cached symbols.
		FReadScopeLock _(SymbolsLock);
		if (const FResolvedSymbol** Entry = SymbolCache.Find(Address))
		{
			return *Entry;
		}
	}

	FResolvedSymbol* ResolvedSymbol = nullptr;
	{
		// Add a pending entry to our cache
		FWriteScopeLock _(SymbolsLock);
		if (SymbolCache.Contains(Address))
		{
			return SymbolCache[Address];
		}
		ResolvedSymbol = &Symbols.EmplaceBack(ESymbolQueryResult::Pending, nullptr, nullptr);
		SymbolCache.Add(Address, ResolvedSymbol);
	}

	// If not in cache yet, queue it up in the resolver
	check(ResolvedSymbol);
	Resolver.QueueSymbolResolve(Address, ResolvedSymbol);

	return ResolvedSymbol;
}

/////////////////////////////////////////////////////////////////////
template<typename SymbolResolver>
void TModuleProvider<SymbolResolver>::GetStats(FStats* OutStats) const
{
	Resolver.GetStats(OutStats);
}

/////////////////////////////////////////////////////////////////////
template<typename SymbolProvider>
void TModuleProvider<SymbolProvider>::OnModuleLoad(const FStringView& Module, uint64 Base, uint32 Size)
{
	if (Module.Len() == 0)
	{
		return;
	}

	const TCHAR* Path = Session.StoreString(Module);
	const TCHAR* Name = Session.StoreString(Module);

	FModuleEntry* NewEntry = nullptr;
	{
		FWriteScopeLock _(ModulesLock);
		NewEntry = &Modules.EmplaceBack(Name, Path, Base, Size, false);
	}

	Resolver.QueueModuleLoad(Module, Base, Size);
}

/////////////////////////////////////////////////////////////////////
template<typename SymbolProvider>
void TModuleProvider<SymbolProvider>::OnModuleUnload(uint64 Base)
{
	//todo: Find entry, set bLoaded to false
}

/////////////////////////////////////////////////////////////////////
IModuleAnalysisProvider* CreateModuleProvider(IAnalysisSession& InSession, const FAnsiStringView& InSymbolFormat)
{
#if PLATFORM_WINDOWS
	if (InSymbolFormat.Equals("pdb"))
	{
		return new TModuleProvider<FPdbSymbols>(InSession);
	}
#endif
	return nullptr;
}

} // namespace TraceServices
