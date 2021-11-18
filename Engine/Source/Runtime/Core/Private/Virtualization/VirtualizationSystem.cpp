// Copyright Epic Games, Inc. All Rights Reserved.

#include "Virtualization/VirtualizationSystem.h"

#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"

namespace UE::Virtualization
{

/** Default implementation to be used when the system is disabled */
class FNullVirtualizationSystem : public IVirtualizationSystem
{
public:
	FNullVirtualizationSystem()
	{
		UE_LOG(LogVirtualization, Log, TEXT("FNullVirtualizationSystem mounted, virtualization will be disabled"));
	}

	virtual ~FNullVirtualizationSystem() = default;

	virtual bool IsEnabled() const override
	{
		return false;
	}

	virtual bool PushData(const FPayloadId& Id, const FCompressedBuffer& Payload, EStorageType StorageType, const FPackagePath& Package) override
	{
		return false;
	}

	virtual FCompressedBuffer PullData(const FPayloadId& Id) override
	{
		return FCompressedBuffer();
	}

	virtual void GetPayloadActivityInfo(GetPayloadActivityInfoFuncRef) const override
	{
	}

	virtual FPayloadActivityInfo GetAccumualtedPayloadActivityInfo() const override
	{
		return FPayloadActivityInfo();
	}
};

TUniquePtr<IVirtualizationSystem> GVirtualizationSystem = nullptr;

void Initialize()
{
	FName SystemName;

	FConfigFile EngineIni;
	if (FConfigCacheIni::LoadLocalIniFile(EngineIni, TEXT("Engine"), true))
	{
		FString RawSystemName;
		if (EngineIni.GetString(TEXT("Core.ContentVirtualization"), TEXT("SystemName"), RawSystemName))
		{
			SystemName = FName(RawSystemName);
			UE_LOG(LogVirtualization, Log, TEXT("VirtualizationSystem name found in ini file: %s"), *RawSystemName);
		}
	}

	if (!SystemName.IsNone())
	{
		TArray<Private::IVirtualizationSystemFactory*> AvaliableSystems = IModularFeatures::Get().GetModularFeatureImplementations<Private::IVirtualizationSystemFactory>(FName("VirtualizationSystem"));
		for (Private::IVirtualizationSystemFactory* SystemFactory : AvaliableSystems)
		{
			if (SystemFactory->GetName() == SystemName)
			{
				GVirtualizationSystem = SystemFactory->Create();
				return;
			}
		}
	}

	// We found no system to create so we will use the fallback Null system
	GVirtualizationSystem = MakeUnique<FNullVirtualizationSystem>();	
}

IVirtualizationSystem& IVirtualizationSystem::Get()
{
	// For now allow Initialize to be called directly if it was not called explicitly.
	if (GVirtualizationSystem == nullptr)
	{
		UE_LOG(LogVirtualization, Warning, TEXT("UE::Virtualization::Initialize was not called before UE::Virtualization::IVirtualizationSystem::Get()!"));
		Initialize();
	}

	return *GVirtualizationSystem;
}

} // namespace UE::Virtualization
