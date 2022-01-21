// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/BulkDataRegistry.h"

#if WITH_EDITOR

#include "Containers/Array.h"
#include "Containers/Set.h"
#include "HAL/CriticalSection.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeRWLock.h"
#include "Serialization/EditorBulkData.h"
#include "UObject/Package.h"
#include "UObject/PackageResourceManager.h"
#include "UObject/UObjectGlobals.h"

namespace UE::BulkDataRegistry::Private
{

IBulkDataRegistry* GBulkDataRegistry = nullptr;
FSetBulkDataRegistry GSetBulkDataRegistry;

/** A stub class to provide a return value from IBulkDataRegistry::Get() when the registery is disabled. */
class FBulkDataRegistryNull : public IBulkDataRegistry
{
public:
	FBulkDataRegistryNull() = default;
	virtual ~FBulkDataRegistryNull() {}

	virtual void Register(UPackage* Owner, const UE::Serialization::FEditorBulkData& BulkData) override {}
	virtual void OnExitMemory(const UE::Serialization::FEditorBulkData& BulkData) override {}
	virtual TFuture<UE::BulkDataRegistry::FMetaData> GetMeta(const FGuid& BulkDataId) override
	{
		TPromise<UE::BulkDataRegistry::FMetaData> Promise;
		Promise.SetValue(UE::BulkDataRegistry::FMetaData{ false, FIoHash(), 0 });
		return Promise.GetFuture();
	}
	virtual TFuture<UE::BulkDataRegistry::FData> GetData(const FGuid& BulkDataId) override
	{
		TPromise<UE::BulkDataRegistry::FData> Promise;
		Promise.SetValue(UE::BulkDataRegistry::FData{ false, FCompressedBuffer() });
		return Promise.GetFuture();
	}
	virtual uint64 GetBulkDataResaveSize(FName PackageName) override
	{
		return 0;
	}
};

class FBulkDataRegistryTrackBulkDataToResave : public FBulkDataRegistryNull
{
public:
	FBulkDataRegistryTrackBulkDataToResave() = default;
	virtual ~FBulkDataRegistryTrackBulkDataToResave() {}

	virtual void Register(UPackage* Owner, const UE::Serialization::FEditorBulkData& BulkData) override
	{
		ResaveSizeTracker.Register(Owner, BulkData);
	}
	virtual uint64 GetBulkDataResaveSize(FName PackageName) override
	{
		return ResaveSizeTracker.GetBulkDataResaveSize(PackageName);
	}

private:
	FResaveSizeTracker ResaveSizeTracker;
};

}

bool IBulkDataRegistry::IsEnabled()
{
	bool bEnabled = true;
	GConfig->GetBool(TEXT("CookSettings"), TEXT("BulkDataRegistryEnabled"), bEnabled, GEditorIni);
	return bEnabled;
}

IBulkDataRegistry& IBulkDataRegistry::Get()
{
	check(UE::BulkDataRegistry::Private::GBulkDataRegistry);
	return *UE::BulkDataRegistry::Private::GBulkDataRegistry;
}

void IBulkDataRegistry::Initialize()
{
	using namespace UE::BulkDataRegistry::Private;

	if (IsEnabled())
	{
		FSetBulkDataRegistry& SetRegistryDelegate = GetSetBulkDataRegistryDelegate();
		if (SetRegistryDelegate.IsBound())
		{
			// Allow the editor or licensee project to define the BulkDataRegistry
			GBulkDataRegistry = SetRegistryDelegate.Execute();
		}
	}
	else if (IsEditorDomainEnabled())
	{
		GBulkDataRegistry = new FBulkDataRegistryTrackBulkDataToResave();
	}

	// Assign the null BulkDataRegistry if it was disabled or not set by a higher level source
	if (!GBulkDataRegistry)
	{
		GBulkDataRegistry = new FBulkDataRegistryNull();
	}
}

void IBulkDataRegistry::Shutdown()
{
	delete UE::BulkDataRegistry::Private::GBulkDataRegistry;
	UE::BulkDataRegistry::Private::GBulkDataRegistry = nullptr;
}

FSetBulkDataRegistry& IBulkDataRegistry::GetSetBulkDataRegistryDelegate()
{
	return UE::BulkDataRegistry::Private::GSetBulkDataRegistry;
}

namespace UE::GlobalBuildInputResolver::Private
{
UE::DerivedData::IBuildInputResolver* GGlobalBuildInputResolver = nullptr;
}

UE::DerivedData::IBuildInputResolver* GetGlobalBuildInputResolver()
{
	return UE::GlobalBuildInputResolver::Private::GGlobalBuildInputResolver;
}

void SetGlobalBuildInputResolver(UE::DerivedData::IBuildInputResolver* InResolver)
{
	UE::GlobalBuildInputResolver::Private::GGlobalBuildInputResolver = InResolver;
}

namespace UE::BulkDataRegistry::Private
{

FResaveSizeTracker::FResaveSizeTracker()
{
	FCoreUObjectDelegates::OnEndLoadPackage.AddRaw(this, &FResaveSizeTracker::OnEndLoadPackage);
	ELoadingPhase::Type CurrentPhase = IPluginManager::Get().GetLastCompletedLoadingPhase();
	if (CurrentPhase == ELoadingPhase::None || CurrentPhase < ELoadingPhase::PostEngineInit)
	{
		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FResaveSizeTracker::OnPostEngineInit);
	}
	else
	{
		OnPostEngineInit();
	}
	FCoreUObjectDelegates::OnEndLoadPackage.AddRaw(this, &FResaveSizeTracker::OnEndLoadPackage);
}

FResaveSizeTracker::~FResaveSizeTracker()
{
	FCoreUObjectDelegates::OnEndLoadPackage.RemoveAll(this);
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
}

void FResaveSizeTracker::OnPostEngineInit()
{
	bPostEngineInitComplete = true;

	FReadScopeLock ScopeLock(Lock);
	DeferredRemove.Reserve(PackageBulkResaveSize.Num());
	for (const TPair<FName, uint64>& Pair : PackageBulkResaveSize)
	{
		DeferredRemove.Add(Pair.Key);
	}
}

void FResaveSizeTracker::Register(UPackage* Owner, const UE::Serialization::FEditorBulkData& BulkData)
{
	if (!BulkData.GetIdentifier().IsValid() || !BulkData.IsMemoryOnlyPayload())
	{
		return;
	}

	if (!Owner
		|| !Owner->GetFileSize() // We only track disk packages
		|| (bPostEngineInitComplete && Owner->GetHasBeenEndLoaded()) // We only record BulkDatas that are loaded before the package finishes loading
		)
	{
		return;
	}

	FWriteScopeLock ScopeLock(Lock);
	PackageBulkResaveSize.FindOrAdd(Owner->GetFName()) += BulkData.GetPayloadSize();
}

uint64 FResaveSizeTracker::GetBulkDataResaveSize(FName PackageName)
{
	FReadScopeLock ScopeLock(Lock);
	return PackageBulkResaveSize.FindRef(PackageName);
}

void FResaveSizeTracker::OnEndLoadPackage(TConstArrayView<UPackage*> LoadedPackages)
{
	if (!bPostEngineInitComplete)
	{
		return;
	}

	TArray<FName> PackageNames;
	PackageNames.Reserve(LoadedPackages.Num());
	for (UPackage* LoadedPackage : LoadedPackages)
	{
		PackageNames.Add(LoadedPackage->GetFName());
	}

	FWriteScopeLock ScopeLock(Lock);
	// The contract for GetBulkDataResaveSize specifies that we must answer correctly until
	// OnEndLoadPackage is complete. This includes being called from other subscribers to OnEndLoadPackage
	// that might run after us. So we defer the removals from PackageBulkResaveSize until the next call 
	// to OnEndLoadPackage.
	for (FName PackageName : DeferredRemove)
	{
		PackageBulkResaveSize.Remove(PackageName);
	}
	DeferredRemove.Reset();
	DeferredRemove.Append(MoveTemp(PackageNames));
}

}
#endif // WITH_EDITOR