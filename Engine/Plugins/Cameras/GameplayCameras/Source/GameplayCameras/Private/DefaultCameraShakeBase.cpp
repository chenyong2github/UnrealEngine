// Copyright Epic Games, Inc. All Rights Reserved.

#include "DefaultCameraShakeBase.h"
#include "PerlinNoiseCameraShakePattern.h"

UDefaultCameraShakeBase::UDefaultCameraShakeBase(const FObjectInitializer& ObjInit)
	: Super(ObjInit
				.SetDefaultSubobjectClass<UPerlinNoiseCameraShakePattern>(TEXT("RootShakePattern")))
{
}

