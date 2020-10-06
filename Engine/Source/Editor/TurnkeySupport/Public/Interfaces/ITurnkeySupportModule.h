// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class SWidget;

enum class ETurnkeyPlatformSdkStatus : uint8
{
	Unknown,
	Querying,
	Valid,
	OutOfDate,
	NoSdk,
	Error,
	FlashValid,
	FlashOutOfDate,
	// @todo turnkey: add AutoSdkValid and ManualSdkValid, with Valid a Combination of both
};

struct FTurnkeySdkInfo
{
	ETurnkeyPlatformSdkStatus Status = ETurnkeyPlatformSdkStatus::Unknown;
	FText SdkErrorInformation;
	FString InstalledVersion;
	FString AutoSDKVersion; // only valid for platform, not device
	FString MinAllowedVersion;
	FString MaxAllowedVersion;
};


/**
 * Interface for turnkey support module
 */
class ITurnkeySupportModule
	: public IModuleInterface
{
public:

	/**
	 * Runs Turnkey to get the Sdk information for all known platforms
	 */
	virtual TSharedRef<SWidget> MakeTurnkeyMenu() const = 0;

	/**
	 * @return	The newly-created menu widget
	 */
	virtual void UpdateSdkInfo() = 0;

	/**
	 * Runs Turnkey to get the Sdk information for a list of devices
	 */
	virtual void UpdateSdkInfoForDevices(TArray<FString> DeviceIds) = 0;


	virtual FTurnkeySdkInfo GetSdkInfo(FName PlatformName, bool bBlockIfQuerying = true) const = 0;
	virtual FTurnkeySdkInfo GetSdkInfoForDeviceId(const FString& DeviceId) const = 0;
	// @todo turnkey look into remove this
	virtual void ClearDeviceStatus(FName PlatformName=NAME_None) = 0;

public:

	/**
	 * Gets a reference to the search module instance.
	 *
	 * @todo gmp: better implementation using dependency injection.
	 * @return A reference to the MainFrame module.
	 */
	static ITurnkeySupportModule& Get( )
	{
		static const FName TurnkeySupportModuleName = "TurnkeySupport";
		return FModuleManager::LoadModuleChecked<ITurnkeySupportModule>(TurnkeySupportModuleName);
	}

public:

	/**
	 * Virtual destructor.
	 */
	virtual ~ITurnkeySupportModule( ) { }
};
