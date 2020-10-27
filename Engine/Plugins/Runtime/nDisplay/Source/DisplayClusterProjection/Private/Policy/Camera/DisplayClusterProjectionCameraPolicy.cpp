// Copyright Epic Games, Inc. All Rights Reserved.

#include "Policy/Camera/DisplayClusterProjectionCameraPolicy.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "Game/IDisplayClusterGameManager.h"

#include "Misc/DisplayClusterHelpers.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "ComposurePostMoves.h"


FDisplayClusterProjectionCameraPolicy::FDisplayClusterProjectionCameraPolicy(const FString& ViewportId, const TMap<FString, FString>& Parameters)
	: FDisplayClusterProjectionPolicyBase(ViewportId, Parameters)
{
}

FDisplayClusterProjectionCameraPolicy::~FDisplayClusterProjectionCameraPolicy()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterProjectionCameraPolicy::StartScene(UWorld* InWorld)
{
	check(IsInGameThread());

	World = InWorld;
}

void FDisplayClusterProjectionCameraPolicy::EndScene()
{
	check(IsInGameThread());

	AssignedCamera = nullptr;
}

bool FDisplayClusterProjectionCameraPolicy::HandleAddViewport(const FIntPoint& InViewportSize, const uint32 InViewsAmount)
{
	check(IsInGameThread());

	UE_LOG(LogDisplayClusterProjectionCamera, Log, TEXT("Initializing internals for the viewport '%s'"), *GetViewportId());
	
	return true;
}

void FDisplayClusterProjectionCameraPolicy::HandleRemoveViewport()
{
	check(IsInGameThread());
	UE_LOG(LogDisplayClusterProjectionCamera, Log, TEXT("Removing viewport '%s'"), *GetViewportId());
}

bool FDisplayClusterProjectionCameraPolicy::CalculateView(const uint32 ViewIdx, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP)
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
	// Otherwise default UE4 camera is used
	else
	{
		if (World)
		{
			APlayerController* const CurPlayerController = World->GetFirstPlayerController();
			if (CurPlayerController)
			{
				APlayerCameraManager* const CurPlayerCameraManager = CurPlayerController->PlayerCameraManager;
				if (CurPlayerCameraManager)
				{
					InOutViewLocation = CurPlayerCameraManager->GetCameraLocation();
					InOutViewRotation = CurPlayerCameraManager->GetCameraRotation();
				}
			}
		}
	}

	return true;
}

bool FDisplayClusterProjectionCameraPolicy::GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix)
{
	check(IsInGameThread());

	FComposurePostMoveSettings ComposureSettings;

	if (AssignedCamera)
	{
		OutPrjMatrix = ComposureSettings.GetProjectionMatrix(AssignedCamera->FieldOfView * CurrentFovMultiplier, AssignedCamera->AspectRatio);
		return true;
	}
	else
	{
		if (World)
		{
			APlayerController* const CurPlayerController = World->GetFirstPlayerController();
			if (CurPlayerController)
			{
				APlayerCameraManager* const CurPlayerCameraManager = CurPlayerController->PlayerCameraManager;
				if (CurPlayerCameraManager)
				{
					OutPrjMatrix = ComposureSettings.GetProjectionMatrix(CurPlayerCameraManager->GetFOVAngle() * CurrentFovMultiplier, CurPlayerCameraManager->DefaultAspectRatio);
					return true;
				}
			}
		}
	}

	return false;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterProjectionCameraPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterProjectionCameraPolicy::SetCamera(UCameraComponent* NewCamera, float FOVMultiplier)
{
	check(NewCamera);
	check(FOVMultiplier >= 0.1f);

	if (NewCamera)
	{
		UE_LOG(LogDisplayClusterProjectionCamera, Verbose, TEXT("New camera set: %s"), *NewCamera->GetFullName());
		AssignedCamera = NewCamera;
	}
	else
	{
		UE_LOG(LogDisplayClusterProjectionCamera, Warning, TEXT("Trying to set nullptr camera pointer"));
	}

	if (FOVMultiplier >= 0.1f)
	{
		UE_LOG(LogDisplayClusterProjectionCamera, Verbose, TEXT("New FOV multiplier set: %f"), FOVMultiplier);
		CurrentFovMultiplier = FOVMultiplier;
	}
	else
	{
		UE_LOG(LogDisplayClusterProjectionCamera, Warning, TEXT("FOV multiplier is too small"));
	}
}
