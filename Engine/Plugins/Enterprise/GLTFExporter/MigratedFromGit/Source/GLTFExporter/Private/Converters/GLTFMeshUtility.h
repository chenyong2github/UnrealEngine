// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine.h"
#include "GLTFMaterialArray.h"

struct FGLTFMeshUtility
{
	static int32 GetMinimumLOD(const UStaticMesh* StaticMesh);

	static int32 GetMinimumLOD(const USkeletalMesh* SkeletalMesh);

private:

	template <typename ValueType, typename StructType>
	static ValueType GetValueForRunningPlatform(const StructType& Properties);
};
