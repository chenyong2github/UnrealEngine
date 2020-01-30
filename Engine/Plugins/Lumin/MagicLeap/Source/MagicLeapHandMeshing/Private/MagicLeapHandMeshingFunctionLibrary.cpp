// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapHandMeshingFunctionLibrary.h"
#include "MagicLeapHandMeshingModule.h"

bool UMagicLeapHandMeshingFunctionLibrary::CreateClient()
{
	return GetMagicLeapHandMeshingModule().CreateClient();
}

bool UMagicLeapHandMeshingFunctionLibrary::DestroyClient()
{
	return GetMagicLeapHandMeshingModule().DestroyClient();
}

bool UMagicLeapHandMeshingFunctionLibrary::ConnectMRMesh(UMRMeshComponent* InMRMeshPtr)
{
	return GetMagicLeapHandMeshingModule().ConnectMRMesh(InMRMeshPtr);
}

bool UMagicLeapHandMeshingFunctionLibrary::DisconnectMRMesh(class UMRMeshComponent* InMRMeshPtr)
{
	return GetMagicLeapHandMeshingModule().DisconnectMRMesh(InMRMeshPtr);
}
