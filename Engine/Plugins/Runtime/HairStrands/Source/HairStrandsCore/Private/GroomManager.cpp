// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomManager.h"
#include "HairStrandsMeshProjection.h"

#include "GeometryCacheComponent.h"
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
#include "HAL/IConsoleManager.h"
#include "SceneView.h"
#include "HairCardsVertexFactory.h"
#include "HairStrandsVertexFactory.h"

static int32 GHairStrandsMinLOD = 0;
static FAutoConsoleVariableRef CVarGHairStrandsMinLOD(TEXT("r.HairStrands.MinLOD"), GHairStrandsMinLOD, TEXT("Clamp the min hair LOD to this value, preventing to reach lower/high-quality LOD."), ECVF_Scalability);

static int32 GHairStrands_UseCards = 0;
static FAutoConsoleVariableRef CVarHairStrands_UseCards(TEXT("r.HairStrands.UseCardsInsteadOfStrands"), GHairStrands_UseCards, TEXT("Force cards geometry on all groom elements. If no cards data is available, nothing will be displayed"), ECVF_Scalability);

static int32 GHairStrands_SwapBufferEndOfFrame = 1;
static FAutoConsoleVariableRef CVarGHairStrands_SwapBufferEndOfFrame(TEXT("r.HairStrands.SwapEndOfFrame"), GHairStrands_SwapBufferEndOfFrame, TEXT("Swap rendering buffer at the end of frame. This is an experimental toggle. Default:1"));

static int32 GHairStrands_ManualSkinCache = 0;
static FAutoConsoleVariableRef CVarGHairStrands_ManualSkinCache(TEXT("r.HairStrands.ManualSkinCache"), GHairStrands_ManualSkinCache, TEXT("If skin cache is not enabled, and grooms use skinning method, this enable a simple skin cache mechanisme for groom. Default:disable"));

static int32 GHairStrands_InterpolationFrustumCullingEnable = 1;
static FAutoConsoleVariableRef CVarHairStrands_InterpolationFrustumCullingEnable(TEXT("r.HairStrands.Interoplation.FrustumCulling"), GHairStrands_InterpolationFrustumCullingEnable, TEXT("Swap rendering buffer at the end of frame. This is an experimental toggle. Default:1"));

bool IsHairStrandsSkinCacheEnable()
{
	return GHairStrands_ManualSkinCache > 0;
}

DEFINE_LOG_CATEGORY_STATIC(LogGroomManager, Log, All);

///////////////////////////////////////////////////////////////////////////////////////////////////

void FHairGroupInstance::FCards::FLOD::InitVertexFactory()
{
	VertexFactory[0]->InitResources();
	if (DeformedResource)
	{
		VertexFactory[1]->InitResources();
	}
}

void FHairGroupInstance::FMeshes::FLOD::InitVertexFactory()
{
	VertexFactory[0]->InitResources();
	if (DeformedResource)
	{
		VertexFactory[1]->InitResources();
	}
}

static bool IsInstanceFrustumCullingEnable()
{
	return GHairStrands_InterpolationFrustumCullingEnable > 0;
}

bool NeedsUpdateCardsMeshTriangles();

static void RunInternalHairStrandsInterpolation(
	FRDGBuilder& GraphBuilder,
	const FSceneView* View,
	const FHairStrandsInstances& Instances,
	const FGPUSkinCache* SkinCache,
	const FShaderDrawDebugData* ShaderDrawData,
	FGlobalShaderMap* ShaderMap, 
	EHairStrandsInterpolationType Type,
	FHairStrandClusterData* ClusterData)
{
	check(IsInRenderingThread());

	// Update dynamic mesh triangles
	for (FHairStrandsInstance* AbstractInstance : Instances)
	{
		FHairGroupInstance* Instance = static_cast<FHairGroupInstance*>(AbstractInstance);

		int32 MeshLODIndex = -1;
		if (Instance->GeometryType == EHairGeometryType::NoneGeometry)
			continue;
	
		check(Instance->HairGroupPublicData);

		FCachedGeometry CachedGeometry;
		if (Instance->Debug.GroomBindingType == EGroomBindingMeshType::SkeletalMesh)
		{
			if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(Instance->Debug.MeshComponent))
			{
				if (SkinCache)
				{
					CachedGeometry = SkinCache->GetCachedGeometry(SkeletalMeshComponent->ComponentId.PrimIDValue);
				}

				if (IsHairStrandsSkinCacheEnable() && CachedGeometry.Sections.Num() == 0)
				{
					//#hair_todo: Need to have a (frame) cache to insure that we don't recompute the same projection several time
					// Actual populate the cache with only the needed part basd on the groom projection data. At the moment it recompute everything ...
					BuildCacheGeometry(GraphBuilder, ShaderMap, SkeletalMeshComponent, CachedGeometry);
				}
			}
		}
		else if (UGeometryCacheComponent* GeometryCacheComponent = Cast<UGeometryCacheComponent>(Instance->Debug.MeshComponent))
		{
			BuildCacheGeometry(GraphBuilder, ShaderMap, GeometryCacheComponent, CachedGeometry);
		}

		FHairStrandsProjectionMeshData::LOD MeshDataLOD;
		for (const FCachedGeometry::Section& Section : CachedGeometry.Sections)
		{
			// Ensure all mesh's sections have the same LOD index
			if (MeshLODIndex < 0) MeshLODIndex = Section.LODIndex;
			check(MeshLODIndex == Section.LODIndex);

			MeshDataLOD.Sections.Add(ConvertMeshSection(Section));
		}

		Instance->Debug.MeshLODIndex = MeshLODIndex;
		if (0 <= MeshLODIndex)
		{
			const EHairGeometryType InstanceGeometryType = Instance->GeometryType;
			const EHairBindingType InstanceBindingType = Instance->BindingType;

			const uint32 HairLODIndex = Instance->HairGroupPublicData->LODIndex;
			const EHairBindingType BindingType = Instance->HairGroupPublicData->GetBindingType(HairLODIndex);
			const bool bSimulationEnable = Instance->HairGroupPublicData->IsSimulationEnable(HairLODIndex);
			const bool bGlobalDeformationEnable = Instance->HairGroupPublicData->IsGlobalInterpolationEnable(HairLODIndex);
			check(InstanceBindingType == BindingType);

			if (EHairStrandsInterpolationType::RenderStrands == Type)
			{
				if (InstanceGeometryType == EHairGeometryType::Strands)
				{
					if (BindingType == EHairBindingType::Skinning || bGlobalDeformationEnable)
					{
						check(Instance->Strands.HasValidRootData());
						check(Instance->Strands.DeformedRootResource->IsValid(MeshLODIndex));

						AddHairStrandUpdateMeshTrianglesPass(
							GraphBuilder, 
							ShaderMap, 
							MeshLODIndex, 
							HairStrandsTriangleType::DeformedPose, 
							MeshDataLOD, 
							Instance->Strands.RestRootResource, 
							Instance->Strands.DeformedRootResource);

						AddHairStrandUpdatePositionOffsetPass(
							GraphBuilder,
							ShaderMap,
							MeshLODIndex,
							Instance->Strands.DeformedRootResource,
							Instance->Strands.DeformedResource);
					}
					else if (bSimulationEnable)
					{
						check(Instance->Strands.HasValidData());

						AddHairStrandUpdatePositionOffsetPass(
							GraphBuilder,
							ShaderMap,
							MeshLODIndex,
							nullptr,
							Instance->Strands.DeformedResource);
					}
				}
				else if (InstanceGeometryType == EHairGeometryType::Cards)
				{
					if (Instance->Cards.IsValid(HairLODIndex))
					{
						FHairGroupInstance::FCards::FLOD& CardsInstance = Instance->Cards.LODs[HairLODIndex];
						if (BindingType == EHairBindingType::Skinning || bGlobalDeformationEnable)
						{
							check(CardsInstance.Guides.IsValid());
							check(CardsInstance.Guides.HasValidRootData());
							check(CardsInstance.Guides.DeformedRootResource->IsValid(MeshLODIndex));

							AddHairStrandUpdateMeshTrianglesPass(
								GraphBuilder,
								ShaderMap,
								MeshLODIndex,
								HairStrandsTriangleType::DeformedPose,
								MeshDataLOD,
								CardsInstance.Guides.RestRootResource,
								CardsInstance.Guides.DeformedRootResource);

							AddHairStrandUpdatePositionOffsetPass(
								GraphBuilder,
								ShaderMap,
								MeshLODIndex,
								CardsInstance.Guides.DeformedRootResource,
								CardsInstance.Guides.DeformedResource);
						}
						else if (bSimulationEnable)
						{
							check(CardsInstance.Guides.IsValid());
							AddHairStrandUpdatePositionOffsetPass(
								GraphBuilder,
								ShaderMap,
								MeshLODIndex,
								nullptr,
								CardsInstance.Guides.DeformedResource);
						}
					}
				}
				else if (InstanceGeometryType == EHairGeometryType::Meshes)
				{
					// Nothing to do
				}
			}
			else if (EHairStrandsInterpolationType::SimulationStrands == Type)
			{
				// Guide update need to run only if simulation is enabled, or if RBF is enabled (since RFB are transfer through guides)
				if (bGlobalDeformationEnable || bSimulationEnable)
				{
					check(Instance->Guides.IsValid());

					if (BindingType == EHairBindingType::Skinning)
					{
						check(Instance->Guides.IsValid());
						check(Instance->Guides.HasValidRootData());
						check(Instance->Guides.DeformedRootResource->IsValid(MeshLODIndex));

						AddHairStrandUpdateMeshTrianglesPass(
							GraphBuilder,
							ShaderMap,
							Instance->Debug.MeshLODIndex,
							HairStrandsTriangleType::DeformedPose,
							MeshDataLOD,
							Instance->Guides.RestRootResource,
							Instance->Guides.DeformedRootResource);

						if (bGlobalDeformationEnable)
						{
							AddHairStrandInitMeshSamplesPass(
								GraphBuilder,
								ShaderMap,
								Instance->Debug.MeshLODIndex,
								HairStrandsTriangleType::DeformedPose,
								MeshDataLOD,
								Instance->Guides.RestRootResource,
								Instance->Guides.DeformedRootResource);

							AddHairStrandUpdateMeshSamplesPass(
								GraphBuilder,
								ShaderMap,
								Instance->Debug.MeshLODIndex,
								MeshDataLOD,
								Instance->Guides.RestRootResource,
								Instance->Guides.DeformedRootResource);
						}

						AddHairStrandUpdatePositionOffsetPass(
							GraphBuilder,
							ShaderMap,
							Instance->Debug.MeshLODIndex,
							Instance->Guides.DeformedRootResource,
							Instance->Guides.DeformedResource);
					}
					else if (bSimulationEnable)
					{
						check(Instance->Guides.IsValid());

						AddHairStrandUpdatePositionOffsetPass(
							GraphBuilder,
							ShaderMap,
							Instance->Debug.MeshLODIndex,
							nullptr,
							Instance->Guides.DeformedResource);
					}
				}
			}
		}
	}

	// Reset deformation
	if (EHairStrandsInterpolationType::SimulationStrands == Type)
	{
		for (FHairStrandsInstance* AbstractInstance : Instances)
		{
			FHairGroupInstance* Instance = static_cast<FHairGroupInstance*>(AbstractInstance);

			if (Instance->GeometryType == EHairGeometryType::NoneGeometry)
				continue;

			ResetHairStrandsInterpolation(GraphBuilder, ShaderMap, Instance, Instance->Debug.MeshLODIndex);
		}
	}

	// Hair interpolation
	if (EHairStrandsInterpolationType::RenderStrands == Type)
	{
		for (FHairStrandsInstance* AbstractInstance : Instances)
		{
			FHairGroupInstance* Instance = static_cast<FHairGroupInstance*>(AbstractInstance);

			if (Instance->GeometryType == EHairGeometryType::NoneGeometry)
				continue;

 			ComputeHairStrandsInterpolation(
				GraphBuilder, 
				ShaderMap,
				ShaderDrawData, 
				Instance,
				Instance->Debug.MeshLODIndex,
				ClusterData);
		}
	}
}

static void RunHairStrandsInterpolation_Guide(
	FRDGBuilder& GraphBuilder,
	const FSceneView* View,
	const FHairStrandsInstances& Instances,
	const FGPUSkinCache* SkinCache,
	const FShaderDrawDebugData* ShaderDrawData,
	FGlobalShaderMap* ShaderMap,
	FHairStrandClusterData* ClusterData)
{
	check(IsInRenderingThread());

	DECLARE_GPU_STAT(HairGuideInterpolation);
	RDG_EVENT_SCOPE(GraphBuilder, "HairGuideInterpolation");
	RDG_GPU_STAT_SCOPE(GraphBuilder, HairGuideInterpolation);

	RunInternalHairStrandsInterpolation(
		GraphBuilder,
		View,
		Instances,
		SkinCache,
		ShaderDrawData,
		ShaderMap,
		EHairStrandsInterpolationType::SimulationStrands,
		ClusterData);
}

static void RunHairStrandsInterpolation_Strands(
	FRDGBuilder& GraphBuilder,
	const FSceneView* View,
	const FHairStrandsInstances& Instances,
	const FGPUSkinCache* SkinCache,
	const FShaderDrawDebugData* ShaderDrawData,
	FGlobalShaderMap* ShaderMap,
	FHairStrandClusterData* ClusterData)
{
	check(IsInRenderingThread());

	DECLARE_GPU_STAT(HairStrandsInterpolation);
	RDG_EVENT_SCOPE(GraphBuilder, "HairStrandsInterpolation");
	RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandsInterpolation);

	RunInternalHairStrandsInterpolation(
		GraphBuilder,
		View,
		Instances,
		SkinCache,
		ShaderDrawData,
		ShaderMap,
		EHairStrandsInterpolationType::RenderStrands,
		ClusterData);
}


static void RunHairStrandsGatherCluster(
	const FHairStrandsInstances& Instances,
	FHairStrandClusterData* ClusterData)
{
	for (FHairStrandsInstance* AbstractInstance : Instances)
	{
		FHairGroupInstance* Instance = static_cast<FHairGroupInstance*>(AbstractInstance);

		if (Instance->GeometryType != EHairGeometryType::Strands)
			continue;

		if (Instance->Strands.IsValid())
		{
			if (Instance->Strands.bIsCullingEnabled)
			{
				RegisterClusterData(Instance, ClusterData);
			}
			else
			{
				Instance->HairGroupPublicData->bCullingResultAvailable = false;
			}
		}
	}
}


// Return the LOD which should be used for a given screen size and LOD bias value
// This function is mirrored in HairStrandsClusterCommon.ush
static float GetHairInstanceLODIndex(const TArray<float>& InLODScreenSizes, float InScreenSize, float InLODBias)
{
	const uint32 LODCount = InLODScreenSizes.Num();
	check(LODCount > 0);

	float OutLOD = 0;
	if (LODCount > 1 && InScreenSize < InLODScreenSizes[0])
	{
		for (uint32 LODIt = 1; LODIt < LODCount; ++LODIt)
		{
			if (InScreenSize >= InLODScreenSizes[LODIt])
			{
				uint32 PrevLODIt = LODIt - 1;

				const float S_Delta = abs(InLODScreenSizes[PrevLODIt] - InLODScreenSizes[LODIt]);
				const float S = S_Delta > 0 ? FMath::Clamp(FMath::Abs(InScreenSize - InLODScreenSizes[LODIt]) / S_Delta, 0.f, 1.f) : 0;
				OutLOD = PrevLODIt + (1 - S);
				break;
			}
			else if (LODIt == LODCount - 1)
			{
				OutLOD = LODIt;
			}
		}
	}

	if (InLODBias != 0)
	{
		OutLOD = FMath::Clamp(OutLOD + InLODBias, 0.f, float(LODCount - 1));
	}
	return OutLOD;
}


static EHairGeometryType ConvertLODGeometryType(EHairGeometryType Type, bool InbUseCards, EShaderPlatform Platform)
{
	// Force cards only if it is enabled or fallback on cards if strands are disabled
	InbUseCards = (InbUseCards || !IsHairStrandsEnabled(EHairStrandsShaderType::Strands, Platform)) && IsHairStrandsEnabled(EHairStrandsShaderType::Cards, Platform);

	switch (Type)
	{
	case EHairGeometryType::Strands: return IsHairStrandsEnabled(EHairStrandsShaderType::Strands, Platform) ? (InbUseCards ? EHairGeometryType::Cards : EHairGeometryType::Strands) : (InbUseCards ? EHairGeometryType::Cards : EHairGeometryType::NoneGeometry);
	case EHairGeometryType::Cards:   return IsHairStrandsEnabled(EHairStrandsShaderType::Cards, Platform) ? EHairGeometryType::Cards : EHairGeometryType::NoneGeometry;
	case EHairGeometryType::Meshes:  return IsHairStrandsEnabled(EHairStrandsShaderType::Meshes, Platform) ? EHairGeometryType::Meshes : EHairGeometryType::NoneGeometry;
	}
	return EHairGeometryType::NoneGeometry;
}

static void RunHairBufferSwap(const FHairStrandsInstances& Instances, const TArray<const FSceneView*> Views)
{
	EShaderPlatform ShaderPlatform = EShaderPlatform::SP_NumPlatforms;
	if (Views.Num() > 0)
	{
		ShaderPlatform = Views[0]->GetShaderPlatform();
	}

	for (FHairStrandsInstance* AbstractInstance : Instances)
	{
		FHairGroupInstance* Instance = static_cast<FHairGroupInstance*>(AbstractInstance);

		int32 MeshLODIndex = -1;
		check(Instance);
		if (!Instance)
			continue;

		check(Instance->HairGroupPublicData);

		// 1. Swap current/previous buffer for all LODs
		const bool bIsPaused = Views[0]->Family->bWorldIsPaused;
		if (!bIsPaused)
		{
			if (Instance->Guides.DeformedResource) { Instance->Guides.DeformedResource->SwapBuffer(); }
			if (Instance->Strands.DeformedResource) { Instance->Strands.DeformedResource->SwapBuffer(); }

			for (uint32 LODIt = 0, LODCount = Instance->Cards.LODs.Num(); LODIt < LODCount; ++LODIt)
			{
				if (Instance->Cards.IsValid(LODIt))
				{
					if (Instance->Cards.LODs[LODIt].DeformedResource)
					{
						Instance->Cards.LODs[LODIt].DeformedResource->SwapBuffer();
					}
					if (Instance->Cards.LODs[LODIt].Guides.IsValid())
					{
						Instance->Cards.LODs[LODIt].Guides.DeformedResource->SwapBuffer();
					}
				}
			}

			for (uint32 LODIt = 0, LODCount = Instance->Meshes.LODs.Num(); LODIt < LODCount; ++LODIt)
			{
				if (Instance->Meshes.IsValid(LODIt))
				{
					if (Instance->Meshes.LODs[LODIt].DeformedResource)
					{
						Instance->Meshes.LODs[LODIt].DeformedResource->SwapBuffer();
					}
				}
			}

			Instance->Debug.LastFrameIndex = Views[0]->Family->FrameNumber;
		}

		// 2. Update the local offset (used for improving precision of strands data, as they are stored in 16bit precision)
		// If attached to a mesh, ProxyLocalBound is mapped on the bounding box of the parent skeletal mesh. This offset is 
		// based on the center of the skeletal mesh (which is computed based on the physics capsules/boxes/...)
		if (Instance->bUpdatePositionOffset && Instance->ProxyLocalBounds && !bIsPaused)
		{
			const FVector PositionOffset = Instance->ProxyLocalBounds->GetSphere().Center;
			if (Instance->Strands.DeformedResource)
			{
				Instance->Strands.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current) = PositionOffset;
			}

			if (Instance->Guides.DeformedResource)
			{
				Instance->Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current) = PositionOffset;
			}

			for (uint32 LODIt = 0, LODCount = Instance->Cards.LODs.Num(); LODIt < LODCount; ++LODIt)
			{
				if (Instance->Cards.IsValid(LODIt) && Instance->Cards.LODs[LODIt].Guides.IsValid())
				{
					Instance->Cards.LODs[LODIt].Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current) = PositionOffset;
				}
			}
		}
	}
}

static void RunHairLODSelection(FRDGBuilder& GraphBuilder, const FHairStrandsInstances& Instances, const TArray<const FSceneView*> Views)
{
	EShaderPlatform ShaderPlatform = EShaderPlatform::SP_NumPlatforms;
	if (Views.Num() > 0)
	{
		ShaderPlatform = Views[0]->GetShaderPlatform();
	}

	for (FHairStrandsInstance* AbstractInstance : Instances)
	{
		FHairGroupInstance* Instance = static_cast<FHairGroupInstance*>(AbstractInstance);

		int32 MeshLODIndex = -1;
		check(Instance);
		check(Instance->HairGroupPublicData);

		//if (Instance->ProxyLocalToWorld)
		//{
		//	Instance->LocalToWorld = *Instance->ProxyLocalToWorld;
		//}

		// Perform LOD selection based on all the views	
		// CPU LOD selection. 
		// * When enable the CPU LOD selection allow to change the geometry representation. 
		// * GPU LOD selecion allow fine grain LODing, but does not support representation changes (strands, cards, meshes)
		// 
		// Use the forced LOD index if set, otherwise compute the LOD based on the maximal screensize accross all views
		// Compute the view where the screen size is maximale
		const float PrevLODIndex = Instance->HairGroupPublicData->GetLODIndex();
		float LODIndex = Instance->Strands.Modifier.LODForcedIndex; // check where this is updated 

		// Insure that MinLOD is necessary taken into account if a force LOD is request (i.e., LODIndex>=0). If a Force LOD 
		// is not resquested (i.e., LODIndex<0), the MinLOD is applied after ViewLODIndex has been determined in the codeblock below
		const float MinLOD = FMath::Max(0, GHairStrandsMinLOD);
		LODIndex = Instance->bUseCPULODSelection && LODIndex >= 0 ? FMath::Max(LODIndex, MinLOD) : LODIndex;
		if (LODIndex < 0 && Instance->bUseCPULODSelection)
		{
			const FSphere SphereBound = Instance->ProxyBounds ? Instance->ProxyBounds->GetSphere() : FSphere(0);
			for (const FSceneView* View : Views)
			{
				const float ScreenSize = ComputeBoundsScreenSize(FVector4(SphereBound.Center, 1), SphereBound.W, *View);
				const float LODBias = Instance->Strands.Modifier.LODBias;
				const float ViewLODIndex = FMath::Max(MinLOD, GetHairInstanceLODIndex(Instance->HairGroupPublicData->GetLODScreenSizes(), ScreenSize, LODBias));

				// Select highest LOD accross all views
				LODIndex = LODIndex == -1 ? ViewLODIndex : FMath::Min(LODIndex, ViewLODIndex);
			}
		}

		const TArray<EHairGeometryType>& LODGeometryTypes = Instance->HairGroupPublicData->GetLODGeometryTypes();
		const TArray<bool>& LODVisibilities = Instance->HairGroupPublicData->GetLODVisibilities();
		const int32 LODCount = LODVisibilities.Num();

		// CPU selection: insure the LOD index is in valid range 
		// GPU selection: -1 means auto-selection based on GPU data, >=0 means forced LOD
		if (Instance->bUseCPULODSelection)
		{
			LODIndex = FMath::Clamp(LODIndex, 0.f, float(LODCount - 1));
		}
		else
		{
			LODIndex = FMath::Clamp(LODIndex, -1.f, float(LODCount - 1));
		}
		const int32 IntLODIndex = FMath::Clamp(FMath::FloorToInt(LODIndex), 0, LODCount - 1);
		const bool bIsVisible = LODVisibilities[IntLODIndex];
		const bool bForceCards = GHairStrands_UseCards > 0 || Instance->bForceCards; // todo
		EHairGeometryType GeometryType = ConvertLODGeometryType(LODGeometryTypes[IntLODIndex], bForceCards, ShaderPlatform);

		// Skip cluster culling if strands have a single LOD (i.e.: LOD0), and the instance is static (i.e., no skinning, no simulation, ...)
		bool bCullingEnable = false;
		if (GeometryType == EHairGeometryType::Meshes)
		{
			if (!Instance->Meshes.IsValid(IntLODIndex))
			{
				GeometryType = EHairGeometryType::NoneGeometry;
			}
		}
		else if (GeometryType == EHairGeometryType::Cards)
		{
			if (!Instance->Cards.IsValid(IntLODIndex))
			{
				GeometryType = EHairGeometryType::NoneGeometry;
			}
		}
		else if (GeometryType == EHairGeometryType::Strands)
		{
			if (!Instance->Strands.IsValid())
			{
				GeometryType = EHairGeometryType::NoneGeometry;
			}
			else
			{
				const bool bNeedLODing		= LODIndex > 0;
				const bool bNeedDeformation = Instance->Strands.DeformedResource != nullptr;
				bCullingEnable = bNeedLODing || bNeedDeformation;
			}
		}

		if (!bIsVisible)
		{
			GeometryType = EHairGeometryType::NoneGeometry;
		}

		// Lazy allocation of resources
		// Note: Allocation will only be done if the resources it not yet initialized.
		if (Instance->Guides.Data)
		{
			if (Instance->Guides.RestRootResource)		Instance->Guides.RestRootResource->Allocate(GraphBuilder);
			if (Instance->Guides.RestResource)			Instance->Guides.RestResource->Allocate(GraphBuilder);
			if (Instance->Guides.DeformedRootResource)	Instance->Guides.DeformedRootResource->Allocate(GraphBuilder);
			if (Instance->Guides.DeformedResource)		Instance->Guides.DeformedResource->Allocate(GraphBuilder);
		}

		if (GeometryType == EHairGeometryType::Meshes)
		{
			FHairGroupInstance::FMeshes::FLOD& InstanceLOD = Instance->Meshes.LODs[IntLODIndex];

			if (InstanceLOD.DeformedResource)	InstanceLOD.DeformedResource->Allocate(GraphBuilder);
			#if RHI_RAYTRACING
			if (InstanceLOD.RaytracingResource)	InstanceLOD.RaytracingResource->Allocate(GraphBuilder);
			#endif

			InstanceLOD.InitVertexFactory();
		}
		else if (GeometryType == EHairGeometryType::Cards)
		{
			FHairGroupInstance::FCards::FLOD& InstanceLOD = Instance->Cards.LODs[IntLODIndex];

			if (InstanceLOD.DeformedResource)				InstanceLOD.DeformedResource->Allocate(GraphBuilder);
			if (InstanceLOD.Guides.RestRootResource)		InstanceLOD.Guides.RestRootResource->Allocate(GraphBuilder);
			if (InstanceLOD.Guides.RestResource)			InstanceLOD.Guides.RestResource->Allocate(GraphBuilder);
			if (InstanceLOD.Guides.DeformedRootResource)	InstanceLOD.Guides.DeformedRootResource->Allocate(GraphBuilder);
			if (InstanceLOD.Guides.DeformedResource)		InstanceLOD.Guides.DeformedResource->Allocate(GraphBuilder);
			#if RHI_RAYTRACING
			if (InstanceLOD.RaytracingResource)				InstanceLOD.RaytracingResource->Allocate(GraphBuilder);
			#endif
			InstanceLOD.InitVertexFactory();
		}
		else if (GeometryType == EHairGeometryType::Strands)
		{
			if (Instance->Strands.RestRootResource)			Instance->Strands.RestRootResource->Allocate(GraphBuilder);
			if (Instance->Strands.RestResource)				Instance->Strands.RestResource->Allocate(GraphBuilder);
			if (Instance->Strands.ClusterCullingResource)	Instance->Strands.ClusterCullingResource->Allocate(GraphBuilder);
			if (Instance->Strands.InterpolationResource)	Instance->Strands.InterpolationResource->Allocate(GraphBuilder);

			if (Instance->Strands.DeformedRootResource)		Instance->Strands.DeformedRootResource->Allocate(GraphBuilder);
			if (Instance->Strands.DeformedResource)			Instance->Strands.DeformedResource->Allocate(GraphBuilder);
			#if RHI_RAYTRACING
			if (Instance->Strands.RenRaytracingResource)	Instance->Strands.RenRaytracingResource->Allocate(GraphBuilder);
			#endif
			Instance->Strands.VertexFactory->InitResources();
		}

		Instance->HairGroupPublicData->SetLODVisibility(bIsVisible);
		Instance->HairGroupPublicData->SetLODIndex(LODIndex);
		Instance->HairGroupPublicData->SetLODBias(0);
		Instance->HairGroupPublicData->VFInput.GeometryType = GeometryType;
		Instance->HairGroupPublicData->VFInput.bHasLODSwitch = (FMath::FloorToInt(PrevLODIndex) != FMath::FloorToInt(LODIndex));
		Instance->GeometryType = GeometryType;
		Instance->BindingType = Instance->HairGroupPublicData->GetBindingType(IntLODIndex);
		Instance->Guides.bIsSimulationEnable = Instance->HairGroupPublicData->IsSimulationEnable(IntLODIndex);
		Instance->Guides.bHasGlobalInterpolation = Instance->HairGroupPublicData->IsGlobalInterpolationEnable(IntLODIndex);
		Instance->Strands.bIsCullingEnabled = bCullingEnable;
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////
bool HasHairStrandsFolliculeMaskQueries();
void RunHairStrandsFolliculeMaskQueries(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap);

#if WITH_EDITOR
bool HasHairStrandsTexturesQueries();
void RunHairStrandsTexturesQueries(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, const struct FShaderDrawDebugData* DebugShaderData);
#endif

bool HasHairStrandsBindigQueries();
void RunHairStrandsBindingQueries(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap);

#if WITH_EDITOR
bool HasHairCardsAtlasQueries();
void RunHairCardsAtlasQueries(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, const struct FShaderDrawDebugData* DebugShaderData);
#endif

static void RunHairStrandsProcess(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, const struct FShaderDrawDebugData* DebugShaderData)
{
#if WITH_EDITOR
	if (HasHairStrandsTexturesQueries())
	{
		RunHairStrandsTexturesQueries(GraphBuilder, ShaderMap, DebugShaderData);
	}
#endif

	if (HasHairStrandsFolliculeMaskQueries())
	{
		RunHairStrandsFolliculeMaskQueries(GraphBuilder, ShaderMap);
	}

	if (HasHairStrandsBindigQueries())
	{
		RunHairStrandsBindingQueries(GraphBuilder, ShaderMap);
	}

#if WITH_EDITOR
	if (HasHairCardsAtlasQueries())
	{
		RunHairCardsAtlasQueries(GraphBuilder, ShaderMap, DebugShaderData);
	}
#endif
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void RunHairStrandsDebug(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const FSceneView& View,
	const FHairStrandsInstances& Instances,
	const FGPUSkinCache* SkinCache,
	const FShaderDrawDebugData* ShaderDrawData,
	FRDGTextureRef SceneColor,
	FIntRect Viewport,
	const TUniformBufferRef<FViewUniformShaderParameters>& ViewUniformBuffer);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HairStrands Bookmark API

void ProcessHairStrandsBookmark(
	FRDGBuilder* GraphBuilder,
	EHairStrandsBookmark Bookmark,
	FHairStrandsBookmarkParameters& Parameters)
{
	check(Parameters.Instances != nullptr);

	const bool bCulling =
		IsInstanceFrustumCullingEnable() && (
			Bookmark == EHairStrandsBookmark::ProcessGatherCluster ||
			Bookmark == EHairStrandsBookmark::ProcessStrandsInterpolation ||
			Bookmark == EHairStrandsBookmark::ProcessDebug);
	FHairStrandsInstances& Instances = bCulling ? Parameters.VisibleInstances : *Parameters.Instances;

	if (Bookmark == EHairStrandsBookmark::ProcessTasks)
	{
		const bool bHasHairStardsnProcess =
		#if WITH_EDITOR
			HasHairCardsAtlasQueries() ||
			HasHairStrandsTexturesQueries() ||
		#endif
			HasHairStrandsFolliculeMaskQueries() ||
			HasHairStrandsBindigQueries();

		if (bHasHairStardsnProcess)
		{
			check(GraphBuilder);
			RunHairStrandsProcess(*GraphBuilder, Parameters.ShaderMap, Parameters.DebugShaderData);
		}
	}
	else if (Bookmark == EHairStrandsBookmark::ProcessLODSelection)
	{
		if (GHairStrands_SwapBufferEndOfFrame <= 0)
		{
			RunHairBufferSwap(
				*Parameters.Instances,
				Parameters.AllViews);
		}
		check(GraphBuilder);
		RunHairLODSelection(
			*GraphBuilder,
			*Parameters.Instances,
			Parameters.AllViews);
	}
	else if (Bookmark == EHairStrandsBookmark::ProcessEndOfFrame)
	{
		if (GHairStrands_SwapBufferEndOfFrame > 0)
		{
			RunHairBufferSwap(
				*Parameters.Instances,
				Parameters.AllViews);
		}
	}
	else if (Bookmark == EHairStrandsBookmark::ProcessGuideInterpolation)
	{
		check(GraphBuilder);
		RunHairStrandsInterpolation_Guide(
			*GraphBuilder,
			Parameters.View,
			Instances,
			Parameters.SkinCache,
			Parameters.DebugShaderData,
			Parameters.ShaderMap,
			&Parameters.HairClusterData);
	}
	else if (Bookmark == EHairStrandsBookmark::ProcessGatherCluster)
	{
		RunHairStrandsGatherCluster(
			Instances,
			&Parameters.HairClusterData);
	}
	else if (Bookmark == EHairStrandsBookmark::ProcessStrandsInterpolation)
	{
		check(GraphBuilder);
		RunHairStrandsInterpolation_Strands(
			*GraphBuilder,
			Parameters.View,
			Instances,
			Parameters.SkinCache,
			Parameters.DebugShaderData,
			Parameters.ShaderMap,
			&Parameters.HairClusterData);
	}
	else if (Bookmark == EHairStrandsBookmark::ProcessDebug)
	{
		check(GraphBuilder);
		RunHairStrandsDebug(
			*GraphBuilder,
			Parameters.ShaderMap,
			*Parameters.View,
			Instances,
			Parameters.SkinCache,
			Parameters.DebugShaderData,
			Parameters.SceneColorTexture,
			Parameters.View->UnscaledViewRect,
			Parameters.View->ViewUniformBuffer);
	}
}

void ProcessHairStrandsParameters(FHairStrandsBookmarkParameters& Parameters)
{
	Parameters.bHasElements = Parameters.Instances && Parameters.Instances->Num() > 0;
}
