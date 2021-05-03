// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "BoneIndices.h"
#include "MeshTypes.h"
#include "StaticMeshOperations.h"


DECLARE_LOG_CATEGORY_EXTERN(LogSkeletalMeshOperations, Log, All);



class SKELETALMESHDESCRIPTION_API FSkeletalMeshOperations : public FStaticMeshOperations
{
public:
	struct FSkeletalMeshAppendSettings
	{
		FSkeletalMeshAppendSettings()
			: SourceVertexIDOffset(0)
		{}

		int32 SourceVertexIDOffset;
		TArray<FBoneIndexType> SourceRemapBoneIndex;
	};
	
	static void AppendSkinWeight(const FMeshDescription& SourceMesh, FMeshDescription& TargetMesh, FSkeletalMeshAppendSettings& AppendSettings);
};
