// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Policy/Camera/DisplayClusterProjectionCameraPolicy.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "DisplayClusterUtils/DisplayClusterCommonHelpers.h"

#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"

#include "DisplayClusterCameraComponent.h"
#include "DisplayClusterRootComponent.h"
#include "DisplayClusterScreenComponent.h"

#include "Camera/CameraComponent.h"
#include "ComposurePostMoves.h"


FDisplayClusterProjectionCameraPolicy::FDisplayClusterProjectionCameraPolicy(const FString& ViewportId)
	: FDisplayClusterProjectionPolicyBase(ViewportId)
{
}

FDisplayClusterProjectionCameraPolicy::~FDisplayClusterProjectionCameraPolicy()
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProjectionPolicy
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterProjectionCameraPolicy::StartScene(UWorld* World)
{
	check(IsInGameThread());
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

	InOutViewLocation = (AssignedCamera ? AssignedCamera->GetComponentLocation() : FVector::ZeroVector);
	InOutViewRotation = (AssignedCamera ? AssignedCamera->GetComponentRotation() : FRotator::ZeroRotator);

	return true;
}

bool FDisplayClusterProjectionCameraPolicy::GetProjectionMatrix(const uint32 ViewIdx, FMatrix& OutPrjMatrix)
{
	check(IsInGameThread());

	if (!AssignedCamera)
	{
		return false;
	}

	FComposurePostMoveSettings ComposureSettings;
	OutPrjMatrix = ComposureSettings.GetProjectionMatrix(AssignedCamera->FieldOfView * CurrentFovMultiplier, AssignedCamera->AspectRatio);

	return true;
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
