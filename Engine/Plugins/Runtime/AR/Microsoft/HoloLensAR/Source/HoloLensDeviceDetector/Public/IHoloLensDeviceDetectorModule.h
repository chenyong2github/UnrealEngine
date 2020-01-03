// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformProcess.h"

namespace HoloLensDeviceTypes
{
	extern HOLOLENSDEVICEDETECTOR_API const FName HoloLens;
	extern HOLOLENSDEVICEDETECTOR_API const FName HoloLensEmulation;
}

struct FHoloLensDeviceInfo
{
	FString HostName;
	FString WdpUrl;
	FString WindowsDeviceId;
	FName DeviceTypeName;
	bool bIs64Bit;
	bool bRequiresCredentials;
	bool bCanDeployTo;

	bool IsLocal() const
	{
		return DeviceTypeName == HoloLensDeviceTypes::HoloLensEmulation;
	}
};

class IHoloLensDeviceDetectorModule :
	public IModuleInterface
{
public:
	static inline IHoloLensDeviceDetectorModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IHoloLensDeviceDetectorModule>("HoloLensDeviceDetector");
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("HoloLensDeviceDetector");
	}

public:
	virtual void StartDeviceDetection() = 0;
	virtual void StopDeviceDetection() = 0;

	DECLARE_EVENT_OneParam(IHoloLensDeviceDetectorModule, FOnDeviceDetected, const FHoloLensDeviceInfo&);
	virtual FOnDeviceDetected& OnDeviceDetected() = 0;

	virtual const TArray<FHoloLensDeviceInfo> GetKnownDevices() = 0;
};

