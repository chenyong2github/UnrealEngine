// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HoloLensTargetDevice.h: Declares the HoloLensTargetDevice class.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/ITargetDevice.h"
#include "Interfaces/ITargetPlatform.h"
#include "IHoloLensDeviceDetectorModule.h"

#include "Windows/AllowWindowsPlatformTypes.h"

/**
 * Implements a HoloLens target device.
 */
class FHoloLensTargetDevice
	: public ITargetDevice
{
public:

	/**
	 * Creates and initializes a new device for the specified target platform.
	 *
	 * @param InTargetPlatform - The target platform.
	 */
	FHoloLensTargetDevice(const ITargetPlatform& InTargetPlatform, const FHoloLensDeviceInfo& InInfo)
		: TargetPlatform(InTargetPlatform)
		, Info(InInfo)
	{ }


	virtual bool Connect() override
	{
		return Info.bCanDeployTo;
	}

	virtual bool Deploy(const FString& SourceFolder, FString& OutAppId) override;

	virtual void Disconnect() override
	{ }

	virtual ETargetDeviceTypes GetDeviceType() const override
	{
		if (Info.DeviceTypeName == HoloLensDeviceTypes::HoloLens)
		{
			return ETargetDeviceTypes::HMD;
		}
		else if (Info.DeviceTypeName == HoloLensDeviceTypes::HoloLensEmulation)
		{
			return ETargetDeviceTypes::Desktop;
		}
		return ETargetDeviceTypes::Indeterminate;
	}

	virtual FTargetDeviceId GetId() const override
	{
		if (Info.IsLocal())
		{
			return FTargetDeviceId(TargetPlatform.PlatformName(), Info.HostName);
		}
		// This is what gets handed off to UAT, so we need to supply the
		// actual Device Portal url instead of just the host name
		return FTargetDeviceId(TargetPlatform.PlatformName(), Info.WdpUrl);
	}

	virtual FString GetName() const override
	{
		return Info.HostName + TEXT(" (HoloLens)");
	}

	virtual FString GetOperatingSystemName() override
	{
		return FString::Printf(TEXT("HoloLens (%s)"), *Info.DeviceTypeName.ToString());
	}

	virtual int32 GetProcessSnapshot(TArray<FTargetDeviceProcessInfo>& OutProcessInfos) override
	{
		return 0;
	}

	virtual const class ITargetPlatform& GetTargetPlatform() const override
	{
		return TargetPlatform;
	}

	virtual bool GetUserCredentials(FString& OutUserName, FString& OutUserPassword) override
	{
		return false;
	}

	virtual bool IsConnected()
	{
		// @todo JoeG - Add a connected device check here
		return Info.bCanDeployTo;
	}

	virtual bool IsDefault() const override
	{
		return Info.HostName == FPlatformProcess::ComputerName();
	}

	virtual bool Launch(const FString& AppId, EBuildConfiguration BuildConfiguration, EBuildTargetType TargetType, const FString& Params, uint32* OutProcessId) override;

	virtual bool PowerOff(bool Force) override
	{
		// @todo JoeG - Add support if HL2 supports this
		return false;
	}

	virtual bool PowerOn() override
	{
		// @todo JoeG - Add support if HL2 supports this
		return false;
	}

	virtual bool Reboot(bool bReconnect = false) override
	{
		// @todo JoeG - Add support if HL2 supports this
		return false;
	}

	virtual bool Run(const FString& ExecutablePath, const FString& Params, uint32* OutProcessId) override;

	virtual void SetUserCredentials( const FString& UserName, const FString& UserPassword ) override { }

	virtual bool SupportsFeature(ETargetDeviceFeatures Feature) const override
	{
		// @todo JoeG - Add support if HL2 supports this
		return false;
	}

	virtual bool SupportsSdkVersion(const FString& VersionString) const override
	{
		// @todo JoeG - Add support for this check
		return false;
	}

	virtual bool TerminateProcess(const int64 ProcessId) override
	{
		// @todo JoeG - Add support if HL2 supports this
		return false;
	}

private:
	// Holds a reference to the device's target platform.
	const ITargetPlatform& TargetPlatform;

	FHoloLensDeviceInfo Info;
};

typedef TSharedPtr<FHoloLensTargetDevice, ESPMode::ThreadSafe> FHoloLensDevicePtr;

#include "Windows/HideWindowsPlatformTypes.h"
