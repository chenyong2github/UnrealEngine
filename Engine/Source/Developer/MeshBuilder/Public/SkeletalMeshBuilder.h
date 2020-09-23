// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshBuilder.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSkeletalMeshBuilder, Log, All);

class UStaticMesh;
class FStaticMeshRenderData;
class FStaticMeshLODGroup;
class USkeletalMesh;

class MESHBUILDER_API FSkeletalMeshBuilder : public FMeshBuilder
{
public:
	FSkeletalMeshBuilder();

	//No support for static mesh build in this class
	virtual bool Build(FStaticMeshRenderData& OutRenderData, UStaticMesh* StaticMesh, const FStaticMeshLODGroup& LODGroup) override
	{
		bool No_Support_For_StaticMesh_Build_In_FSkeletalMeshBuilder_Class = false;
		check(No_Support_For_StaticMesh_Build_In_FSkeletalMeshBuilder_Class);
		return false;
	}

	virtual bool Build(
		UStaticMesh* StaticMesh,
		TArray< FStaticMeshBuildVertex >& Verts,
		TArray< uint32 >& Indexes,
		FStaticMeshSectionArray& Sections,
		bool bBuildOnlyPosition,
		uint32& NumTexCoords,
		bool& bHasColors) override
	{
		bool No_Support_For_StaticMesh_Build_In_FSkeletalMeshBuilder_Class = false;
		check(No_Support_For_StaticMesh_Build_In_FSkeletalMeshBuilder_Class);
		return false;
	}
	
	virtual bool Build(USkeletalMesh* SkeletalMesh, const int32 LODIndex, const bool bRegenDepLODs) override;

	virtual ~FSkeletalMeshBuilder() {}

private:

};

