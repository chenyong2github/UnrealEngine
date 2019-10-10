// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshBuilder.h"

DECLARE_LOG_CATEGORY_EXTERN(LogStaticMeshBuilder, Log, All);

class UStaticMesh;
class FStaticMeshRenderData;
class FStaticMeshLODGroup;
class USkeletalMesh;

class MESHBUILDER_API FStaticMeshBuilder : public FMeshBuilder
{
public:
	FStaticMeshBuilder();

	virtual bool Build(FStaticMeshRenderData& OutRenderData, UStaticMesh* StaticMesh, const FStaticMeshLODGroup& LODGroup) override;

	//No support for skeletal mesh build in this class
	virtual bool Build(USkeletalMesh* SkeletalMesh, const int32 LODIndex, const bool bRegenDepLODs) override
	{
		bool No_Support_For_SkeletalMesh_Build_In_FStaticMeshBuilder_Class = false;
		check(No_Support_For_SkeletalMesh_Build_In_FStaticMeshBuilder_Class);
		return false;
	}

	virtual ~FStaticMeshBuilder() {}

private:

	void OnBuildRenderMeshStart(class UStaticMesh* StaticMesh, const bool bInvalidateLighting);
	void OnBuildRenderMeshFinish(class UStaticMesh* StaticMesh, const bool bRebuildBoundsAndCollision);

	/** Used to refresh all components in the scene that may be using a mesh we're editing */
	TSharedPtr<class FStaticMeshComponentRecreateRenderStateContext> RecreateRenderStateContext;
};

