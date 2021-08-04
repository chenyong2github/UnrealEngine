// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if __UNREAL__
#include "CoreMinimal.h"
#endif

struct FTextureShareAdditionalData
{
public:
	// Frame info
	uint32 FrameNumber;

	// Projection matrix
	FMatrix PrjMatrix;

	// View info
	FMatrix  ViewMatrix;
	FVector  ViewLocation;
	FRotator ViewRotation;
	FVector  ViewScale;

	//@todo: add more frame data
};

// Custom render
struct FTextureShareCustomProjectionData
{
	// Projection matrix
	FMatrix PrjMatrix;

	FVector  ViewLocation;
	FRotator ViewRotation;
	FVector  ViewScale;
};
