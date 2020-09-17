// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMagicLeapLocationPlugin.h"
#include "MagicLeapLocationTypes.h"
#include "AppEventHandler.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapLocation, Verbose, All);

class FMagicLeapLocationPlugin : public IMagicLeapLocationPlugin
{
public:
	FMagicLeapLocationPlugin();

	bool GetLastLocation(FMagicLeapLocationData& OutLocation, bool bRequestAccuracy = true);
	bool GetLastLocationAsync(const FMagicLeapLocationResultDelegateMulti& ResultDelegate, bool bRequestAccuracy = true);
	bool GetLastLocationOnSphere(float InRadius, FVector& OutLocation, bool bRequestAccuracy = true);
	bool GetLastLocationOnSphereAsync(const FMagicLeapLocationOnSphereResultDelegateMulti& ResultDelegate, float InRadius, bool bRequestAccuracy = true);
private:
	MagicLeap::IAppEventHandler PrivilegesManager;
	float CachedRadius;
	FMagicLeapLocationResultDelegateMulti OnLocationResult;
	FMagicLeapLocationOnSphereResultDelegateMulti OnLocationOnSphereResult;

	void OnGetLocationPrivilegeResult(const MagicLeap::FRequiredPrivilege& RequiredPrivilege);
	void OnGetLocationOnSpherePrivilegeResult(const MagicLeap::FRequiredPrivilege& RequiredPrivilege);
};

inline FMagicLeapLocationPlugin& GetMagicLeapLocationPlugin()
{
	return FModuleManager::Get().GetModuleChecked<FMagicLeapLocationPlugin>("MagicLeapLocation");
}
