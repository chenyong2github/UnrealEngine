// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModuleProvider.h"
#include <atomic>

#include "Algo/Find.h"
#include "Algo/Transform.h"
#include "Common/CachedPagedArray.h"
#include "Common/CachedStringStore.h"
#include "Common/Utils.h"
#include "Containers/Map.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
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
template<typename SymbolResolverType>
class TModuleProvider : public IModuleAnalysisProvider
{
public:
	explicit TModuleProvider(IAnalysisSession& Session);
	virtual ~TModuleProvider();

	//Query interface
	const FResolvedSymbol*		GetSymbol(uint64 Address) override;
	uint32						GetNumModules() const override;
	void						EnumerateModules(uint32 Start, TFunctionRef<void(const FModule& Module)> Callback) const override;
	FGraphEventRef				LoadSymbolsForModuleUsingPath(uint64 Base, const TCHAR* Path) override;
	void						EnumerateSymbolSearchPaths(TFunctionRef<void(FStringView Path)> Callback) const override;
	void						GetStats(FStats* OutStats) const override;

	//Analysis interface
	void						OnModuleLoad(const FStringView& Module, uint64 Base, uint32 Size, const uint8* Checksum, uint32 ChecksumSize) override;
	void 						OnModuleUnload(uint64 Base) override;
	void						OnAnalysisComplete() override;
	void						SaveSymbolsToCache(IAnalysisCache& Cache);
	void						LoadSymbolsFromCache(IAnalysisCache& Cache);

private:
	uint32						GetNumCachedSymbolsFromModule(uint64 Base, uint32 Size);

	struct FSavedSymbol
	{
		uint64 Address;
		uint32 ModuleOffset;
		uint32 NameOffset;
		uint32 FileOffset;
		uint32 Line;
	};
	
	mutable FRWLock				ModulesLock;
	TPagedArray<FModule>		Modules;

	FRWLock				SymbolsLock;
	// Persistently stored symbol strings
	FCachedStringStore Strings;
	// Efficient representation of symbols
	TPagedArray<FResolvedSymbol> SymbolCache;
	// Lookup table to for symbols
	TMap<uint64, const FResolvedSymbol*> SymbolCacheLookup;
	// Number of cached symbols that was loaded (used for stats)
	uint32 NumCachedSymbols;
	// Number of discovered symbols
	std::atomic<uint32> SymbolsDiscovered;
	
	IAnalysisSession&			Session;
	FString						Platform;
	TUniquePtr<SymbolResolverType>	Resolver;
	FGraphEventRef				LoadSymbolsTask;
	bool						LoadSymbolsAbort = false;
};

/////////////////////////////////////////////////////////////////////
template<typename SymbolResolverType>
TModuleProvider<SymbolResolverType>::TModuleProvider(IAnalysisSession& Session)
	: Modules(Session.GetLinearAllocator(), 128)
	, Strings(TEXT("ModuleProvider.Strings"), Session.GetCache())
	, SymbolCache(Session.GetLinearAllocator(), 1024*1024)
	, NumCachedSymbols(0)
	, Session(Session)
{
	Resolver = TUniquePtr<SymbolResolverType>(new SymbolResolverType(Session));
	LoadSymbolsFromCache(Session.GetCache());
}

/////////////////////////////////////////////////////////////////////
template <typename SymbolResolverType>
TModuleProvider<SymbolResolverType>::~TModuleProvider()
{
	if (LoadSymbolsTask)
	{
		LoadSymbolsAbort = true;
		LoadSymbolsTask->Wait();
	}
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
		++SymbolsDiscovered;
	}

	// If not in cache yet, queue it up in the resolver
	check(ResolvedSymbol);
	Resolver->QueueSymbolResolve(Address, ResolvedSymbol);

	return ResolvedSymbol;
}

/////////////////////////////////////////////////////////////////////
template <typename SymbolResolverType>
uint32 TModuleProvider<SymbolResolverType>::GetNumModules() const
{
	FReadScopeLock _(ModulesLock);
	return Modules.Num();
}

/////////////////////////////////////////////////////////////////////
template <typename SymbolResolverType>
void TModuleProvider<SymbolResolverType>::EnumerateModules(uint32 Start, TFunctionRef<void(const FModule& Module)> Callback) const 
{
	FReadScopeLock _(ModulesLock);
	for (uint32 i = Start; i < Modules.Num(); ++i)
	{
		Callback(Modules[i]);
	}
}

/////////////////////////////////////////////////////////////////////
template <typename SymbolResolverType>
FGraphEventRef TModuleProvider<SymbolResolverType>::LoadSymbolsForModuleUsingPath(uint64 Base, const TCHAR* Path)
{
	FReadScopeLock _(ModulesLock);
	const FModule* Module = Algo::FindBy(Modules, Base, &FModule::Base);
	if (Module)
	{
		const FString FullPath = FPaths::ConvertRelativePathToFull(Path);
		if (Resolver && !FullPath.IsEmpty() && Module->Status.load() != EModuleStatus::Loaded)
		{
			// Setup a task to queue and watch the queued module. If it succeeds in resolving with the new path
			// re-resolve any cached symbols 
			LoadSymbolsTask = FFunctionGraphTask::CreateAndDispatchWhenReady([this, Module, FullPath]()
			{
				auto ReloadModuleFn = [this] (const FModule* InModule, const TCHAR* InPath)
				{
					const uint32 DiscoveredSymbols = InModule->Stats.Discovered.load();
					const uint64 ModuleBegin = InModule->Base;
					const uint64 ModuleEnd = InModule->Base + InModule->Size;

					auto ReresolveOnSuccess = [this, DiscoveredSymbols, ModuleBegin, ModuleEnd] (TArray<TTuple<uint64,FResolvedSymbol*>>& OutSymbols)
					{
						OutSymbols.Reserve(DiscoveredSymbols);
					
						FReadScopeLock _(SymbolsLock);
						for (auto Pair : SymbolCacheLookup)
						{
							const uint64 Address = Pair.template Get<0>();
							FResolvedSymbol* Symbol = const_cast<FResolvedSymbol*>(Pair.template Get<1>());
							if (FMath::IsWithin(Address, ModuleBegin, ModuleEnd))
							{
								OutSymbols.Add(TTuple<uint64,FResolvedSymbol*>(Address, Symbol));
							}
						}
					};

					Resolver->QueueModuleReload(InModule, InPath, ReresolveOnSuccess);
					// Wait for the resolver to do it's work
					while (InModule->Status.load() == EModuleStatus::Pending)
					{
						FPlatformProcess::Sleep(.1);
					}

					return InModule->Status.load();
				};
			
				UE_LOG(LogTraceServices, Display, TEXT("Queing symbol loading using path %s."), *FullPath);

				// Load the requested module
				const EModuleStatus Result = ReloadModuleFn(Module, *FullPath);

				if (Result == EModuleStatus::Loaded)
				{
					// Queue up any other failed module using the directory.
					IPlatformFile* PlatformFile = &FPlatformFileManager::Get().GetPlatformFile();
					const FString Directory = PlatformFile->DirectoryExists(*FullPath) ? FullPath : FPaths::GetPath(FullPath);
					for (auto& OtherModule : Modules)
					{
						if (LoadSymbolsAbort)
						{
							return;
						}
						const EModuleStatus ModuleStatus = OtherModule.Status.load();
						if (&OtherModule != Module && ModuleStatus != EModuleStatus::Loaded && ModuleStatus != EModuleStatus::Pending)
						{
							ReloadModuleFn(&OtherModule, *Directory);
						}
					}
				}

				UE_LOG(LogTraceServices, Display, TEXT("Loading symbols for path %s complete."), *FullPath);
			});

			return LoadSymbolsTask;
		}
	}
	return FGraphEventRef();
}

/////////////////////////////////////////////////////////////////////
template <typename SymbolResolverType>
void TModuleProvider<SymbolResolverType>::EnumerateSymbolSearchPaths(TFunctionRef<void(FStringView Path)> Callback) const
{
	Resolver->EnumerateSymbolSearchPaths(Callback);
}

/////////////////////////////////////////////////////////////////////
template<typename SymbolResolverType>
void TModuleProvider<SymbolResolverType>::GetStats(FStats* OutStats) const
{
	Resolver->GetStats(OutStats);
	OutStats->SymbolsDiscovered = SymbolsDiscovered.load();
	// Add the cached symbols (the resolver doesn't know about them)
	OutStats->SymbolsResolved += NumCachedSymbols;
}

/////////////////////////////////////////////////////////////////////
template<typename SymbolResolverType>
void TModuleProvider<SymbolResolverType>::OnModuleLoad(const FStringView& Module, uint64 Base, uint32 Size, const uint8* ImageId, uint32 ImageIdSize)
{
	if (Module.Len() == 0)
	{
		return;
	}

	const FStringView NameTemp = FPathViews::GetCleanFilename(Module);
	const TCHAR* Name = Session.StoreString(NameTemp);
	const TCHAR* FullName = Session.StoreString(Module);

	FWriteScopeLock _(ModulesLock);
	FModule& NewModule = Modules.EmplaceBack(Name, FullName, Base, Size, EModuleStatus::Pending);

	// The number of cached symbols for this module is equal to the number
	// of matching addresses in the cache.
	NewModule.Stats.Cached = GetNumCachedSymbolsFromModule(Base, Size);
	
	Resolver->QueueModuleLoad(ImageId, ImageIdSize, &NewModule);
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
	UE_LOG(LogTraceServices, Display, TEXT("Added %d symbols to the %d previously saved symbols."), NumSavedSymbols, NumPreviouslySavedSymbols);
}

/////////////////////////////////////////////////////////////////////
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
	NumCachedSymbols = SymbolCacheLookup.Num();
	UE_LOG(LogTraceServices, Display, TEXT("Loaded %d symbols from cache."), SymbolCacheLookup.Num());
}

/////////////////////////////////////////////////////////////////////
template <typename SymbolResolverType>
uint32 TModuleProvider<SymbolResolverType>::GetNumCachedSymbolsFromModule(uint64 Base, uint32 Size)
{
	uint32 Count(0);
	const uint64 Start = Base;
	const uint64 End = Base + Size;
	FWriteScopeLock _(SymbolsLock);
	for (auto& AddressSymbolPair : SymbolCacheLookup)
	{
		const uint64 Address = AddressSymbolPair.template Get<0>();
		if (FMath::IsWithin(Address, Start, End))
		{
			++Count;
		}
	}
	return Count;
}

/////////////////////////////////////////////////////////////////////
IModuleAnalysisProvider* CreateModuleProvider(IAnalysisSession& InSession, const FAnsiStringView& InSymbolFormat)
{
	IModuleAnalysisProvider* Provider(nullptr);
#if PLATFORM_WINDOWS && USE_SYMSLIB
	if (!Provider && (InSymbolFormat.Equals("pdb") || InSymbolFormat.Equals("dwarf")))
	{
		Provider = new TModuleProvider<FSymslibResolver>(InSession);
	}
#endif // PLATFORM_WINDOWS && USE_SYMSLIB
#if PLATFORM_WINDOWS && USE_DBGHELP
	if (!Provider && InSymbolFormat.Equals("pdb"))
	{
		Provider = new TModuleProvider<FDbgHelpResolver>(InSession);
	}
#endif // PLATFORM_WINDOWS && USE_DBGHELP
	return Provider;
}

/////////////////////////////////////////////////////////////////////
FName GetModuleProviderName()
{
	static FName Name(TEXT("ModuleProvider"));
	return Name;
}

/////////////////////////////////////////////////////////////////////
const IModuleProvider* ReadModuleProvider(const IAnalysisSession& Session)
{
	return Session.ReadProvider<IModuleProvider>(GetModuleProviderName());
}

/////////////////////////////////////////////////////////////////////

} // namespace TraceServices

#undef USE_SYMSLIB
#undef USE_DBGHELP
