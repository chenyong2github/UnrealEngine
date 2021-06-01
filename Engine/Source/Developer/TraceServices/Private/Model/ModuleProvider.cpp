// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModuleProvider.h"
#include <atomic>
#include "Algo/Transform.h"
#include "Common/CachedPagedArray.h"
#include "Common/CachedStringStore.h"
#include "Common/Utils.h"
#include "Containers/Map.h"
#include "Misc/PathViews.h"
#include "TraceServices/Model/AnalysisCache.h"
#include "TraceServices/Model/AnalysisSession.h"

// Choose which symbol resolver to use.
// If both are enabled and the RAD Syms library fails to initialize, it will fall back to DbgHelp (on Windows).
#define USE_SYMSLIB 1
#define USE_DBGHELP 1

// Symbol files implementations
#if USE_SYMSLIB
#include "SymslibResolver.h"
#endif
#if USE_DBGHELP
#include "DbgHelpResolver.h"
#endif

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
template<typename SymbolResolverType>
class TModuleProvider : public IModuleAnalysisProvider
{
public:
								TModuleProvider(IAnalysisSession& Session);
	virtual 					~TModuleProvider() override;

	//Query interface
	const FResolvedSymbol*		GetSymbol(uint64 Address) override;
	void						GetStats(FStats* OutStats) const override;

	//Analysis interface
	void						OnModuleLoad(const FStringView& Module, uint64 Base, uint32 Size, const uint8* Checksum, uint32 ChecksumSize) override;
	void 						OnModuleUnload(uint64 Base) override;
	void						OnAnalysisComplete() override;
	void						SaveSymbolsToCache(IAnalysisCache& Cache);
	void						LoadSymbolsFromCache(IAnalysisCache& Cache);

private:

	struct FSavedSymbol
	{
		uint64 Address;
		uint32 ModuleOffset;
		uint32 NameOffset;
		uint32 FileOffset;
		uint32 Line;
	};
	
	FRWLock						ModulesLock;
	TPagedArray<FModuleEntry>	Modules;

	FRWLock						SymbolsLock;
	// Persistently stored symbol strings
	FCachedStringStore Strings;
	// Efficient representation of symbols
	TPagedArray<FResolvedSymbol> SymbolCache;
	// Lookup table to for symbols
	TMap<uint64, const FResolvedSymbol*> SymbolCacheLookup;
	
	IAnalysisSession&			Session;
	FString						Platform;
	TUniquePtr<SymbolResolverType>	Resolver;
};

/////////////////////////////////////////////////////////////////////
template<typename SymbolResolverType>
TModuleProvider<SymbolResolverType>::TModuleProvider(IAnalysisSession& Session)
	: Modules(Session.GetLinearAllocator(), 128)
	, Strings(TEXT("ModuleProvider.Strings"), Session.GetCache())
	, SymbolCache(Session.GetLinearAllocator(), 1024*1024)
	, Session(Session)
{
	Resolver = TUniquePtr<SymbolResolverType>(new SymbolResolverType(Session));
	LoadSymbolsFromCache(Session.GetCache());
}

/////////////////////////////////////////////////////////////////////
template <typename SymbolResolverType>
TModuleProvider<SymbolResolverType>::~TModuleProvider()
{
	// Delete the resolver in order to flush all pending resolves
	Resolver.Reset();
	SaveSymbolsToCache(Session.GetCache());
}

/////////////////////////////////////////////////////////////////////
template<typename SymbolResolverType>
const FResolvedSymbol* TModuleProvider<SymbolResolverType>::GetSymbol(uint64 Address)
{
	{
		// Attempt to read from the cached symbols.
		FReadScopeLock _(SymbolsLock);
		if (const FResolvedSymbol** Entry = SymbolCacheLookup.Find(Address))
		{
			return *Entry;
		}
	}

	FResolvedSymbol* ResolvedSymbol;
	{
		// Add a pending entry to our cache
		FWriteScopeLock _(SymbolsLock);
		if (SymbolCacheLookup.Contains(Address))
		{
			return SymbolCacheLookup[Address];
		}
		ResolvedSymbol = &SymbolCache.EmplaceBack(ESymbolQueryResult::Pending, nullptr, nullptr, nullptr, 0);
		SymbolCacheLookup.Add(Address, ResolvedSymbol);
	}

	// If not in cache yet, queue it up in the resolver
	check(ResolvedSymbol);
	Resolver->QueueSymbolResolve(Address, ResolvedSymbol);

	return ResolvedSymbol;
}

/////////////////////////////////////////////////////////////////////
template<typename SymbolResolverType>
void TModuleProvider<SymbolResolverType>::GetStats(FStats* OutStats) const
{
	Resolver->GetStats(OutStats);
}

/////////////////////////////////////////////////////////////////////
template<typename SymbolResolverType>
void TModuleProvider<SymbolResolverType>::OnModuleLoad(const FStringView& Module, uint64 Base, uint32 Size, const uint8* ImageId, uint32 ImageIdSize)
{
	if (Module.Len() == 0)
	{
		return;
	}

	const TCHAR* Path = Session.StoreString(Module);
	const TCHAR* Name = Session.StoreString(Module);

	{
		FWriteScopeLock _(ModulesLock);
		Modules.EmplaceBack(Name, Path, Base, Size, false);
	}

	Resolver->QueueModuleLoad(Module, Base, Size, ImageId, ImageIdSize);
}

/////////////////////////////////////////////////////////////////////
template<typename SymbolResolverType>
void TModuleProvider<SymbolResolverType>::OnModuleUnload(uint64 Base)
{
	//todo: Find entry, set bLoaded to false
}

/////////////////////////////////////////////////////////////////////
template<typename SymbolResolverType>
void TModuleProvider<SymbolResolverType>::OnAnalysisComplete()
{
	Resolver->OnAnalysisComplete();
}

/////////////////////////////////////////////////////////////////////
template <typename SymbolResolverType>
void TModuleProvider<SymbolResolverType>::SaveSymbolsToCache(IAnalysisCache& Cache)
{
	// Create a temporary reverse lookup for symbol -> address
	TMap<const FResolvedSymbol*, uint64> SymbolReverseLookup;
	SymbolReverseLookup.Reserve(SymbolCacheLookup.Num());
	Algo::Transform(SymbolCacheLookup, SymbolReverseLookup, [](const TTuple<uint64, const FResolvedSymbol*>& Pair) { 
		return TTuple<const FResolvedSymbol*, uint64>(Pair.Value, Pair.Key);
	});

	// Save new symbols
	TCachedPagedArray<FSavedSymbol, 1024> SavedSymbols(TEXT("ModuleProvider.Symbols"), Cache);
	const uint32 NumPreviouslySavedSymbols = SavedSymbols.Num();
	uint32 NumSavedSymbols = 0;
	for (uint32 SymbolIndex = SavedSymbols.Num(); SymbolIndex < SymbolCache.Num(); ++SymbolIndex)
	{
		const FResolvedSymbol& Symbol = SymbolCache[SymbolIndex];
		if (Symbol.GetResult() != ESymbolQueryResult::OK)
		{
			continue;
		}
		const uint64* Address = SymbolReverseLookup.Find(&Symbol);
		const uint32 ModuleOffset = Strings.Store_GetOffset(Symbol.Module);
		const uint32 NameOffset = Strings.Store_GetOffset(Symbol.Name);
		const uint32 FileOffset = Strings.Store_GetOffset(Symbol.File);
		SavedSymbols.EmplaceBack(FSavedSymbol{*Address, ModuleOffset, NameOffset, FileOffset, Symbol.Line});
		++NumSavedSymbols;
	}
	UE_LOG(LogTraceServices, Display, TEXT("Added %d symbols to the %d previously saved symbols."), NumPreviouslySavedSymbols, NumSavedSymbols);
}

template <typename SymbolResolverType>
void TModuleProvider<SymbolResolverType>::LoadSymbolsFromCache(IAnalysisCache& Cache)
{
	// Load saved symbols
	TCachedPagedArray<FSavedSymbol, 1024> SavedSymbols(TEXT("ModuleProvider.Symbols"), Cache);
	for (uint64 SymbolIndex = 0; SymbolIndex < SavedSymbols.Num(); ++SymbolIndex)
	{
		const FSavedSymbol& Symbol = SavedSymbols[SymbolIndex];
		const TCHAR* Module = Strings.GetStringAtOffset(Symbol.ModuleOffset);
		const TCHAR* Name = Strings.GetStringAtOffset(Symbol.NameOffset);
		const TCHAR* File = Strings.GetStringAtOffset(Symbol.FileOffset);
		if (Module == nullptr || Name == nullptr || File == nullptr)
		{
			UE_LOG(LogTraceServices, Warning, TEXT("Found cached symbol (adress %llx) which referenced unknown string."), Symbol.Address);
			continue;
		}
		FResolvedSymbol& Resolved = SymbolCache.EmplaceBack(ESymbolQueryResult::OK, Module, Name, File, Symbol.Line);
		SymbolCacheLookup.Add(Symbol.Address, &Resolved);
	}
	UE_LOG(LogTraceServices, Display, TEXT("Loaded %d symbols from cache."), SymbolCacheLookup.Num());
}

/////////////////////////////////////////////////////////////////////
IModuleAnalysisProvider* CreateModuleProvider(IAnalysisSession& InSession, const FAnsiStringView& InSymbolFormat)
{
	IModuleAnalysisProvider* Provider(nullptr);
#if PLATFORM_WINDOWS
#if USE_SYMSLIB
	if (!Provider && InSymbolFormat.Equals("pdb"))
	{
		Provider = new TModuleProvider<FSymslibResolver>(InSession);
	}
#endif // USE_SYMSLIB
#if USE_DBGHELP
	if (!Provider && InSymbolFormat.Equals("pdb"))
	{
		Provider = new TModuleProvider<FDbgHelpResolver>(InSession);
	}
#endif // USE_DBGHELP
#endif //PLATFORM_WINDOWS
	return Provider;
}

} // namespace TraceServices

#undef USE_SYMSLIB
#undef USE_DBGHELP
