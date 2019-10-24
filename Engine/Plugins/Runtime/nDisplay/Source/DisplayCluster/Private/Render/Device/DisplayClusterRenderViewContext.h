// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


struct FDisplayClusterRenderViewContext
{
	// Camera location and orientation
	FVector  ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;

	// World scale
	float WorldToMeters = 100.f;

	// Location and size on a render target
	FIntRect RenderTargetRect;
};

