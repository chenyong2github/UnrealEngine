// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FBIKDebugOption.generated.h"

USTRUCT()
struct FFBIKDebugOption
{
	GENERATED_BODY()
	
	UPROPERTY(meta = (Input))
	bool bDrawDebugHierarchy = false;

	// use red channel
	UPROPERTY(meta = (Input))
	bool bColorAngularMotionStrength = false;

	// use green channel
	UPROPERTY(meta = (Input))
	bool bColorLinearMotionStrength = false;

	UPROPERTY(meta = (Input))
	bool bDrawDebugAxes = false;

	UPROPERTY(meta = (Input))
	bool bDrawDebugEffector = false;

	UPROPERTY(meta = (Input))
	bool bDrawDebugConstraints = false;

	UPROPERTY(meta = (Input))
	FTransform DrawWorldOffset;

	UPROPERTY(meta = (Input))
	float DrawSize = 5.f;

	FFBIKDebugOption()
	{
		DrawWorldOffset.SetLocation(FVector(30.f, 0.f, 0.f));
	}
};