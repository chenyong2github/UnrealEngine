// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomManager.h"
#include "HairStrandsMeshProjection.h"

#include "GPUSkinCache.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "CommonRenderResources.h"
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalRenderPublic.h"
#include "GroomTextureBuilder.h"
#include "GroomResources.h"
#include "GroomInstance.h"
#include "GroomGeometryCache.h"

DEFINE_LOG_CATEGORY_STATIC(LogGroomManager, Log, All);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Runtime execution order (on the render thread):
//  * Register
//  * For each frame
//		* Update
//		* Update triangles information for dynamic meshes
//		* RunHairStrandsInterpolation (Interpolation callback)
//  * UnRegister
//
// This code supposed a  small number of instance (~10), and won't scale to large crowed (linear loop, lot of cache misses, ...)
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FHairStrandsManager
{
	// #hair_todo: change this array to a queue update, in order make processing/update thread safe.
	TArray<FHairGroupInstance*> Instances;

	FHairStrandsManager()
	{
		// Reserve a large a mount of object to avoid any potential memory reallocation, which 
		// could cause some thread safety issue. This is a workaround against the non-thread-safe array
		Instances.Reserve(64);
	}
};

FHairStrandsManager GHairManager;

void RegisterHairStrands(FHairGroupInstance* InInstance)
{
	for (const FHairGroupInstance* Instance : GHairManager.Instances)
	{
		if (Instance->Debug.ComponentId == InInstance->Debug.ComponentId && 
			Instance->Debug.GroupIndex == InInstance->Debug.GroupIndex)
		{
			// Component already registered. This should not happen. 
			UE_LOG(LogGroomManager, Warning, TEXT("Component already register. This should't happen. Please report this to a rendering engineer."))
			return;
		}
	}

	check(InInstance->HairGroupPublicData);
	GHairManager.Instances.Add(InInstance);
}

void UnregisterHairStrands(uint32 ComponentId)
{
	for (int32 Index=0;Index< GHairManager.Instances.Num();)
	{
		const FHairGroupInstance* Instance = GHairManager.Instances[Index];

		if (Instance->Debug.ComponentId == ComponentId)
		{
			GHairManager.Instances[Index] = GHairManager.Instances[GHairManager.Instances.Num()-1];
			GHairManager.Instances.SetNum(GHairManager.Instances.Num() - 1, false);
		}
		else
		{
			++Index;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void RunHairStrandsInterpolation(
	FRDGBuilder& GraphBuilder,
	EWorldType::Type WorldType, 
	const FGPUSkinCache* SkinCache,
	const FShaderDrawDebugData* ShaderDrawData,
	FGlobalShaderMap* ShaderMap, 
	EHairStrandsInterpolationType Type,
	FHairStrandClusterData* ClusterData)
{
	check(IsInRenderingThread());

	DECLARE_GPU_STAT(HairStrandsInterpolationGrouped);
	RDG_EVENT_SCOPE(GraphBuilder, "HairStrandsInterpolationGrouped");
	RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandsInterpolationGrouped);

	// Update dynamic mesh triangles
	for (FHairGroupInstance* Instance : GHairManager.Instances)
	{
		int32 FrameLODIndex = -1;
		if (Instance->WorldType != WorldType)
			continue;
	
		check(Instance->HairGroupPublicData);

		FCachedGeometry CachedGeometry;
		if (Instance->Debug.SkeletalComponent)
		{
			if (SkinCache)
			{
				CachedGeometry = SkinCache->GetCachedGeometry(Instance->Debug.SkeletalComponent->ComponentId.PrimIDValue);
			}
			else
			{
				//#hair_todo: Need to have a (frame) cache to insure that we don't recompute the same projection several time
				// Actual populate the cache with only the needed part basd on the groom projection data. At the moment it recompute everything ...
				BuildCacheGeometry(GraphBuilder, ShaderMap, Instance->Debug.SkeletalComponent, CachedGeometry);
			}
		}
		if (CachedGeometry.Sections.Num() == 0)
			continue;

		FHairStrandsProjectionMeshData::LOD MeshDataLOD;
		for (const FCachedGeometry::Section& Section : CachedGeometry.Sections)
		{
			// Ensure all mesh's sections have the same LOD index
			if (FrameLODIndex < 0) FrameLODIndex = Section.LODIndex;
			check(FrameLODIndex == Section.LODIndex);

			MeshDataLOD.Sections.Add(ConvertMeshSection(Section));
		}

		FBufferTransitionQueue TransitionQueue;

		Instance->Debug.MeshLODIndex = FrameLODIndex;
		if (0 <= FrameLODIndex)
		{
			if (EHairStrandsInterpolationType::RenderStrands == Type)
			{
				if (FrameLODIndex < Instance->Strands.DeformedRootResource->LODs.Num() && Instance->Strands.DeformedRootResource->LODs[FrameLODIndex].IsValid())
				{
					AddHairStrandUpdateMeshTrianglesPass(
						GraphBuilder, 
						ShaderMap, 
						FrameLODIndex, 
						HairStrandsTriangleType::DeformedPose, 
						MeshDataLOD, 
						Instance->Strands.RestRootResource, 
						Instance->Strands.DeformedRootResource,
						TransitionQueue);
				}
			}
			else if (EHairStrandsInterpolationType::SimulationStrands == Type)
			{
				if (FrameLODIndex < Instance->Guides.DeformedRootResource->LODs.Num() && Instance->Guides.DeformedRootResource->LODs[FrameLODIndex].IsValid())
				{
					AddHairStrandUpdateMeshTrianglesPass(
						GraphBuilder,
						ShaderMap,
						Instance->Debug.MeshLODIndex,
						HairStrandsTriangleType::DeformedPose,
						MeshDataLOD,
						Instance->Guides.RestRootResource,
						Instance->Guides.DeformedRootResource,
						TransitionQueue);
				}

				if (FrameLODIndex < Instance->Guides.DeformedRootResource->LODs.Num() && Instance->Guides.DeformedRootResource->LODs[FrameLODIndex].IsValid())
				{
					AddHairStrandInitMeshSamplesPass(
						GraphBuilder, 
						ShaderMap, 
						Instance->Debug.MeshLODIndex, 
						HairStrandsTriangleType::DeformedPose, 
						MeshDataLOD, 
						Instance->Guides.RestRootResource,
						Instance->Guides.DeformedRootResource,
						TransitionQueue);

					AddHairStrandUpdateMeshSamplesPass(
						GraphBuilder, 
						ShaderMap, 
						Instance->Debug.MeshLODIndex, 
						MeshDataLOD, 
						Instance->Guides.RestRootResource, 
						Instance->Guides.DeformedRootResource, 
						TransitionQueue);
				}
			}
		}

		TransitBufferToReadable(GraphBuilder, TransitionQueue);
	}

	// Reset deformation
	if (EHairStrandsInterpolationType::SimulationStrands == Type)
	{
		for (FHairGroupInstance* Instance : GHairManager.Instances)
		{
			if (Instance->WorldType != WorldType)
				continue;

			ResetHairStrandsInterpolation(GraphBuilder, Instance, Instance->Debug.MeshLODIndex);
		}
	}

	// Hair interpolation
	if (EHairStrandsInterpolationType::RenderStrands == Type)
	{
		for (FHairGroupInstance* Instance : GHairManager.Instances)
		{
			if (Instance->WorldType != WorldType)
				continue;

			ComputeHairStrandsInterpolation(
				GraphBuilder,
				ShaderDrawData, 
				Instance,
				Instance->Debug.MeshLODIndex,
				ClusterData);
		}
	}
}

static void RunHairStrandsGatherCluster(
	EWorldType::Type WorldType,
	FHairStrandClusterData* ClusterData)
{
	for (FHairGroupInstance* Instance : GHairManager.Instances)
	{
		if (Instance->WorldType != WorldType)
			continue;

		RegisterClusterData(Instance, ClusterData);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool HasHairStrandsFolliculeMaskQueries();
void RunHairStrandsFolliculeMaskQueries(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap);

bool HasHairStrandsTexturesQueries();
void RunHairStrandsTexturesQueries(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, const struct FShaderDrawDebugData* DebugShaderData);

bool HasHairStrandsBindigQueries();
void RunHairStrandsBindingQueries(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap);

bool HasHairCardsAtlasQueries();
void RunHairCardsAtlasQueries(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, const struct FShaderDrawDebugData* DebugShaderData);

static void RunHairStrandsProcess(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, const struct FShaderDrawDebugData* DebugShaderData)
{
	if (HasHairStrandsTexturesQueries())
	{
		RunHairStrandsTexturesQueries(GraphBuilder, ShaderMap, DebugShaderData);
	}

	if (HasHairStrandsFolliculeMaskQueries())
	{
		RunHairStrandsFolliculeMaskQueries(GraphBuilder, ShaderMap);
	}

	if (HasHairStrandsBindigQueries())
	{
		RunHairStrandsBindingQueries(GraphBuilder, ShaderMap);
	}

	if (HasHairCardsAtlasQueries())
	{
		RunHairCardsAtlasQueries(GraphBuilder, ShaderMap, DebugShaderData);
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RunHairStrandsDebug(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	EWorldType::Type WorldType,
	const FSceneView& View,
	const FGPUSkinCache* SkinCache,
	const FShaderDrawDebugData* ShaderDrawData,
	const TArray<FHairGroupInstance*>& Instances,
	TRefCountPtr<IPooledRenderTarget>& SceneColor,
	FIntRect Viewport,
	TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HairStrands Bookmark API

void ProcessHairStrandsBookmark(
	FRDGBuilder& GraphBuilder,
	EHairStrandsBookmark Bookmark,
	FHairStrandsBookmarkParameters& Parameters)
{
	if (Bookmark == EHairStrandsBookmark::ProcessTasks)
	{
		const bool bHasHairStardsnProcess =
			HasHairCardsAtlasQueries() ||
			HasHairStrandsTexturesQueries() ||
			HasHairStrandsFolliculeMaskQueries() ||
			HasHairStrandsBindigQueries();

		if (bHasHairStardsnProcess)
		{
			RunHairStrandsProcess(GraphBuilder, Parameters.ShaderMap, Parameters.DebugShaderData);
		}
	}
	else if (Bookmark == EHairStrandsBookmark::ProcessGuideInterpolation)
	{
		RunHairStrandsInterpolation(
			GraphBuilder,
			Parameters.WorldType,
			Parameters.SkinCache,
			Parameters.DebugShaderData,
			Parameters.ShaderMap,
			EHairStrandsInterpolationType::SimulationStrands,
			&Parameters.HairClusterData);
	}
	else if (Bookmark == EHairStrandsBookmark::ProcessGatherCluster)
	{
		RunHairStrandsGatherCluster(
			Parameters.WorldType,
			&Parameters.HairClusterData);
	}
	else if (Bookmark == EHairStrandsBookmark::ProcessStrandsInterpolation)
	{
		RunHairStrandsInterpolation(
			GraphBuilder,
			Parameters.WorldType,
			Parameters.SkinCache,
			Parameters.DebugShaderData,
			Parameters.ShaderMap,
			EHairStrandsInterpolationType::RenderStrands,
			&Parameters.HairClusterData);
	}
	else if (Bookmark == EHairStrandsBookmark::ProcessDebug)
	{
		RunHairStrandsDebug(
			GraphBuilder,
			Parameters.ShaderMap,
			Parameters.WorldType,
			*Parameters.View,
			Parameters.SkinCache,
			Parameters.DebugShaderData,
			GHairManager.Instances,
			Parameters.SceneColorTexture,
			Parameters.View->UnscaledViewRect,
			Parameters.View->ViewUniformBuffer);
	}
}

void ProcessHairStrandsParameters(FHairStrandsBookmarkParameters& Parameters)
{
	Parameters.bHasElements = GHairManager.Instances.Num() > 0;
}
