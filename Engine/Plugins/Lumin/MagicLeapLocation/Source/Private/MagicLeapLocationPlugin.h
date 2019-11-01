// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMagicLeapLocationPlugin.h"
#include "MagicLeapLocationTypes.h"
#include "AppEventHandler.h"
#include "MagicLeapPluginUtil.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapLocation, Verbose, All);

class FMagicLeapLocationPlugin : public IMagicLeapLocationPlugin
{
public:
	FMagicLeapLocationPlugin();

	bool GetLastCoarseLocation(FLocationData& OutLocation);
	bool GetLastCoarseLocationAsync(const FCoarseLocationResultDelegateMulti& ResultDelegate);
	bool GetLastCoarseLocationOnSphere(float InRadius, FVector& OutLocation);
	bool GetLastCoarseLocationOnSphereAsync(const FCoarseLocationOnSphereResultDelegateMulti& ResultDelegate, float InRadius);
private:
	MagicLeap::IAppEventHandler PrivilegesManager;
	float CachedRadius;
	FCoarseLocationResultDelegateMulti OnCoarseLocationResult;
	FCoarseLocationOnSphereResultDelegateMulti OnCoarseLocationOnSphereResult;

	void OnGetLocationPrivilegeResult(const MagicLeap::FRequiredPrivilege& RequiredPrivilege);
	void OnGetLocationOnSpherePrivilegeResult(const MagicLeap::FRequiredPrivilege& RequiredPrivilege);
};

#define GET_MAGIC_LEAP_LOCATION_PLUGIN() static_cast<FMagicLeapLocationPlugin*>(&IMagicLeapLocationPlugin::Get())
