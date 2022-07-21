// Copyright Epic Games, Inc. All Rights Reserved.

#include "Virtualization/VirtualizationSystem.h"

#include "CoreGlobals.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"

// Can be defined as 1 by programs target.cs files to disable the virtualization system
// entirely. This could be removed in the future if we move the virtualization module
// to be a plugin
#ifndef UE_DISABLE_VIRTUALIZATION_SYSTEM
	#define UE_DISABLE_VIRTUALIZATION_SYSTEM 0
#endif

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

	virtual bool Initialize(const FInitParams& InitParams) override
	{
		return true;
	}

	virtual bool IsEnabled() const override
	{
		return false;
	}

	virtual bool IsPushingEnabled(EStorageType StorageType) const override
	{
		return false;
	}

	virtual bool IsDisabledForObject(const UObject* Owner) const override
	{
		return false;
	}

	virtual bool AllowSubmitIfVirtualizationFailed() const override
	{
		return false;
	}

	virtual bool PushData(const FIoHash& Id, const FCompressedBuffer& Payload, EStorageType StorageType, const FString& Context) override
	{
		return false;
	}

	virtual bool PushData(TArrayView<FPushRequest> Requests, EStorageType StorageType) override
	{
		return false;
	}

	virtual FCompressedBuffer PullData(const FIoHash& Id) override
	{
		return FCompressedBuffer();
	}

	virtual EQueryResult QueryPayloadStatuses(TArrayView<const FIoHash> Ids, EStorageType StorageType, TArray<EPayloadStatus>& OutStatuses) override
	{
		OutStatuses.Reset();

		return EQueryResult::Failure_NotImplemented;
	}

	virtual EVirtualizationResult TryVirtualizePackages(const TArray<FString>& FilesToVirtualize, TArray<FText>& OutDescriptionTags, TArray<FText>& OutErrors) override
	{
		OutDescriptionTags.Reset();
		OutErrors.Reset();

		OutErrors.Add(FText::FromString(TEXT("Calling ::TryVirtualizePackages on FNullVirtualizationSystem")));

		return EVirtualizationResult::Failed;
	}

	virtual ERehydrationResult TryRehydratePackages(const TArray<FString>& Packages, TArray<FText>& OutErrors) override
	{
		OutErrors.Reset();

		OutErrors.Add(FText::FromString(TEXT("Calling ::TryRehydratePackages on FNullVirtualizationSystem")));

		return ERehydrationResult::Failed;
	}

	virtual void DumpStats() const override
	{
		// The null implementation will have no stats and nothing to log
	}

	virtual void GetPayloadActivityInfo(GetPayloadActivityInfoFuncRef) const override
	{
		// The null implementation has no stats and nothing to invoke
	}

	virtual FPayloadActivityInfo GetAccumualtedPayloadActivityInfo() const override
	{
		return FPayloadActivityInfo();
	}

	virtual FOnNotification& GetNotificationEvent() override
	{
		return NotificationEvent;
	}

	FOnNotification NotificationEvent;
};

TUniquePtr<IVirtualizationSystem> GVirtualizationSystem = nullptr;

/** Utility function for finding a IVirtualizationSystemFactory for a given system name */
Private::IVirtualizationSystemFactory* FindFactory(FName SystemName)
{
	TArray<Private::IVirtualizationSystemFactory*> AvaliableSystems = IModularFeatures::Get().GetModularFeatureImplementations<Private::IVirtualizationSystemFactory>(FName("VirtualizationSystem"));
	for (Private::IVirtualizationSystemFactory* SystemFactory : AvaliableSystems)
	{
		if (SystemFactory->GetName() == SystemName)
		{
			return SystemFactory;
		}
	}

	return nullptr;
}

void Initialize()
{
	FApp::GetProjectName();

	const FConfigFile* ConfigFile = GConfig->Find(GEngineIni);

	if (ConfigFile != nullptr)
	{
		FInitParams InitParams(FApp::GetProjectName() , *ConfigFile);
		Initialize(InitParams);
	}
	else
	{
		UE_LOG(LogVirtualization, Error, TEXT("Unable to find a valid engine config file when trying to create the virtualization system"));

		GVirtualizationSystem = MakeUnique<FNullVirtualizationSystem>();

		FConfigFile EmptyConfigFile;
		FInitParams DummyParams(TEXT(""), EmptyConfigFile);

		GVirtualizationSystem->Initialize(DummyParams);
	}	
}

void Initialize(const FInitParams& InitParams)
{
	FName SystemName;

#if UE_DISABLE_VIRTUALIZATION_SYSTEM == 0
	FString RawSystemName;
	if (InitParams.ConfigFile.GetString(TEXT("Core.ContentVirtualization"), TEXT("SystemName"), RawSystemName))
	{
		SystemName = FName(RawSystemName);
		UE_LOG(LogVirtualization, Display, TEXT("VirtualizationSystem name found in ini file: %s"), *RawSystemName);
	}
#else
	UE_LOG(LogVirtualization, Display, TEXT("The virtualization system has been disabled by code"));
#endif //UE_DISABLE_VIRTUALIZATION_SYSTEM

	if (!SystemName.IsNone())
	{
		Private::IVirtualizationSystemFactory* SystemFactory = FindFactory(SystemName);
		if (SystemFactory != nullptr)
		{
			GVirtualizationSystem = SystemFactory->Create();
			check(GVirtualizationSystem.IsValid()); // It is assumed that create will always return a valid pointer

			if (!GVirtualizationSystem->Initialize(InitParams))
			{
				UE_LOG(LogVirtualization, Error, TEXT("Initialization of the virtualization system '%s' failed, falling back to the default implementation"), *SystemName.ToString());
				GVirtualizationSystem.Reset();
			}
		}
		else
		{
			UE_LOG(LogVirtualization, Error, TEXT("Unable to find factory to create the virtualization system: %s"), *SystemName.ToString());
		}
	}

	// If we found no system to create so we will use the fallback system
	if (!GVirtualizationSystem.IsValid())
	{
		GVirtualizationSystem = MakeUnique<FNullVirtualizationSystem>();
		GVirtualizationSystem->Initialize(InitParams);
	}
}

void Shutdown()
{
	GVirtualizationSystem.Reset();
	UE_LOG(LogVirtualization, Log, TEXT("UE::Virtualization was shutdown"));
}

IVirtualizationSystem& IVirtualizationSystem::Get()
{
	// For now allow Initialize to be called directly if it was not called explicitly.
	if (GVirtualizationSystem == nullptr)
	{
		UE_LOG(LogVirtualization, Warning, TEXT("UE::Virtualization::Initialize was not called before UE::Virtualization::IVirtualizationSystem::Get()!"));
		UE::Virtualization::Initialize();
	}

	return *GVirtualizationSystem;
}

} // namespace UE::Virtualization
