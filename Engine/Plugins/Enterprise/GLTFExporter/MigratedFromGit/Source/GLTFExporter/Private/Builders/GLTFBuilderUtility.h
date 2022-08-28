// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine.h"

struct FGLTFBuilderUtility
{
	static FString GetMeshName(const UStaticMesh* StaticMesh, int32 LODIndex)
	{
		FString Name;
		StaticMesh->GetName(Name);
		if (LODIndex != 0) Name += TEXT("_LOD") + FString::FromInt(LODIndex);
		return Name;
	}
};
