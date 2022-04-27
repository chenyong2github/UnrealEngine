// Copyright Epic Games, Inc. All Rights Reserved.

#include "IXRTrackingSystem.h"
#include "IHeadMountedDisplay.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

void IXRTrackingSystem::GetHMDData(UObject* WorldContext, FXRHMDData& HMDData)
{
	HMDData.bValid = true;
	HMDData.DeviceName = GetHMDDevice() ? GetHMDDevice()->GetHMDName() : GetSystemName();
	HMDData.ApplicationInstanceID = FApp::GetInstanceId();

	bool bIsTracking = IsTracking(IXRTrackingSystem::HMDDeviceId);
	HMDData.TrackingStatus = bIsTracking ? ETrackingStatus::Tracked : ETrackingStatus::NotTracked;

	APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(WorldContext, 0);
	if (CameraManager)
	{
		HMDData.Rotation = CameraManager->GetCameraRotation().Quaternion();
		HMDData.Position = CameraManager->GetCameraLocation();
	}
	//GetCurrentPose(0, HMDVisualizationData.Rotation, HMDVisualizationData.Position);
}

bool IXRTrackingSystem::IsHeadTrackingAllowedForWorld(UWorld& World) const
{
#if WITH_EDITOR
	// For VR PIE only the first instance uses the headset
	// This implementation is constrained by hotfix rules.  It would be better to cache this somewhere.

	if (!IsHeadTrackingAllowed())
	{
		return false;
	}

	if (World.WorldType != EWorldType::PIE)
	{
		return true;
	}

	// If we are a pie instance then the first pie world that is not a dedicated server uses head tracking
	const int32 MyPIEInstanceID = World.GetOutermost()->GetPIEInstanceID();
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.WorldType == EWorldType::PIE && WorldContext.RunAsDedicated == false && WorldContext.World())
		{
			return WorldContext.World()->GetOutermost()->GetPIEInstanceID() == MyPIEInstanceID;
		}
	}
	return false;
#else
	return IsHeadTrackingAllowed();
#endif
}
