// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGDebug.h"

FPCGDebugVisualizationSettings::FPCGDebugVisualizationSettings()
{
	PointMesh = FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Material = FSoftObjectPath(TEXT("Material'/Game/PCG/DebugMaterial.DebugMaterial'"));
}