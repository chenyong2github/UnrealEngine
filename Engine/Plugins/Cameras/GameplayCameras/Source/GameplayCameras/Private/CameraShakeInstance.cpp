// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraShakeInstance.h"
#include "PerlinNoiseCameraShakePattern.h"

UCameraShakeInstance::UCameraShakeInstance(const FObjectInitializer& ObjInit)
	: Super(ObjInit
				.SetDefaultSubobjectClass<UPerlinNoiseCameraShakePattern>(TEXT("RootShakePattern")))
{
}

