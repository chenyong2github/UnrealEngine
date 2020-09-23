// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "HairStrandsInterface.h"

// Represent/Describe data & resources of a hair group belonging to a groom
struct HAIRSTRANDSCORE_API FHairGroupInstance
{
	//////////////////////////////////////////////////////////////////////////////////////////
	// Helper struct which aggregate strands based data/resources

	struct FStrandsBase
	{
		bool IsValid() const { return RestResource && RestResource->GetVertexCount() > 0; }

		// Data - Render & sim (rest) data
		FHairStrandsDatas* Data = nullptr;

		// Resources - Strands rest position data for sim & render strands
		// Resources - Strands deformed position data for sim & render strands
		FHairStrandsRestResource* RestResource = nullptr;
		FHairStrandsDeformedResource* DeformedResource = nullptr;

		// Resources - Rest root data, for deforming strands attached to a skinned mesh surface
		// Resources - Deformed root data, for deforming strands attached to a skinned mesh surface
		bool bOwnRootResourceAllocation = true;
		FHairStrandsRestRootResource* RestRootResource = nullptr;
		FHairStrandsDeformedRootResource* DeformedRootResource = nullptr;
	};

	struct FStrandsBaseWithInterpolation : FStrandsBase
	{
		// Data - Interpolation data (weights/Id/...) for transfering sim strands (i.e. guide) motion to render strands
		// Resources - Strands deformed position data for sim & render strands
		FHairStrandsInterpolationDatas* InterpolationData = nullptr;
		FHairStrandsInterpolationResource* InterpolationResource = nullptr;

		uint32 HairInterpolationType = 0;
	};

	//////////////////////////////////////////////////////////////////////////////////////////
	// Simulation
	struct FGuides : FStrandsBase
	{
		bool bIsSimulationEnable = false;
		bool bHasGlobalInterpolation = false;
	} Guides;

	//////////////////////////////////////////////////////////////////////////////////////////
	// Strands
	struct FStrands : FStrandsBaseWithInterpolation
	{
		// Resources - Strands cluster data for culling/voxelization purpose
		FHairStrandsClusterCullingResource* ClusterCullingResource = nullptr;

		// Resources - Raytracing data when enabling (expensive) raytracing method
		#if RHI_RAYTRACING
		FHairStrandsRaytracingResource* RenRaytracingResource = nullptr;
		#endif

		FRWBuffer DebugAttributeBuffer;
		FHairGroupInstanceModifer Modifier;

		UMaterialInterface* Material = nullptr;
	} Strands;

	//////////////////////////////////////////////////////////////////////////////////////////
	// Cards
	struct FCards
	{
		bool IsValid(int32 LODIndex) const { return LODIndex >= 0 && LODIndex < LODs.Num() && LODs[LODIndex].RestResource != nullptr; }
		struct FLOD
		{
			// Data
			FHairCardsDatas* Data = nullptr;

			// Resources
			FHairCardsRestResource* RestResource = nullptr;
			FHairCardsDeformedResource* DeformedResource = nullptr;

			// Interpolation data/resources
			FHairCardsInterpolationDatas* InterpolationData = nullptr;
			FHairCardsInterpolationResource* InterpolationResource = nullptr;

			FStrandsBaseWithInterpolation Guides;

			UMaterialInterface* Material = nullptr;
		};
		TArray<FLOD> LODs;
	} Cards;

	//////////////////////////////////////////////////////////////////////////////////////////
	// Meshes
	struct FMeshes
	{
		bool IsValid(int32 LODIndex) const { return LODIndex >= 0 && LODIndex < LODs.Num() && LODs[LODIndex].RestResource != nullptr && LODs[LODIndex].DeformedResource != nullptr; }
		struct FLOD
		{
			// Data
			FHairMeshesDatas* Data = nullptr;

			// Resources
			FHairMeshesRestResource* RestResource = nullptr;
			FHairMeshesDeformedResource* DeformedResource = nullptr;

			UMaterialInterface* Material = nullptr;
		};
		TArray<FLOD> LODs;
	} Meshes;
	
	//////////////////////////////////////////////////////////////////////////////////////////
	// Debug
	struct FDebug
	{
		// Data
		EHairStrandsDebugMode	DebugMode = EHairStrandsDebugMode::NoneDebug;
		uint32					ComponentId = ~0;
		uint32					GroupIndex = ~0;
		uint32					GroupCount = 0;
		FString					GroomAssetName;

		int32					MeshLODIndex = ~0;
		USkeletalMeshComponent*	SkeletalComponent = nullptr;
		FString					SkeletalComponentName;
		FTransform				SkeletalLocalToWorld = FTransform::Identity;
		bool					bDrawCardsGuides = false;

		// Transfer
		TArray<FRWBuffer> TransferredPositions;
		FHairStrandsProjectionMeshData SourceMeshData;
		FHairStrandsProjectionMeshData TargetMeshData;

		// Resources
		FHairStrandsDebugDatas::FResources* HairDebugResource = nullptr;
	} Debug;

	FTransform				LocalToWorld = FTransform::Identity;
	EWorldType::Type		WorldType;
	FHairGroupPublicData*	HairGroupPublicData = nullptr;
	EHairGeometryType		GeometryType = EHairGeometryType::NoneGeometry;
};
