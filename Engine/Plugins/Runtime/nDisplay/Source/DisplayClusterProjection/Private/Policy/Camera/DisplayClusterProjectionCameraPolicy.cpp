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

#include "Components/DisplayClusterICVFX_CineCameraComponent.h"

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

	USceneComponent* CfgComponent = nullptr;
	FDisplayClusterProjectionCameraPolicySettings CfgCameraSettings;

	if (!GetSettingsFromConfig(InViewport, CfgComponent, CfgCameraSettings))
	{
		UE_LOG(LogDisplayClusterProjectionCamera, Error, TEXT("Invalid camera settings for viewport: %s"), *InViewport->GetId());
		return false;
	}

	if (CfgComponent)
	{
		UDisplayClusterICVFX_CineCameraComponent* CameraICVFX = Cast<UDisplayClusterICVFX_CineCameraComponent>(CfgComponent);
		if (CameraICVFX)
		{
			AssignedICVFXCameraRef.SetSceneComponent(CameraICVFX);
			UpdateICVFXCamera(CameraICVFX);
		}
		else
		{
			AssignedICVFXCameraRef.ResetSceneComponent();

			UCameraComponent* Camera = StaticCast<UCameraComponent*>(CfgComponent);
			SetCamera(Camera, CfgCameraSettings);
		}
	}

	return true;
}

void FDisplayClusterProjectionCameraPolicy::HandleEndScene(class IDisplayClusterViewport* InViewport)
{
	check(IsInGameThread());

	AssignedCameraRef.ResetSceneComponent();
	AssignedICVFXCameraRef.ResetSceneComponent();
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

bool FDisplayClusterProjectionCameraPolicy::UpdateICVFXCamera(UDisplayClusterICVFX_CineCameraComponent* InComponentICVFX)
{
	if (InComponentICVFX)
	{
		FDisplayClusterProjectionCameraPolicySettings PolicyCameraSettings;
		{
			const FDisplayClusterConfigurationICVFX_CameraSettings& CameraSettingsICVFX = InComponentICVFX->GetCameraSettingsICVFX();
			PolicyCameraSettings.FOVMultiplier = CameraSettingsICVFX.FieldOfViewMultiplier;

			// Lens correction
			PolicyCameraSettings.FrustumRotation = CameraSettingsICVFX.FrustumRotation;
			PolicyCameraSettings.FrustumOffset   = CameraSettingsICVFX.FrustumOffset;
		}

		// Update referenced component and settings
		SetCamera(InComponentICVFX->GetCameraComponent(), PolicyCameraSettings);

		return true;
	}

	return false;
}

UCameraComponent* FDisplayClusterProjectionCameraPolicy::GetCameraComponent()
{
	// Support runtime update for ICVFX camera component
	UDisplayClusterICVFX_CineCameraComponent* CameraICVFX = ImplGetAssignedICVFXComponent();
	if (CameraICVFX)
	{
		UpdateICVFXCamera(CameraICVFX);
	}

	return ImplGetAssignedCamera();
}

UDisplayClusterICVFX_CineCameraComponent* FDisplayClusterProjectionCameraPolicy::ImplGetAssignedICVFXComponent()
{
	if (AssignedICVFXCameraRef.IsDefinedSceneComponent())
	{
		USceneComponent* SceneComponent = AssignedICVFXCameraRef.GetOrFindSceneComponent();
		if (SceneComponent)
		{
			UDisplayClusterICVFX_CineCameraComponent* CameraComponentICVFX = Cast<UDisplayClusterICVFX_CineCameraComponent>(SceneComponent);
			if (CameraComponentICVFX)
			{
				return CameraComponentICVFX;
			}
		}

		//@todo: handle error: reference to deleted ICVFX component
	}

	return nullptr;
}

UCameraComponent* FDisplayClusterProjectionCameraPolicy::ImplGetAssignedCamera()
{
	if (AssignedCameraRef.IsDefinedSceneComponent())
	{
		USceneComponent* SceneComponent = AssignedCameraRef.GetOrFindSceneComponent();
		if (SceneComponent)
		{
			UCameraComponent* CameraComponent = Cast<UCameraComponent>(SceneComponent);
			if (CameraComponent)
			{
				return CameraComponent;
			}
		}

		//@todo: handle error: reference to deleted component
	}

	return nullptr;
}

bool FDisplayClusterProjectionCameraPolicy::CalculateView(class IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP)
{
	check(IsInGameThread());

	InOutViewLocation = FVector::ZeroVector;
	InOutViewRotation = FRotator::ZeroRotator;
	
	// Use transform of an assigned camera
	if (UCameraComponent* AssignedCameraComponent = GetCameraComponent())
	{
		InOutViewLocation = AssignedCameraComponent->GetComponentLocation();
		InOutViewRotation = AssignedCameraComponent->GetComponentRotation();
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

	if (UCameraComponent* AssignedCameraComponent = GetCameraComponent())
	{
		OutPrjMatrix = ComposureSettings.GetProjectionMatrix(AssignedCameraComponent->FieldOfView * CameraSettings.FOVMultiplier, AssignedCameraComponent->AspectRatio);
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


bool FDisplayClusterProjectionCameraPolicy::GetSettingsFromConfig(class IDisplayClusterViewport* InViewport, USceneComponent*& OutSceneComponent, FDisplayClusterProjectionCameraPolicySettings& OutCameraSettings)
{
	check(InViewport);

	FString CameraComponentId;
	// Get assigned camera ID
	if (!DisplayClusterHelpers::map::template ExtractValue(GetParameters(), DisplayClusterProjectionStrings::cfg::camera::Component, CameraComponentId))
	{
		// use default cameras
		return true;
	}

	ADisplayClusterRootActor* const RootActor = InViewport->GetOwner().GetRootActor();
	if (!RootActor)
	{
		UE_LOG(LogDisplayClusterProjectionCamera, Error, TEXT("Couldn't get a DisplayClusterRootActor root object"));
		return false;
	}

	// Get camera component
	TArray<UCameraComponent*> CameraComps;
	RootActor->GetComponents<UCameraComponent>(CameraComps);
	for (UCameraComponent* Comp : CameraComps)
	{
		if (Comp->GetName() == CameraComponentId)
		{
			OutSceneComponent = Comp;
			return true;
		}
	}

	// Get ICVFX camera component
	TArray<UDisplayClusterICVFX_CineCameraComponent*> ICVFXCameraComps;
	RootActor->GetComponents<UDisplayClusterICVFX_CineCameraComponent>(ICVFXCameraComps);
	for (UCameraComponent* Comp : ICVFXCameraComps)
	{
		if (Comp->GetName() == CameraComponentId)
		{
			OutSceneComponent = Comp;
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
		AssignedCameraRef.SetSceneComponent(NewCamera);
	}
	else
	{
		AssignedCameraRef.ResetSceneComponent();
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
