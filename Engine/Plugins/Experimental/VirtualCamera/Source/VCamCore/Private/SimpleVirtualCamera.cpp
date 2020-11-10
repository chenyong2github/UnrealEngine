// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimpleVirtualCamera.h"

ASimpleVirtualCamera::ASimpleVirtualCamera(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Don't run on CDO
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		VCamComponent = ObjectInitializer.CreateDefaultSubobject<UVCamComponent>(this, TEXT("VCamComponent"));
		VCamComponent->AttachToComponent(GetCineCameraComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		VCamComponent->RegisterComponent();
	}
}
