// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/PCGGraphCache.h"

#include "PCGComponent.h"
#include "PCGModule.h"

#include "HAL/IConsoleManager.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeRWLock.h"

static TAutoConsoleVariable<bool> CVarCacheDebugging(
	TEXT("pcg.CacheDebugging"),
	false,
	TEXT("Enable various features for debugging the graph cache system."));

FPCGGraphCacheEntry::FPCGGraphCacheEntry(const FPCGCrc& InDependenciesCrc, const FPCGDataCollection& InOutput, FPCGRootSet& OutRootSet)
	: Output(InOutput)
	, DependenciesCrc(InDependenciesCrc)
{
	Output.AddToRootSet(OutRootSet);
}

FPCGGraphCache::FPCGGraphCache(TWeakObjectPtr<UObject> InOwner, FPCGRootSet* InRootSet)
	: Owner(InOwner), RootSet(InRootSet)
{
	check(InOwner.Get() && InRootSet);
}

FPCGGraphCache::~FPCGGraphCache()
{
	ClearCache();
}

bool FPCGGraphCache::GetFromCache(const UPCGNode* InNode, const IPCGElement* InElement, const FPCGCrc& InDependenciesCrc, const FPCGDataCollection& InInput, const UPCGSettings* InSettings, const UPCGComponent* InComponent, FPCGDataCollection& OutOutput) const
{
	if (!Owner.IsValid())
	{
		return false;
	}

	if(!InDependenciesCrc.IsValid())
	{
		UE_LOG(LogPCG, Warning, TEXT("Invalid dependencies passed to FPCGGraphCache::GetFromCache(), lookup aborted."));
		return false;
	}

	const bool bDebuggingEnabled = IsDebuggingEnabled() && InComponent && InComponent->GetOwner() && InNode;

	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGraphCache::GetFromCache);
	FReadScopeLock ScopedReadLock(CacheLock);

	if (const FPCGGraphCacheEntries* Entries = CacheData.Find(InElement))
	{
		for (const FPCGGraphCacheEntry& Entry : *Entries)
		{
			if (Entry.DependenciesCrc == InDependenciesCrc)
			{
				OutOutput = Entry.Output;

				if (bDebuggingEnabled)
				{
					// Leading spaces to align log content with warnings below - helps readability a lot.
					UE_LOG(LogPCG, Log, TEXT("         [%s] %s\t\tCACHE HIT %u"), *InComponent->GetOwner()->GetName(), *InNode->GetNodeTitle().ToString(), InDependenciesCrc.GetValue());
				}

				return true;
			}
		}

		if (bDebuggingEnabled)
		{
			UE_LOG(LogPCG, Warning, TEXT("[%s] %s\t\tCACHE MISS %u"), *InComponent->GetOwner()->GetName() , *InNode->GetNodeTitle().ToString(), InDependenciesCrc.GetValue());
		}

		return false;
	}

	if (bDebuggingEnabled)
	{
		UE_LOG(LogPCG, Warning, TEXT("[%s] %s\t\tCACHE MISS NOELEMENT"), *InComponent->GetOwner()->GetName() , *InNode->GetNodeTitle().ToString());
	}

	return false;
}

void FPCGGraphCache::StoreInCache(const IPCGElement* InElement, const FPCGCrc& InDependenciesCrc, const FPCGDataCollection& InInput, const UPCGSettings* InSettings, const UPCGComponent* InComponent, const FPCGDataCollection& InOutput)
{
	if (!Owner.IsValid() || !ensure(InDependenciesCrc.IsValid()))
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGraphCache::StoreInCache);
	FWriteScopeLock ScopedWriteLock(CacheLock);

	FPCGGraphCacheEntries* Entries = CacheData.Find(InElement);
	if(!Entries)
	{
		Entries = &(CacheData.Add(InElement));
	}

	Entries->Emplace(InDependenciesCrc, InOutput, *RootSet);
}

void FPCGGraphCache::ClearCache()
{
	FWriteScopeLock ScopedWriteLock(CacheLock);

	// Unroot all previously rooted data
	for (TPair<const IPCGElement*, FPCGGraphCacheEntries>& CacheEntry : CacheData)
	{
		for (FPCGGraphCacheEntry& Entry : CacheEntry.Value)
		{
			Entry.Output.RemoveFromRootSet(*RootSet);
		}
	}

	// Remove all entries
	CacheData.Reset();
}

#if WITH_EDITOR
void FPCGGraphCache::CleanFromCache(const IPCGElement* InElement, const UPCGSettings* InSettings/*= nullptr*/)
{
	if (!InElement)
	{
		return;
	}

	if (IsDebuggingEnabled() && InSettings)
	{
		UE_LOG(LogPCG, Warning, TEXT("CACHE: PURGED [%s]"), *InSettings->GetDefaultNodeName().ToString());
	}

	FWriteScopeLock ScopeWriteLock(CacheLock);
	FPCGGraphCacheEntries* Entries = CacheData.Find(InElement);
	if (Entries)
	{
		for (FPCGGraphCacheEntry& Entry : *Entries)
		{
			Entry.Output.RemoveFromRootSet(*RootSet);
		}
	}

	// Finally, remove all entries matching that element
	CacheData.Remove(InElement);
}

uint32 FPCGGraphCache::GetGraphCacheEntryCount(IPCGElement* InElement) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGraphCache::GetFromCache);
	FReadScopeLock ScopedReadLock(CacheLock);

	if (const FPCGGraphCacheEntries* Entries = CacheData.Find(InElement))
	{
		return Entries->Num();
	}

	return 0;
}
#endif // WITH_EDITOR

bool FPCGGraphCache::IsDebuggingEnabled() const
{
	return CVarCacheDebugging.GetValueOnAnyThread();
}
