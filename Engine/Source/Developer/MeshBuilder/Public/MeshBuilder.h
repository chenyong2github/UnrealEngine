// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class FStaticMeshRenderData;
class FStaticMeshLODGroup;
class USkeletalMesh;
class FStaticMeshSectionArray;
struct FStaticMeshBuildVertex;
struct FStaticMeshSection;


/**
 * Abstract class which is the base class of all builder.
 * All share code to build some render data should be found inside this class
 */
class MESHBUILDER_API FMeshBuilder
{
public:
	FMeshBuilder();
	
	/**
	 * Build function should be override and is the starting point for static mesh builders
	 */
	virtual bool Build(FStaticMeshRenderData& OutRenderData, UStaticMesh* StaticMesh, const FStaticMeshLODGroup& LODGroup) = 0;
	virtual bool Build(
		UStaticMesh* StaticMesh,
		TArray< FStaticMeshBuildVertex >& Verts,
		TArray< uint32 >& Indexes,
		FStaticMeshSectionArray& Sections,
		bool bBuildOnlyPosition,
		uint32& NumTexCoords,
		bool& bHasColors ) = 0;

	/**
	 * Build function should be override and is the starting point for skeletal mesh builders
	 */
	virtual bool Build(USkeletalMesh* SkeletalMesh, const int32 LODIndex, const bool bRegenDepLODs) = 0;

private:

};
