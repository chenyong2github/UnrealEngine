// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapLocationPlugin.h"

#include "Lumin/CAPIShims/LuminAPILocation.h"

DEFINE_LOG_CATEGORY(LogMagicLeapLocation);

using namespace MagicLeap;

FMagicLeapLocationPlugin::FMagicLeapLocationPlugin()
: PrivilegesManager({ EMagicLeapPrivilege::CoarseLocation })
, CachedRadius(0.0f)
{
}

bool FMagicLeapLocationPlugin::GetLastCoarseLocation(FLocationData& OutLocation)
{
#if WITH_MLSDK
	if (PrivilegesManager.GetPrivilegeStatus(EMagicLeapPrivilege::CoarseLocation, false) == MagicLeap::EPrivilegeState::Granted)
	{
		MLLocationData LocationData;
		MLLocationDataInit(&LocationData);
		MLResult Result = MLLocationGetLastCoarseLocation(&LocationData);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapLocation, Error, TEXT("MLLocationGetLastCoarseLocation failed with error '%s'"), UTF8_TO_TCHAR(MLGetResultString(Result)));
			return false;
		}
		else
		{
			OutLocation.Latitude = LocationData.latitude;
			OutLocation.Longitude = LocationData.longitude;
			if (LocationData.location_mask & MLLocationDataMask_HasPostalCode)
			{
				OutLocation.PostalCode = LocationData.postal_code;
			}
			return true;
		}
	}
#endif // WITH_MLSDK

	return false;
}

bool FMagicLeapLocationPlugin::GetLastCoarseLocationAsync(const FCoarseLocationResultDelegateMulti& ResultDelegate)
{
	FLocationData LocationData;
	if (GetLastCoarseLocation(LocationData))
	{
		ResultDelegate.Broadcast(LocationData, true);
		return true;
	}
	else
	{
		OnCoarseLocationResult = ResultDelegate;
		PrivilegesManager.SetPrivilegeEventHandler([this](const FRequiredPrivilege& RequiredPrivilege)
		{
			OnGetLocationPrivilegeResult(RequiredPrivilege);
		});
	}

	return false;
}

bool FMagicLeapLocationPlugin::GetLastCoarseLocationOnSphere(float InRadius, FVector& OutLocation)
{
	FLocationData LocationData;
	if (!GetLastCoarseLocation(LocationData))
	{
		return false;
	}

	FRotator Rotator;
	Rotator.Pitch = LocationData.Latitude;
	Rotator.Yaw = -LocationData.Longitude;
	Rotator.Roll = 0.0f;
	OutLocation = FVector(1.0f, 0.0f, 0.0f);
	OutLocation = Rotator.RotateVector(OutLocation) * InRadius;
	return true;
}

bool FMagicLeapLocationPlugin::GetLastCoarseLocationOnSphereAsync(const FCoarseLocationOnSphereResultDelegateMulti& ResultDelegate, float InRadius)
{
	if (PrivilegesManager.GetPrivilegeStatus(EMagicLeapPrivilege::CoarseLocation, false) == MagicLeap::EPrivilegeState::Granted)
	{
		FVector LocationOnSphere;
		if (GetLastCoarseLocationOnSphere(InRadius, LocationOnSphere))
		{
			ResultDelegate.Broadcast(LocationOnSphere, true);
		}
	}
	else
	{
		OnCoarseLocationOnSphereResult = ResultDelegate;
		CachedRadius = InRadius;
		PrivilegesManager.SetPrivilegeEventHandler([this](const FRequiredPrivilege& RequiredPrivilege)
		{
			OnGetLocationOnSpherePrivilegeResult(RequiredPrivilege);
		});
	}

	return true;
}

void FMagicLeapLocationPlugin::OnGetLocationPrivilegeResult(const FRequiredPrivilege& RequiredPrivilege)
{
	FLocationData LocationData;
	if (RequiredPrivilege.State == EPrivilegeState::Granted)
	{
		bool bSuccess = GetLastCoarseLocation(LocationData);
		OnCoarseLocationResult.Broadcast(LocationData, bSuccess);
	}
	else
	{
		OnCoarseLocationResult.Broadcast(LocationData, false);
	}
}

void FMagicLeapLocationPlugin::OnGetLocationOnSpherePrivilegeResult(const FRequiredPrivilege& RequiredPrivilege)
{
	FVector LocationOnSphere;
	if (RequiredPrivilege.State == EPrivilegeState::Granted)
	{
		bool bSuccess = GetLastCoarseLocationOnSphere(CachedRadius, LocationOnSphere);
		OnCoarseLocationOnSphereResult.Broadcast(LocationOnSphere, bSuccess);
	}
	else
	{
		OnCoarseLocationOnSphereResult.Broadcast(LocationOnSphere, false);
	}
}

IMPLEMENT_MODULE(FMagicLeapLocationPlugin, MagicLeapLocation);
