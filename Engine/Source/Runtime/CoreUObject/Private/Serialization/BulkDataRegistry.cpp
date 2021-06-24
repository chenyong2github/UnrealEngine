// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/BulkDataRegistry.h"

#if WITH_EDITOR

#include "Misc/ConfigCacheIni.h"

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

	virtual void Register(UPackage* Owner, const UE::Virtualization::FVirtualizedUntypedBulkData& BulkData) override {}
	virtual void OnExitMemory(const UE::Virtualization::FVirtualizedUntypedBulkData& BulkData) override {}
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

#endif // WITH_EDITOR