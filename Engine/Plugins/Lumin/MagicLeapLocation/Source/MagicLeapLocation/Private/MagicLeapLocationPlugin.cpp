// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapLocationPlugin.h"
#include "IMagicLeapPlugin.h"
#include "Lumin/CAPIShims/LuminAPILocation.h"

DEFINE_LOG_CATEGORY(LogMagicLeapLocation);

using namespace MagicLeap;

FMagicLeapLocationPlugin::FMagicLeapLocationPlugin()
: PrivilegesManager({ EMagicLeapPrivilege::CoarseLocation, EMagicLeapPrivilege::FineLocation })
, CachedRadius(0.0f)
{
}

bool FMagicLeapLocationPlugin::GetLastLocation(FMagicLeapLocationData& OutLocation, bool bRequestAccuracy)
{
#if WITH_MLSDK
	MLLocationData LocationData;
	MLLocationDataInit(&LocationData);

	if (bRequestAccuracy)
	{
		if (PrivilegesManager.GetPrivilegeStatus(EMagicLeapPrivilege::FineLocation, false) == MagicLeap::EPrivilegeState::Granted)
		{
			MLResult Result = MLLocationGetLastFineLocation(&LocationData);
			if (Result != MLResult_Ok)
			{
				UE_LOG(LogMagicLeapLocation, Error, TEXT("MLLocationGetLastFineLocation failed with error '%s'"), UTF8_TO_TCHAR(MLLocationGetResultString(Result)));
				return false;
			}
			else
			{
				OutLocation.Latitude = LocationData.latitude;
				OutLocation.Longitude = LocationData.longitude;
				if (LocationData.location_mask & MLLocationDataMask_HasPostalCode)
				{
					OutLocation.PostalCode = UTF8_TO_TCHAR(LocationData.postal_code);
				}
				if (LocationData.location_mask & MLLocationDataMask_HasAccuracy)
				{
					OutLocation.Accuracy = LocationData.accuracy * IMagicLeapPlugin::Get().GetWorldToMetersScale();
				}
				return true;
			}
		}
	}
	else if (PrivilegesManager.GetPrivilegeStatus(EMagicLeapPrivilege::CoarseLocation, false) == MagicLeap::EPrivilegeState::Granted)
	{
		MLResult Result = MLLocationGetLastCoarseLocation(&LocationData);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapLocation, Error, TEXT("MLLocationGetLastCoarseLocation failed with error '%s'"), UTF8_TO_TCHAR(MLLocationGetResultString(Result)));
			return false;
		}
		else
		{
			OutLocation.Latitude = LocationData.latitude;
			OutLocation.Longitude = LocationData.longitude;
			if (LocationData.location_mask & MLLocationDataMask_HasPostalCode)
			{
				OutLocation.PostalCode = UTF8_TO_TCHAR(LocationData.postal_code);
			}
			return true;
		}
	}
#endif // WITH_MLSDK

	return false;
}

bool FMagicLeapLocationPlugin::GetLastLocationAsync(const FMagicLeapLocationResultDelegateMulti& ResultDelegate, bool bRequestAccuracy)
{
	FMagicLeapLocationData LocationData;
	if (GetLastLocation(LocationData, bRequestAccuracy))
	{
		ResultDelegate.Broadcast(LocationData, true);
		return true;
	}
	else
	{
		OnLocationResult = ResultDelegate;
		EMagicLeapPrivilege PrivilegeID = bRequestAccuracy ? EMagicLeapPrivilege::FineLocation : EMagicLeapPrivilege::CoarseLocation;
		PrivilegesManager.AddPrivilegeEventHandler(PrivilegeID, [this](const FRequiredPrivilege& RequiredPrivilege)
		{
			OnGetLocationPrivilegeResult(RequiredPrivilege);
		});
	}

	return false;
}

bool FMagicLeapLocationPlugin::GetLastLocationOnSphere(float InRadius, FVector& OutLocation, bool bRequestAccuracy)
{
	FMagicLeapLocationData LocationData;
	if (!GetLastLocation(LocationData, bRequestAccuracy))
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

bool FMagicLeapLocationPlugin::GetLastLocationOnSphereAsync(const FMagicLeapLocationOnSphereResultDelegateMulti& ResultDelegate, float InRadius, bool bRequestAccuracy)
{
	EMagicLeapPrivilege PrivilegeID = bRequestAccuracy ? EMagicLeapPrivilege::FineLocation : EMagicLeapPrivilege::CoarseLocation;
	if (PrivilegesManager.GetPrivilegeStatus(PrivilegeID, false) == MagicLeap::EPrivilegeState::Granted)
	{
		FVector LocationOnSphere;
		if (GetLastLocationOnSphere(InRadius, LocationOnSphere, bRequestAccuracy))
		{
			ResultDelegate.Broadcast(LocationOnSphere, true);
		}
	}
	else
	{
		OnLocationOnSphereResult = ResultDelegate;
		CachedRadius = InRadius;
		PrivilegesManager.AddPrivilegeEventHandler(PrivilegeID, [this](const FRequiredPrivilege& RequiredPrivilege)
		{
			OnGetLocationOnSpherePrivilegeResult(RequiredPrivilege);
		});
	}

	return true;
}

void FMagicLeapLocationPlugin::OnGetLocationPrivilegeResult(const FRequiredPrivilege& RequiredPrivilege)
{
	FMagicLeapLocationData LocationData;
	if (RequiredPrivilege.State == EPrivilegeState::Granted)
	{
		bool bSuccess = GetLastLocation(LocationData, RequiredPrivilege.PrivilegeID == EMagicLeapPrivilege::FineLocation);
		OnLocationResult.Broadcast(LocationData, bSuccess);
	}
	else
	{
		OnLocationResult.Broadcast(LocationData, false);
	}
}

void FMagicLeapLocationPlugin::OnGetLocationOnSpherePrivilegeResult(const FRequiredPrivilege& RequiredPrivilege)
{
	FVector LocationOnSphere;
	if (RequiredPrivilege.State == EPrivilegeState::Granted)
	{
		bool bSuccess = GetLastLocationOnSphere(CachedRadius, LocationOnSphere, RequiredPrivilege.PrivilegeID == EMagicLeapPrivilege::FineLocation);
		OnLocationOnSphereResult.Broadcast(LocationOnSphere, bSuccess);
	}
	else
	{
		OnLocationOnSphereResult.Broadcast(LocationOnSphere, false);
	}
}

IMPLEMENT_MODULE(FMagicLeapLocationPlugin, MagicLeapLocation);
