// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "GroomSettings.h"
#include "HairStrandsInterface.h"
#include "HairStrandsMeshProjection.h"
#include "HairStrandsDatas.h"

struct FHairStrandsRestRootResource;

struct HAIRSTRANDSCORE_API FGroomBindingBuilder
{
	static FString GetVersion();

	// Build binding asset data
	static bool BuildBinding(class UGroomBindingAsset* BindingAsset, bool bUseGPU, bool bInitResources);

	// Transfer mesh vertex position from a source mesh on to a target mesh.
	// The topology of the two meshes can be different, but they need to shader the same UV layout
	static void TransferMesh(
		FRDGBuilder& GraphBuilder,
		const FHairStrandsProjectionMeshData& SourceMeshData,
		const FHairStrandsProjectionMeshData& TargetMeshData,
		TArray<FRWBuffer>& TransferredLODsPositions);

	// Project strands roots onto a mesh and compute closest triangle ID & barycentric
	static void ProjectStrands(
		FRDGBuilder& GraphBuilder,
		const FTransform& LocalToWorld,
		const FHairStrandsProjectionMeshData& TargetMeshData,
		TArray<FHairStrandsRestRootResource*>& InRestRootResources);

	// Update the root mesh projection data with unique valid mesh section IDs, based on the projection data
	static void BuildUniqueSections(FHairStrandsRootData::FMeshProjectionLOD& LOD);
};