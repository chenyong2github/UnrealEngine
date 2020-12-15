// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/ITurnkeySupportModule.h"


/**
 * Editor main frame module
 */
class FTurnkeySupportModule	: public ITurnkeySupportModule
{
public:

	virtual void MakeTurnkeyMenu(struct FToolMenuSection& MenuSection) const override;
	virtual void MakeQuickLaunchItems(class UToolMenu* Menu, FOnQuickLaunchSelected ExternalOnClickDelegate) const override;
	virtual void RepeatQuickLaunch(FString DeviceId) override;



	virtual void UpdateSdkInfo() override;
	virtual void UpdateSdkInfoForDevices(TArray<FString> DeviceIds) override;

	virtual FTurnkeySdkInfo GetSdkInfo(FName PlatformName, bool bBlockIfQuerying = true) const override;
	virtual FTurnkeySdkInfo GetSdkInfoForDeviceId(const FString& DeviceId) const override;
	virtual void ClearDeviceStatus(FName PlatformName=NAME_None) override;
public:

	// IModuleInterface interface

	virtual void StartupModule( ) override;
	virtual void ShutdownModule( ) override;

	virtual bool SupportsDynamicReloading( ) override
	{
		return true; // @todo: Eventually, this should probably not be allowed.
	}


private:
	// Information about the validity of using a platform, discovered via Turnkey
	TMap<FName, FTurnkeySdkInfo> PerPlatformSdkInfo;

	// Information about the validity of each connected device (by string, discovered by Turnkey)
	TMap<FString, FTurnkeySdkInfo> PerDeviceSdkInfo;


	// menu helpers
	TSharedRef<SWidget> MakeTurnkeyMenuWidget() const;
};
