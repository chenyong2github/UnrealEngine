// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/CameraShakeSourceActor.h"
#include "Camera/CameraShakeSourceComponent.h"


ACameraShakeSourceActor::ACameraShakeSourceActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CameraShakeSourceComponent = ObjectInitializer.CreateDefaultSubobject<UCameraShakeSourceComponent>(this, TEXT("CameraShakeSourceComponent"));
	RootComponent = CameraShakeSourceComponent;
}
