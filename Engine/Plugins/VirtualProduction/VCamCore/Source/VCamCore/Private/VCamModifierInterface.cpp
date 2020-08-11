// Copyright Epic Games, Inc. All Rights Reserved.


#include "VCamModifierInterface.h"
#include "VCamComponent.h"
#include "VCamCoreSubsystem.h"

// Default fallback implementation
void IVCamModifierInterface::OnVCamComponentChanged_Implementation(UVCamComponent* VCam)
{
	UE_LOG(LogVCamCore, Warning, TEXT("The default function for On VCam Component Changed has been called. New component is: %s"), *VCam->GetName());
}

