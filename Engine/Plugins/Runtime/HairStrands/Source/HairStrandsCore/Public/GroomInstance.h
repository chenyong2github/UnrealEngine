// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "HairStrandsInterface.h"

class UMeshComponent;
enum class EGroomCacheType : uint8;

// @hair_todo: pack card ID + card UV in 32Bits alpha channel's of the position buffer:
//  * 10/10 bits for UV -> max 1024/1024 rect resolution
//  * 12 bits for cards count -> 4000 cards for a hair group
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FHairCardsVertexFactoryUniformShaderParameters, HAIRSTRANDSCORE_API)
	SHADER_PARAMETER(uint32, bInvertUV)
	SHADER_PARAMETER_SRV(Buffer<float4>, PositionBuffer)
	SHADER_PARAMETER_SRV(Buffer<float4>, PreviousPositionBuffer)
	SHADER_PARAMETER_SRV(Buffer<float4>, NormalsBuffer)
	SHADER_PARAMETER_SRV(Buffer<float4>, UVsBuffer)

	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, DepthTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, DepthSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, TangentTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, TangentSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, CoverageTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, CoverageSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, AttributeTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, AttributeSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, AuxilaryDataTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, AuxilaryDataSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

typedef TUniformBufferRef<FHairCardsVertexFactoryUniformShaderParameters> FHairCardsUniformBuffer;

// Represent/Describe data & resources of a hair group belonging to a groom
struct HAIRSTRANDSCORE_API FHairGroupInstance
{
	//////////////////////////////////////////////////////////////////////////////////////////
	// Helper struct which aggregate strands based data/resources

	struct FStrandsBase
	{
		bool HasValidData() const { return Data != nullptr && Data->GetNumPoints() > 0; }
		bool IsValid() const { return DeformedResource != nullptr; }

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

		bool HasValidRootData() const { return RestRootResource != nullptr && DeformedRootResource != nullptr; }
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

		FRDGExternalBuffer DebugAttributeBuffer;
		FHairGroupInstanceModifer Modifier;

		UMaterialInterface* Material = nullptr;
	} Strands;

	//////////////////////////////////////////////////////////////////////////////////////////
	// Cards
	struct FCards
	{
		bool IsValid() const { for (const FLOD& LOD: LODs) { if (LOD.IsValid()) { return true; } } return false; }
		bool IsValid(int32 LODIndex) const { return LODIndex >= 0 && LODIndex < LODs.Num() && LODs[LODIndex].DeformedResource != nullptr; }
		struct FLOD
		{
			bool IsValid() const { return DeformedResource != nullptr; }

			// Data
			FHairCardsDatas* Data = nullptr;

			// Resources
			FHairCardsRestResource* RestResource = nullptr;
			FHairCardsDeformedResource* DeformedResource = nullptr;
			#if RHI_RAYTRACING
			FHairStrandsRaytracingResource* RaytracingResource = nullptr;
			#endif

			// Interpolation data/resources
			FHairCardsInterpolationDatas* InterpolationData = nullptr;
			FHairCardsInterpolationResource* InterpolationResource = nullptr;

			FStrandsBaseWithInterpolation Guides;

			UMaterialInterface* Material = nullptr;
			FHairCardsUniformBuffer UniformBuffer[2];
		};
		TArray<FLOD> LODs;
	} Cards;

	//////////////////////////////////////////////////////////////////////////////////////////
	// Meshes
	struct FMeshes
	{
		bool IsValid() const { for (const FLOD& LOD : LODs) { if (LOD.IsValid()) { return true; } } return false; }
		bool IsValid(int32 LODIndex) const { return LODIndex >= 0 && LODIndex < LODs.Num() && LODs[LODIndex].DeformedResource != nullptr; }
		struct FLOD
		{
			bool IsValid() const { return DeformedResource != nullptr; }

			// Data
			FHairMeshesDatas* Data = nullptr;

			// Resources
			FHairMeshesRestResource* RestResource = nullptr;
			FHairMeshesDeformedResource* DeformedResource = nullptr;
			#if RHI_RAYTRACING
			FHairStrandsRaytracingResource* RaytracingResource = nullptr;
			#endif

			UMaterialInterface* Material = nullptr;
			FHairCardsUniformBuffer UniformBuffer[2];
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
		uint32					LastFrameIndex = ~0;

		int32					MeshLODIndex = ~0;
		EGroomBindingMeshType	GroomBindingType;
		EGroomCacheType			GroomCacheType;
		UMeshComponent*			MeshComponent = nullptr;
		FString					MeshComponentName;
		FTransform				SkeletalLocalToWorld = FTransform::Identity;
		bool					bDrawCardsGuides = false;

		TSharedPtr<class IGroomCacheBuffers, ESPMode::ThreadSafe> GroomCacheBuffers;

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
	const FBoxSphereBounds*	ProxyBounds = nullptr;
	const FBoxSphereBounds* ProxyLocalBounds = nullptr;
	bool					bUseCPULODSelection = true;
	bool					bForceCards = false;
	bool					bUpdatePositionOffset = false;
	bool					bCastShadow = true;

	bool IsValid() const 
	{
		return Meshes.IsValid() || Cards.IsValid() || Strands.IsValid();
	}
};