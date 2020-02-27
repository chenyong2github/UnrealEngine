// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapLocationServicesImpl.h"
#include "Lumin/CAPIShims/LuminAPILocation.h"

DEFINE_LOG_CATEGORY(LogMagicLeapLocationServicesImpl);

using namespace MagicLeap;

UMagicLeapLocationServicesImpl::UMagicLeapLocationServicesImpl()
: PrivilegesManager({ EMagicLeapPrivilege::CoarseLocation, EMagicLeapPrivilege::FineLocation })
, bUseFineLocation(false)
{
}

bool UMagicLeapLocationServicesImpl::InitLocationServices(ELocationAccuracy Accuracy, float UpdateFrequency, float MinDistanceFilter)
{
	switch (Accuracy)
	{
	case ELocationAccuracy::LA_ThreeKilometers:
	case ELocationAccuracy::LA_OneKilometer:
	{
		bUseFineLocation = false;
		PrivilegesManager.GetPrivilegeStatus(EMagicLeapPrivilege::CoarseLocation, false);
	}
	break;
	case ELocationAccuracy::LA_HundredMeters:
	case ELocationAccuracy::LA_TenMeters:
	case ELocationAccuracy::LA_Best:
	case ELocationAccuracy::LA_Navigation:
	{
		bUseFineLocation = true;
		PrivilegesManager.GetPrivilegeStatus(EMagicLeapPrivilege::FineLocation, false);
	}
	break;
	}

	return true;
}

FLocationServicesData  UMagicLeapLocationServicesImpl::GetLastKnownLocation()
{
	FLocationServicesData LocData;
#if WITH_MLSDK
	MLLocationData LocationData;
	MLLocationDataInit(&LocationData);

	if (bUseFineLocation)
	{
		if (PrivilegesManager.GetPrivilegeStatus(EMagicLeapPrivilege::FineLocation, false) == MagicLeap::EPrivilegeState::Granted)
		{
			MLResult Result = MLLocationGetLastFineLocation(&LocationData);
			if (Result != MLResult_Ok)
			{
				UE_LOG(LogMagicLeapLocationServicesImpl, Error, TEXT("MLLocationGetLastFineLocation failed with error '%s'"), UTF8_TO_TCHAR(MLLocationGetResultString(Result)));
			}
			else
			{
				 
				LocData.Latitude = LocationData.latitude;
				LocData.Longitude = LocationData.longitude;
			}
		}
	}
	else if (PrivilegesManager.GetPrivilegeStatus(EMagicLeapPrivilege::CoarseLocation, false) == MagicLeap::EPrivilegeState::Granted)
	{
		MLResult Result = MLLocationGetLastCoarseLocation(&LocationData);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogMagicLeapLocationServicesImpl, Error, TEXT("MLLocationGetLastCoarseLocation failed with error '%s'"), UTF8_TO_TCHAR(MLLocationGetResultString(Result)));
		}
		else
		{
			LocData.Latitude = LocationData.latitude;
			LocData.Longitude = LocationData.longitude;
		}
	}
#endif // WITH_MLSDK
	return LocData;
}

bool UMagicLeapLocationServicesImpl::IsLocationAccuracyAvailable(ELocationAccuracy Accuracy)
{
	bool bIsAvailable = false;

	switch (Accuracy)
	{
	case ELocationAccuracy::LA_ThreeKilometers:
	case ELocationAccuracy::LA_OneKilometer:
	{
		bIsAvailable = !bUseFineLocation;
	}
	break;
	case ELocationAccuracy::LA_HundredMeters:
	case ELocationAccuracy::LA_TenMeters:
	case ELocationAccuracy::LA_Best:
	case ELocationAccuracy::LA_Navigation:
	{
		bIsAvailable = bUseFineLocation;
	}
	break;
	}

	return bIsAvailable;
}

bool UMagicLeapLocationServicesImpl::IsLocationServiceEnabled()
{
	return PrivilegesManager.GetPrivilegeStatus(EMagicLeapPrivilege::FineLocation, false) ||
		PrivilegesManager.GetPrivilegeStatus(EMagicLeapPrivilege::CoarseLocation, false);
}
