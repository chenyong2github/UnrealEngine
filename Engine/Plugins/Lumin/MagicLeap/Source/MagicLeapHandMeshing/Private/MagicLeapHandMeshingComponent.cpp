// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapHandMeshingComponent.h"
#include "MagicLeapHandMeshingModule.h"

void UMagicLeapHandMeshingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetMagicLeapHandMeshingModule().DestroyClient();
	Super::EndPlay(EndPlayReason);
}

bool UMagicLeapHandMeshingComponent::ConnectMRMesh(UMRMeshComponent* InMRMeshPtr)
{
	FMagicLeapHandMeshingModule& Module = GetMagicLeapHandMeshingModule();
	if (!Module.HasClient())
	{
		Module.CreateClient();
	}

	return Module.ConnectMRMesh(InMRMeshPtr);
}

bool UMagicLeapHandMeshingComponent::DisconnectMRMesh(class UMRMeshComponent* InMRMeshPtr)
{
	return GetMagicLeapHandMeshingModule().DisconnectMRMesh(InMRMeshPtr);
}
