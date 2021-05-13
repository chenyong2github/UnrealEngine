// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/Camera/DisplayClusterProjectionCameraPolicy.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"
#include "DisplayClusterRootActor.h"

#include "Game/IDisplayClusterGameManager.h"

#include "Misc/DisplayClusterHelpers.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "ComposurePostMoves.h"

#include "Render/Viewport/IDisplayClusterViewport.h"
#include "Render/Viewport/IDisplayClusterViewportManager.h"

FDisplayClusterProjectionCameraPolicy::FDisplayClusterProjectionCameraPolicy(const FString& ProjectionPolicyId, const struct FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
	: FDisplayClusterProjectionPolicyBase(ProjectionPolicyId, InConfigurationProjectionPolicy)
{
}

FDisplayClusterProjectionCameraPolicy::~FDisplayClusterProjectionCameraPolicy()
{
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterProjectionCameraPolicy::HandleStartScene(class IDisplayClusterViewport* InViewport)
{
	check(IsInGameThread());

	UCameraComponent* CfgCamera = nullptr;
	FDisplayClusterProjectionCameraPolicySettings CfgCameraSettings;

	if (!GetSettingsFromConfig(InViewport, CfgCamera, CfgCameraSettings))
	{
		UE_LOG(LogDisplayClusterProjectionCamera, Error, TEXT("Invalid camera settings for viewport: %s"), *InViewport->GetId());
		return false;
	}

	if (CfgCamera) 
	{
		SetCamera(CfgCamera, CfgCameraSettings);
	}

	return true;
}

void FDisplayClusterProjectionCameraPolicy::HandleEndScene(class IDisplayClusterViewport* InViewport)
{
	check(IsInGameThread());

	AssignedCamera = nullptr;
}


APlayerCameraManager* const GetCurPlayerCameraManager(IDisplayClusterViewport* InViewport)
{
	UWorld* World = InViewport->GetOwner().GetCurrentWorld();
	if (World)
	{
		APlayerController* const CurPlayerController = World->GetFirstPlayerController();
		if (CurPlayerController)
		{
			return CurPlayerController->PlayerCameraManager;
		}
	}

	return nullptr;
}

bool FDisplayClusterProjectionCameraPolicy::CalculateView(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP)
{
	check(IsInGameThread());

	InOutViewLocation = FVector::ZeroVector;
	InOutViewRotation = FRotator::ZeroRotator;

	// Use transform of an assigned camera
	if (AssignedCamera)
	{
		InOutViewLocation = AssignedCamera->GetComponentLocation();
		InOutViewRotation = AssignedCamera->GetComponentRotation();
	}
	// Otherwise default UE camera is used
	else
	{
		APlayerCameraManager* const CurPlayerCameraManager = GetCurPlayerCameraManager(InViewport);
				if (CurPlayerCameraManager)
				{
					InOutViewLocation = CurPlayerCameraManager->GetCameraLocation();
					InOutViewRotation = CurPlayerCameraManager->GetCameraRotation();
				}
			}

	// Fix camera lens deffects (prototype)
	InOutViewLocation += CameraSettings.FrustumOffset;
	InOutViewRotation += CameraSettings.FrustumRotation;

	return true;
}

bool FDisplayClusterProjectionCameraPolicy::GetProjectionMatrix(IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix)
{
	check(IsInGameThread());

	FComposurePostMoveSettings ComposureSettings;

	if (AssignedCamera)
	{
		OutPrjMatrix = ComposureSettings.GetProjectionMatrix(AssignedCamera->FieldOfView * CameraSettings.FOVMultiplier, AssignedCamera->AspectRatio);
		return true;
	}
	else
	{
		APlayerCameraManager* const CurPlayerCameraManager = GetCurPlayerCameraManager(InViewport);
		if (CurPlayerCameraManager)
		{
			OutPrjMatrix = ComposureSettings.GetProjectionMatrix(CurPlayerCameraManager->GetFOVAngle() * CameraSettings.FOVMultiplier, CurPlayerCameraManager->DefaultAspectRatio);
			return true;
		}
	}

	return false;
}


bool FDisplayClusterProjectionCameraPolicy::GetSettingsFromConfig(class IDisplayClusterViewport* InViewport, UCameraComponent*& OutCamera, FDisplayClusterProjectionCameraPolicySettings& OutCameraSettings)
{
	check(InViewport);
	
	ADisplayClusterRootActor* const RootActor = InViewport->GetOwner().GetRootActor();
	if (!RootActor)
	{
		UE_LOG(LogDisplayClusterProjectionCamera, Error, TEXT("Couldn't get a DisplayClusterRootActor root object"));
		return false;
	}

	FString CameraComponentId;
	// Get assigned camera ID
	if (!DisplayClusterHelpers::map::template ExtractValue(GetParameters(), DisplayClusterProjectionStrings::cfg::camera::Component, CameraComponentId))
	{

#if WITH_EDITOR
		if (CameraComponentId.IsEmpty())
		{
			return false;
		}
#endif

		UE_LOG(LogDisplayClusterProjectionCamera, Error, TEXT("No camera component ID '%s' specified for projection policy '%s'"), *CameraComponentId, *GetId());
		return false;
	}

	// Get camera component
	TArray<UCameraComponent*> CameraComps;
	RootActor->GetComponents<UCameraComponent>(CameraComps);
	for (UCameraComponent* Comp : CameraComps)
	{
		if (Comp->GetName() == CameraComponentId)
		{
			OutCamera = Comp;
			return true;
		}
	}

	UE_LOG(LogDisplayClusterProjectionCamera, Error, TEXT("Camera component ID '%s' not found for projection policy '%s'"), *CameraComponentId, *GetId());

	return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterProjectionCameraPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterProjectionCameraPolicy::SetCamera(UCameraComponent* NewCamera, const FDisplayClusterProjectionCameraPolicySettings& InCameraSettings)
{
	check(NewCamera);
	check(InCameraSettings.FOVMultiplier >= 0.1f);

	if (NewCamera)
	{
		UE_LOG(LogDisplayClusterProjectionCamera, Verbose, TEXT("New camera set: %s"), *NewCamera->GetFullName());
		AssignedCamera = NewCamera;
	}
	else
	{
		UE_LOG(LogDisplayClusterProjectionCamera, Warning, TEXT("Trying to set nullptr camera pointer"));
	}

	if (InCameraSettings.FOVMultiplier >= 0.1f)
	{
		UE_LOG(LogDisplayClusterProjectionCamera, Verbose, TEXT("New FOV multiplier set: %f"), InCameraSettings.FOVMultiplier);
		CameraSettings = InCameraSettings;
	}
	else
	{
		UE_LOG(LogDisplayClusterProjectionCamera, Warning, TEXT("FOV multiplier is too small"));
	}
}
