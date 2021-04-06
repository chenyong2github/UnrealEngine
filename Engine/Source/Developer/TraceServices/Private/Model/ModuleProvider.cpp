// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModuleProvider.h"
#include "Containers/Map.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Diagnostics.h"
#include "HAL/RunnableThread.h"
#include <atomic>

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
	void						OnModuleLoad(const FStringView& Module, uint64 Base, uint32 Size, const uint8* Checksum, uint32 ChecksumSize) override;
	void 						OnModuleUnload(uint64 Base) override;
	void						OnAnalysisComplete() override;

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

	FResolvedSymbol* ResolvedSymbol;
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
void TModuleProvider<SymbolProvider>::OnModuleLoad(const FStringView& Module, uint64 Base, uint32 Size, const uint8* ImageId, uint32 ImageIdSize)
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

	Resolver.QueueModuleLoad(Module, Base, Size, ImageId, ImageIdSize);
}

/////////////////////////////////////////////////////////////////////
template<typename SymbolProvider>
void TModuleProvider<SymbolProvider>::OnModuleUnload(uint64 Base)
{
	//todo: Find entry, set bLoaded to false
}

/////////////////////////////////////////////////////////////////////
template<typename SymbolResolver>
void TModuleProvider<SymbolResolver>::OnAnalysisComplete()
{
	Resolver.OnAnalysisComplete();
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
