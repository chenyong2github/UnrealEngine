// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGSurfaceData.h"

void UPCGSurfaceData::CopyBaseSurfaceData(UPCGSurfaceData* NewSurfaceData) const
{
	NewSurfaceData->Transform = Transform;
}