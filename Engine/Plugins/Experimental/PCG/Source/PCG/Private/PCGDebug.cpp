// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGDebug.h"
#include "Materials/MaterialInterface.h"

namespace PCGDebugVisConstants
{
	const FSoftObjectPath DefaultPointMesh = FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"));
	const FSoftObjectPath MaterialForDefaultPointMesh = FSoftObjectPath(TEXT("Material'/Game/PCG/DebugMaterial.DebugMaterial'"));
}

FPCGDebugVisualizationSettings::FPCGDebugVisualizationSettings()
{
	PointMesh = PCGDebugVisConstants::DefaultPointMesh;
}

TSoftObjectPtr<UMaterialInterface> FPCGDebugVisualizationSettings::GetMaterial() const
{
	if (!MaterialOverride.IsValid() && PointMesh.ToSoftObjectPath() == PCGDebugVisConstants::DefaultPointMesh)
	{
		return TSoftObjectPtr<UMaterialInterface>(PCGDebugVisConstants::MaterialForDefaultPointMesh);
	}
	else
	{
		return MaterialOverride;
	}
}