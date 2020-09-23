// Copyright Epic Games, Inc. All Rights Reserved. 

#include "GroomComponent.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "PrimitiveSceneProxy.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "HairStrandsRendering.h"
#include "HairCardsVertexFactory.h"
#include "HairStrandsVertexFactory.h"
#include "RayTracingInstanceUtils.h"
#include "HairStrandsInterface.h"
#include "UObject/UObjectIterator.h"
#include "GlobalShader.h"
#include "Components/SkeletalMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Engine/RendererSettings.h"
#include "Animation/AnimationSettings.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "Misc/MapErrors.h"
#include "NiagaraComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "GroomBindingBuilder.h"
#include "RenderTargetPool.h"
#include "GroomManager.h"
#include "GroomInstance.h"

static float GHairClipLength = -1;
static FAutoConsoleVariableRef CVarHairClipLength(TEXT("r.HairStrands.DebugClipLength"), GHairClipLength, TEXT("Clip hair strands which have a lenth larger than this value. (default is -1, no effect)"));
float GetHairClipLength() { return GHairClipLength > 0 ? GHairClipLength : 100000;  }

static int GHairStrandsSimulation = 1;
static FAutoConsoleVariableRef CVarHairStrandsSimulation(TEXT("r.HairStrands.Simulation"), GHairStrandsSimulation, TEXT("Enable or disable the groom simulation"));

#define LOCTEXT_NAMESPACE "GroomComponent"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FHairGroupDesc GetGroomGroupsDesc(const UGroomAsset* Asset, UGroomComponent* Component, uint32 GroupIndex)
{
	if (!Asset)
	{
		return FHairGroupDesc();
	}

	FHairGroupDesc O = Component->GroomGroupsDesc[GroupIndex];
	O.HairCount  = Asset->HairGroupsData[GroupIndex].Strands.Data.GetNumCurves();
	O.GuideCount = Asset->HairGroupsData[GroupIndex].Guides.Data.GetNumCurves();
	O.HairLength = Asset->HairGroupsData[GroupIndex].Strands.Data.StrandsCurves.MaxLength;
	O.bSupportVoxelization = Asset->HairGroupsRendering[GroupIndex].ShadowSettings.bVoxelize;

	if (O.HairWidth == 0)					{ O.HairWidth					= Asset->HairGroupsRendering[GroupIndex].GeometrySettings.HairWidth;					}
	if (O.HairRootScale == 0)				{ O.HairRootScale				= Asset->HairGroupsRendering[GroupIndex].GeometrySettings.HairRootScale;				}
	if (O.HairTipScale == 0)				{ O.HairTipScale				= Asset->HairGroupsRendering[GroupIndex].GeometrySettings.HairTipScale;					}
	if (O.HairClipLength == 0)				{ O.HairClipLength				= Asset->HairGroupsRendering[GroupIndex].GeometrySettings.HairClipScale * O.HairLength;	}
	if (O.HairShadowDensity == 0)			{ O.HairShadowDensity			= Asset->HairGroupsRendering[GroupIndex].ShadowSettings.HairShadowDensity;				}
	if (O.HairRaytracingRadiusScale == 0)	{ O.HairRaytracingRadiusScale	= Asset->HairGroupsRendering[GroupIndex].ShadowSettings.HairRaytracingRadiusScale;		}
	if (O.bUseStableRasterization == 0)		{ O.bUseStableRasterization		= Asset->HairGroupsRendering[GroupIndex].AdvancedSettings.bUseStableRasterization;		}
	if (O.bScatterSceneLighting == 0)		{ O.bScatterSceneLighting		= Asset->HairGroupsRendering[GroupIndex].AdvancedSettings.bScatterSceneLighting;		}

	return O;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * An material render proxy which overrides the debug mode parameter.
 */
class FHairDebugModeMaterialRenderProxy : public FMaterialRenderProxy
{
public:
	const FMaterialRenderProxy* const Parent;
	const float DebugMode;
	const float HairMinRadius;
	const float HairMaxRadius;
	const float HairClipLength;

	FName DebugModeParamName;
	FName MinHairRadiusParamName;
	FName MaxHairRadiusParamName;
	FName HairClipLengthParamName;

	/** Initialization constructor. */
	FHairDebugModeMaterialRenderProxy(const FMaterialRenderProxy* InParent, float InMode, float InMinRadius, float InMaxRadius, float InHairClipLength) :
		Parent(InParent),
		DebugMode(InMode),
		HairMinRadius(InMinRadius),
		HairMaxRadius(InMaxRadius),
		HairClipLength(InHairClipLength),
		DebugModeParamName(NAME_FloatProperty),
		MinHairRadiusParamName(NAME_ByteProperty),
		MaxHairRadiusParamName(NAME_IntProperty),
		HairClipLengthParamName(NAME_BoolProperty)
	{}

	virtual const FMaterial& GetMaterialWithFallback(ERHIFeatureLevel::Type InFeatureLevel, const FMaterialRenderProxy*& OutFallbackMaterialRenderProxy) const override
	{
		return Parent->GetMaterialWithFallback(InFeatureLevel, OutFallbackMaterialRenderProxy);
	}

	virtual bool GetVectorValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const override
	{
		return Parent->GetVectorValue(ParameterInfo, OutValue, Context);
	}

	virtual bool GetScalarValue(const FHashedMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const override
	{
		if (ParameterInfo.Name == DebugModeParamName)
		{
			*OutValue = DebugMode;
			return true;
		}
		else if (ParameterInfo.Name == MinHairRadiusParamName)
		{
			*OutValue = HairMinRadius;
			return true;
		}
		else if (ParameterInfo.Name == MaxHairRadiusParamName)
		{
			*OutValue = HairMaxRadius;
			return true;
		}
		else if (ParameterInfo.Name == HairClipLengthParamName)
		{
			*OutValue = HairClipLength;
			return true;
		}
		else
		{
			return Parent->GetScalarValue(ParameterInfo, OutValue, Context);
		}
	}

	virtual bool GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo, const URuntimeVirtualTexture** OutValue, const FMaterialRenderContext& Context) const override
	{
		return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
	}

	virtual bool GetTextureValue(const FHashedMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const override
	{
		return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
	}
};

enum class EHairMaterialCompatibility : uint8
{
	Valid,
	Invalid_UsedWithHairStrands,
	Invalid_ShadingModel,
	Invalid_BlendMode,
	Invalid_IsNull
};

static EHairMaterialCompatibility IsHairMaterialCompatible(UMaterialInterface* MaterialInterface, ERHIFeatureLevel::Type FeatureLevel)
{
	if (MaterialInterface)
	{
		if (!MaterialInterface->GetRelevance_Concurrent(FeatureLevel).bHairStrands)
		{
			return EHairMaterialCompatibility::Invalid_UsedWithHairStrands;
		}
		if (!MaterialInterface->GetShadingModels().HasShadingModel(MSM_Hair))
		{
			return EHairMaterialCompatibility::Invalid_ShadingModel;
		}
		if (MaterialInterface->GetBlendMode() != BLEND_Opaque && MaterialInterface->GetBlendMode() != BLEND_Masked)
		{
			return EHairMaterialCompatibility::Invalid_BlendMode;
		}
	}
	else
	{
		return EHairMaterialCompatibility::Invalid_IsNull;
	}

	return EHairMaterialCompatibility::Valid;
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
//  FStrandHairSceneProxy
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////

class FHairStrandsSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FHairStrandsSceneProxy(UGroomComponent* Component)
		: FPrimitiveSceneProxy(Component)
		, StrandsVertexFactory(GetScene().GetFeatureLevel(), "FStrandsHairSceneProxy")
		, CardsAndMeshesVertexFactory(GetScene().GetFeatureLevel(), "FCardsAndMeshesHairSceneProxy")
		, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))	
	{
		HairGroupInstances = Component->HairGroupInstances;
		check(Component);
		check(Component->GroomAsset);
		check(Component->GroomAsset->GetNumHairGroups() > 0);
		ComponentId = Component->ComponentId.PrimIDValue;
		Strands_DebugMaterial = Component->Strands_DebugMaterial;
		PredictedLODIndex = &Component->PredictedLODIndex;

		FHairStrandsVertexFactory::FDataType StrandsVFData;
		StrandsVFData.Instances = Component->HairGroupInstances;

		FHairCardsVertexFactory::FDataType CardsVFData;
		CardsVFData.Instances = Component->HairGroupInstances;

		check(Component->HairGroupInstances.Num());

		GroomLODSelection = Component->GroomAsset->LODSelectionType;

		const int32 GroupCount = Component->GroomAsset->GetNumHairGroups();
		check(Component->GroomAsset->HairGroupsData.Num() == Component->HairGroupInstances.Num());
		for (int32 GroupIt=0; GroupIt<GroupCount; GroupIt++)
		{	
			const bool bIsVisible = Component->GroomAsset->HairGroupsInfo[GroupIt].bIsVisible;

			const FHairGroupData& InGroupData = Component->GroomAsset->HairGroupsData[GroupIt];
			const FHairGroupInstance* HairInstance = Component->HairGroupInstances[GroupIt];
			check(HairInstance->HairGroupPublicData);

			UMaterialInterface* Strands_Material = Component->GetMaterial(GroupIt);
			if (Strands_Material == nullptr || !Strands_Material->GetMaterialResource(GetScene().GetFeatureLevel())->IsUsedWithHairStrands())
			{
				Strands_Material = Component->Strands_DefaultMaterial;
			}
			
			UMaterialInterface* Cards_Material = nullptr;
			if (IsHairCardsEnable())
			{
				Cards_Material = GroupIt < Component->GroomAsset->HairGroupsCards.Num() ? Component->GroomAsset->HairGroupsCards[GroupIt].Material : nullptr;
				if (Cards_Material == nullptr || !Cards_Material->GetMaterialResource(GetScene().GetFeatureLevel())->IsUsedWithHairStrands())
				{
					Cards_Material = Component->Cards_DefaultMaterial;
				}
			}

			UMaterialInterface* Meshes_Material = nullptr;
			if (IsHairMeshesEnable())
			{
				Meshes_Material = GroupIt < Component->GroomAsset->HairGroupsMeshes.Num() ?  Component->GroomAsset->HairGroupsMeshes[GroupIt].Material : nullptr;
				if (Meshes_Material == nullptr || !Meshes_Material->GetMaterialResource(GetScene().GetFeatureLevel())->IsUsedWithHairStrands())
				{
					Meshes_Material = Component->Meshes_DefaultMaterial;
				}
			}

			{
				#if RHI_RAYTRACING
				FRayTracingGeometry* RayTracingGeometry = nullptr;
				const bool bSupportRaytracing = HairInstance->GeometryType == EHairGeometryType::Strands;
				if (IsHairRayTracingEnabled() && HairInstance->Strands.RenRaytracingResource && bSupportRaytracing)
				{
					RayTracingGeometry = &HairInstance->Strands.RenRaytracingResource->RayTracingGeometry;
				}
				#endif

				HairGroup& OutGroupData = HairGroups.AddDefaulted_GetRef();
				#if RHI_RAYTRACING
				OutGroupData.RayTracingGeometry = RayTracingGeometry;
				#endif
				OutGroupData.bIsVsibible = bIsVisible;
				OutGroupData.PublicData = HairInstance->HairGroupPublicData;
				OutGroupData.LODSettings = Component->GroomAsset->HairGroupsLOD[GroupIt].LODs;
				OutGroupData.Strands_Material = Strands_Material;
				OutGroupData.Cards_Material = Cards_Material;
				OutGroupData.Meshes_Material = Meshes_Material;
			}
		}

		FHairStrandsVertexFactory* LocalStrandsVertexFactory = &StrandsVertexFactory;
		FHairCardsVertexFactory* LocalCardsAndMeshesVertexFactory = &CardsAndMeshesVertexFactory;
		ENQUEUE_RENDER_COMMAND(InitHairStrandsVertexFactory)(
			[LocalStrandsVertexFactory, StrandsVFData, LocalCardsAndMeshesVertexFactory, CardsVFData](FRHICommandListImmediate& RHICmdList)
		{
			LocalStrandsVertexFactory->SetData(StrandsVFData);
			LocalStrandsVertexFactory->InitResource();

			LocalCardsAndMeshesVertexFactory->SetData(CardsVFData);
			LocalCardsAndMeshesVertexFactory->InitResource();
		});
	}

	virtual ~FHairStrandsSceneProxy()
	{
		StrandsVertexFactory.ReleaseResource();
		CardsAndMeshesVertexFactory.ReleaseResource();
	}
	
#if RHI_RAYTRACING
	virtual bool IsRayTracingRelevant() const { return true; }
	virtual bool IsRayTracingStaticRelevant() const { return false; }

	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext & Context, TArray<struct FRayTracingInstance>& OutRayTracingInstances) override
	{
		if (!IsHairRayTracingEnabled() || HairGroups.Num() == 0)
			return;

		for (const HairGroup& GroupData : HairGroups)
		{
			if (GroupData.RayTracingGeometry && GroupData.RayTracingGeometry->RayTracingGeometryRHI.IsValid())
			{
				for (const FRayTracingGeometrySegment& Segment : GroupData.RayTracingGeometry->Initializer.Segments)
				{
					check(Segment.VertexBuffer.IsValid());
				}
				AddOpaqueRaytracingInstance(GetLocalToWorld(), GroupData.RayTracingGeometry, RaytracingInstanceMask_ThinShadow, OutRayTracingInstances);
			}
		}
	}
#endif

	// Return the LOD which should be used for a given screen size and LOD bias value
	// This function is mirrored in HairStrandsClusterCommon.ush
	float GetLODIndex(const TArray<float>& InLODScreenSizes, float InScreenSize, float InLODBias) const 
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

	inline EHairGeometryType Convert(EGroomGeometryType Type, EShaderPlatform Platform) const
	{
		switch (Type)
		{
		case EGroomGeometryType::Strands: return IsHairStrandsEnable(Platform)	? EHairGeometryType::Strands : EHairGeometryType::NoneGeometry;
		case EGroomGeometryType::Cards:   return IsHairCardsEnable()			? EHairGeometryType::Cards   : EHairGeometryType::NoneGeometry;
		case EGroomGeometryType::Meshes:  return IsHairMeshesEnable()			? EHairGeometryType::Meshes  : EHairGeometryType::NoneGeometry;
		}
		return EHairGeometryType::NoneGeometry;
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		TArray<FHairGroupInstance*> Instances = StrandsVertexFactory.GetData().Instances;
		if (Instances.Num() == 0)
		{
			return;
		}

		const uint32 GroupCount = Instances.Num();

		QUICK_SCOPE_CYCLE_COUNTER(STAT_HairStrandsSceneProxy_GetDynamicMeshElements);


		FMaterialRenderProxy* Strands_MaterialProxy = nullptr;
		const EHairStrandsDebugMode DebugMode = Instances[0]->Debug.DebugMode != EHairStrandsDebugMode::NoneDebug ? Instances[0]->Debug.DebugMode : GetHairStrandsDebugStrandsMode();
		if (DebugMode != EHairStrandsDebugMode::NoneDebug)
		{
			float DebugModeScalar = 0;
			switch(DebugMode)
			{
			case EHairStrandsDebugMode::NoneDebug					: DebugModeScalar =99.f; break;
			case EHairStrandsDebugMode::SimHairStrands				: DebugModeScalar = 0.f; break;
			case EHairStrandsDebugMode::RenderHairStrands			: DebugModeScalar = 0.f; break;
			case EHairStrandsDebugMode::RenderHairRootUV			: DebugModeScalar = 1.f; break;
			case EHairStrandsDebugMode::RenderHairUV				: DebugModeScalar = 2.f; break;
			case EHairStrandsDebugMode::RenderHairSeed				: DebugModeScalar = 3.f; break;
			case EHairStrandsDebugMode::RenderHairDimension			: DebugModeScalar = 4.f; break;
			case EHairStrandsDebugMode::RenderHairRadiusVariation	: DebugModeScalar = 5.f; break;
			case EHairStrandsDebugMode::RenderHairRootUDIM			: DebugModeScalar = 6.f; break;
			case EHairStrandsDebugMode::RenderHairBaseColor			: DebugModeScalar = 7.f; break;
			case EHairStrandsDebugMode::RenderHairRoughness			: DebugModeScalar = 8.f; break;
			case EHairStrandsDebugMode::RenderVisCluster			: DebugModeScalar = 0.f; break;
			};

			// TODO: fix this as the radius is incorrect. This code run before the interpolation code, which is where HairRadius is updated.
			float HairMaxRadius = 0;
			for (FHairGroupInstance* Instance : Instances)
			{
				HairMaxRadius = FMath::Max(HairMaxRadius, Instance->Strands.Modifier.HairWidth * 0.5f);
			}

			const float HairClipLength = GetHairClipLength();
			auto DebugMaterial = new FHairDebugModeMaterialRenderProxy(Strands_DebugMaterial ? Strands_DebugMaterial->GetRenderProxy() : nullptr, DebugModeScalar, 0, HairMaxRadius, HairClipLength);
			Collector.RegisterOneFrameMaterialProxy(DebugMaterial);
			Strands_MaterialProxy = DebugMaterial;
		}

		// For each group, compute the highest LOD accross all views
		// This is necessary, as the culling & simulation is run for a single LOD level, which is shared for all views
		TArray<float> LODIndices;
		LODIndices.SetNum(GroupCount);
		for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
		{
			const FHairGroupInstance* Instance = Instances[GroupIt];

			LODIndices[GroupIt] = -1;
			if (GroomLODSelection == EHairLODSelectionType::Cpu)
			{
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					const FSceneView* View = Views[ViewIndex];
					if (View->bIsReflectionCapture || View->bIsPlanarReflection)
					{
						continue;
					}
					const FSphere SphereBound = GetBounds().GetSphere();
					const float ScreenSize = ComputeBoundsScreenSize(FVector4(SphereBound.Center, 1), SphereBound.W, *View);
					const float LODBias  = Instance->Strands.Modifier.LODBias;
					const float LODIndex = GetLODIndex(Instance->HairGroupPublicData->GetLODScreenSizes(), ScreenSize, LODBias);

					// Select highest LOD accross all views
					LODIndices[GroupIt] = LODIndices[GroupIt] == -1 ? LODIndex : FMath::Min(LODIndices[GroupIt], LODIndex);
				}
			}
		}

		// Transfer LOD index selection back to the component
		if (PredictedLODIndex != nullptr && LODIndices.Num() > 0)
		{
			int32 MinLODIndex = 99;
			for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
			{
				MinLODIndex = FMath::Min(FMath::FloorToInt(LODIndices[GroupIt]), MinLODIndex);
			}
			*PredictedLODIndex = MinLODIndex;
		}

		// Need information back from the rendering thread to knwo which representation to use (strands/cards/mesh)
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FSceneView* View = Views[ViewIndex];
			if (View->bIsReflectionCapture || View->bIsPlanarReflection)
			{
				continue;
			}

			if (IsShown(View) && (VisibilityMap & (1 << ViewIndex)))
			{
				for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
				{
					const HairGroup& GroupData = HairGroups[GroupIt];
					FHairGroupInstance* Instance = Instances[GroupIt];

					// CPU LOD selection. 
					// * When enable the CPU LOD selection allow to change the geometry representation. 
					// * GPU LOD selecion allow fine grain LODing, but does not support representation changes (strands, cards, meshes)
					
					float LODIndex = Instance->Strands.Modifier.LODForcedIndex;
					bool bIsVisible = true;
					Instance->GeometryType = EHairGeometryType::NoneGeometry;
					if (LODIndex == -1 && GroomLODSelection == EHairLODSelectionType::Cpu)
					{
						LODIndex = LODIndices[GroupIt];
					}
					{
						const TArray<bool>& LODVisibility = Instance->HairGroupPublicData->GetLODVisibilities();
						const int32 iLODIndex = FMath::Clamp(FMath::FloorToInt(LODIndex), 0, GroupData.LODSettings.Num() - 1);
						bIsVisible = LODVisibility[iLODIndex];
						Instance->GeometryType = Convert(GroupData.LODSettings[iLODIndex].GeometryType, View->GetShaderPlatform());
					}

					if (Instance->GeometryType == EHairGeometryType::NoneGeometry)
					{
						continue;
					}

					Instance->HairGroupPublicData->SetLODVisibility(bIsVisible);
					Instance->HairGroupPublicData->SetLODIndex(LODIndex);
					Instance->HairGroupPublicData->SetLODBias(0);
					const int32 IntLODIndex = Instance->HairGroupPublicData->GetIntLODIndex();

					if (!bIsVisible)
					{
						continue;
					}

					FVertexFactory* VertexFactory = nullptr;
					FIndexBuffer* IndexBuffer = nullptr;
					FMaterialRenderProxy* MaterialRenderProxy = nullptr;

					uint32 NumPrimitive = 0;
					uint32 HairVertexCount = 0;
					uint32 MaxVertexIndex = 0;
					bool bUseCulling = false;
					if (Instance->GeometryType == EHairGeometryType::Meshes)
					{
						if (!Instance->Meshes.IsValid(IntLODIndex))
						{
							continue;
						}
						VertexFactory = (FVertexFactory*)&CardsAndMeshesVertexFactory;
						HairVertexCount = Instance->Meshes.LODs[IntLODIndex].RestResource->PrimitiveCount * 3;
						MaxVertexIndex = HairVertexCount;
						NumPrimitive = HairVertexCount / 3;
						IndexBuffer = &Instance->Meshes.LODs[IntLODIndex].RestResource->IndexBuffer;
						bUseCulling = false;
						MaterialRenderProxy = GroupData.Meshes_Material->GetRenderProxy();
					}
					else if (Instance->GeometryType == EHairGeometryType::Cards)
					{
						if (!Instance->Cards.IsValid(IntLODIndex))
						{
							continue;
						}
						VertexFactory = (FVertexFactory*)&CardsAndMeshesVertexFactory;
						HairVertexCount = Instance->Cards.LODs[IntLODIndex].RestResource->PrimitiveCount * 3;
						MaxVertexIndex = HairVertexCount;
						NumPrimitive = HairVertexCount / 3;
						IndexBuffer = &Instance->Cards.LODs[IntLODIndex].RestResource->RestIndexBuffer;
						bUseCulling = false;
						MaterialRenderProxy = GroupData.Cards_Material->GetRenderProxy();
					}
					else // if (Instance->GeometryType == EHairGeometryType::Strands)
					{
						VertexFactory = (FVertexFactory*)&StrandsVertexFactory;
						HairVertexCount = Instance->Strands.RestResource->GetVertexCount();
						MaxVertexIndex = HairVertexCount * 6;
						NumPrimitive = 0;
						bUseCulling = true;
						MaterialRenderProxy = Strands_MaterialProxy == nullptr ? GroupData.Strands_Material->GetRenderProxy() : Strands_MaterialProxy;
					}

					if (MaterialRenderProxy == nullptr || !GroupData.bIsVsibible)
					{
						continue;
					}

					// Draw the mesh.
					FMeshBatch& Mesh = Collector.AllocateMesh();

					const bool bUseCardsOrMeshes = Instance->GeometryType == EHairGeometryType::Cards || Instance->GeometryType == EHairGeometryType::Meshes;
					Mesh.CastShadow = bUseCardsOrMeshes; // TODo, but this will remove the voxel and the deep shadow part
					Mesh.bUseForMaterial  = bUseCardsOrMeshes;
					Mesh.bUseForDepthPass = bUseCardsOrMeshes;

					FMeshBatchElement& BatchElement = Mesh.Elements[0];
					BatchElement.IndexBuffer = IndexBuffer;
					Mesh.bWireframe = false;
					Mesh.VertexFactory = VertexFactory;
					Mesh.MaterialRenderProxy = MaterialRenderProxy;
					bool bHasPrecomputedVolumetricLightmap;
					FMatrix PreviousLocalToWorld;
					int32 SingleCaptureIndex;
					bool bOutputVelocity = false;
					bool bDrawVelocity = false; // Velocity vector is done in a custom fashion
					GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);
					bOutputVelocity = false;
					FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
					DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, false, bDrawVelocity, bOutputVelocity);
					BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

					BatchElement.FirstIndex = 0;
					BatchElement.NumInstances = 1;
					if (bUseCulling)
					{
						BatchElement.NumPrimitives = 0;
						BatchElement.IndirectArgsBuffer = bUseCulling ? Instance->HairGroupPublicData->GetDrawIndirectBuffer().Buffer.GetReference() : nullptr;
						BatchElement.IndirectArgsOffset = 0;
					}
					else
					{
						BatchElement.NumPrimitives = NumPrimitive;
						BatchElement.IndirectArgsBuffer = nullptr;
						BatchElement.IndirectArgsOffset = 0;
					}
					
					// Setup our vertex factor custom data
					BatchElement.VertexFactoryUserData = const_cast<void*>(reinterpret_cast<const void*>(Instance->HairGroupPublicData));

					BatchElement.MinVertexIndex = 0;
					BatchElement.MaxVertexIndex = MaxVertexIndex;
					BatchElement.UserData = reinterpret_cast<void*>(uint64(ComponentId));
					Mesh.ReverseCulling = bUseCardsOrMeshes ? IsLocalToWorldDeterminantNegative() : false;
					Mesh.Type = PT_TriangleList;
					Mesh.DepthPriorityGroup = SDPG_World;
					Mesh.bCanApplyViewModeOverrides = false;
					Collector.AddMesh(ViewIndex, Mesh);

				#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
					// Render bounds
					RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
				#endif
				}
			}
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{		
		TArray<FHairGroupInstance*> Instances = StrandsVertexFactory.GetData().Instances;

		const bool bIsViewModeValid = View->Family->ViewMode == VMI_Lit;

		bool bUseCardsOrMesh = false;
		const uint32 GroupCount = Instances.Num();
		for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
		{
			const EHairGeometryType GeometryType = Instances[GroupIt]->GeometryType;
			const HairGroup& GroupData = HairGroups[GroupIt];
			bUseCardsOrMesh = bUseCardsOrMesh || GeometryType != EHairGeometryType::Cards || GeometryType != EHairGeometryType::Meshes;
		}

		FPrimitiveViewRelevance Result;
		Result.bHairStrands = bIsViewModeValid && IsShown(View);

		// Special pass for hair strands geometry (not part of the base pass, and shadowing is handlded in a custom fashion). When cards rendering is enabled we reusethe base pass
		Result.bDrawRelevance		= true; //bUseCardsOrMesh;// false;
		Result.bRenderInMainPass	= true; //bUseCardsOrMesh; // false;
		Result.bShadowRelevance		= true; // todo for cards & mesh
		Result.bDynamicRelevance	= true;

		// Selection only
		#if WITH_EDITOR
		{
			const bool bIsSelected = (IsSelected() || IsHovered());
			Result.bEditorStaticSelectionRelevance = bIsSelected;
			Result.bDrawRelevance = Result.bDrawRelevance || bIsSelected;
		}
		#endif
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		return Result;
	}

	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }

	uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }
private:

	TArray<FHairGroupInstance*> HairGroupInstances;

	uint32 ComponentId = 0;
	EHairLODSelectionType GroomLODSelection = EHairLODSelectionType::Cpu;
	FHairStrandsVertexFactory StrandsVertexFactory;
	FHairCardsVertexFactory CardsAndMeshesVertexFactory;
	FMaterialRelevance MaterialRelevance;
	UMaterialInterface* Strands_DebugMaterial = nullptr;
	int32* PredictedLODIndex = nullptr;
	struct HairGroup
	{
		UMaterialInterface* Strands_Material = nullptr;
		UMaterialInterface* Cards_Material = nullptr;
		UMaterialInterface* Meshes_Material = nullptr;
	#if RHI_RAYTRACING
		FRayTracingGeometry* RayTracingGeometry = nullptr;
	#endif
		FHairGroupPublicData* PublicData = nullptr;
		TArray<FHairLODSettings> LODSettings;
		bool bIsVsibible = true;
	};
	TArray<HairGroup> HairGroups;
};

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
// UComponent
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

UGroomComponent::UGroomComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;
	bAutoActivate = true;
	bSelectable = true;
	RegisteredSkeletalMeshComponent = nullptr;
	SkeletalPreviousPositionOffset = FVector::ZeroVector;
	bBindGroomToSkeletalMesh = false;
	InitializedResources = nullptr;
	Mobility = EComponentMobility::Movable;
	bIsGroomAssetCallbackRegistered = false;
	bIsGroomBindingAssetCallbackRegistered = false;
	SourceSkeletalMesh = nullptr; 
	NiagaraComponents.Empty();
	PhysicsAsset = nullptr;

	SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Strands_DebugMaterialRef(TEXT("/HairStrands/Materials/HairDebugMaterial.HairDebugMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Strands_DefaultMaterialRef(TEXT("/HairStrands/Materials/HairDefaultMaterial.HairDefaultMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Cards_DefaultMaterialRef(TEXT("/HairStrands/Materials/HairCardsDefaultMaterial.HairCardsDefaultMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Meshes_DefaultMaterialRef(TEXT("/HairStrands/Materials/HairMeshesDefaultMaterial.HairMeshesDefaultMaterial"));

	Strands_DebugMaterial   = Strands_DebugMaterialRef.Object;
	Strands_DefaultMaterial = Strands_DefaultMaterialRef.Object;
	Cards_DefaultMaterial = Cards_DefaultMaterialRef.Object;
	Meshes_DefaultMaterial = Meshes_DefaultMaterialRef.Object;

	AngularSpringsSystem = nullptr;
	CosseratRodsSystem = nullptr;

}

void UGroomComponent::UpdateHairGroupsDesc(bool bForceInit)
{
	if (!GroomAsset)
	{
		GroomGroupsDesc.Empty();
		return;
	}

	const uint32 GroupCount = GroomAsset->GetNumHairGroups();
	const bool bReinitOverride = bForceInit || GroupCount != GroomGroupsDesc.Num();
	if (bReinitOverride)
	{
		GroomGroupsDesc.Init(FHairGroupDesc(), GroupCount);

		// TODO add actual override mechanism so that componen get the asset settings when the asset is update and the component does not override the values
		for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
		{
			GroomGroupsDesc[GroupIt] = GetGroomGroupsDesc(GroomAsset, this, GroupIt);
			GroomGroupsDesc[GroupIt].bScatterSceneLighting = GroomAsset->HairGroupsRendering[GroupIt].AdvancedSettings.bScatterSceneLighting;
			GroomGroupsDesc[GroupIt].bUseStableRasterization = GroomAsset->HairGroupsRendering[GroupIt].AdvancedSettings.bUseStableRasterization;
		}
	}
}

void UGroomComponent::ReleaseHairSimulation()
{
	for (int32 i = 0; i < NiagaraComponents.Num(); ++i)
	{
		if (NiagaraComponents[i] && !NiagaraComponents[i]->IsBeingDestroyed())
		{
			if (GetWorld())
			{
				NiagaraComponents[i]->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
				NiagaraComponents[i]->UnregisterComponent();
			}
			NiagaraComponents[i]->DestroyComponent();
			NiagaraComponents[i] = nullptr;
		}
	}
	NiagaraComponents.Empty();
}

void UGroomComponent::UpdateHairSimulation()  
{
	const int32 NumGroups = GroomAsset ? GroomAsset->HairGroupsPhysics.Num() : 0;
	const int32 NumComponents = FMath::Max(NumGroups, NiagaraComponents.Num());

	TArray<bool> ValidComponents;
	ValidComponents.Init(false, NumComponents);

	bool NeedSpringsSolver = false;
	bool NeedRodsSolver = false;
	if (GroomAsset)
	{
		for (int32 i = 0; i < NumGroups; ++i)
		{
			ValidComponents[i] = GroomAsset->HairGroupsPhysics[i].SolverSettings.EnableSimulation && (GHairStrandsSimulation == 1);
			if (ValidComponents[i] && (GroomAsset->HairGroupsPhysics[i].SolverSettings.NiagaraSolver == EGroomNiagaraSolvers::AngularSprings))
			{
				NeedSpringsSolver = true;
			}
			if (ValidComponents[i] && (GroomAsset->HairGroupsPhysics[i].SolverSettings.NiagaraSolver == EGroomNiagaraSolvers::CosseratRods))
			{
				NeedRodsSolver = true;
			}
		}
	}
	if (NeedSpringsSolver && (AngularSpringsSystem == nullptr))
	{
		AngularSpringsSystem = LoadObject<UNiagaraSystem>(nullptr, TEXT("/HairStrands/Emitters/SimpleSpringsSystem.SimpleSpringsSystem"));
	}
	if (NeedRodsSolver && (CosseratRodsSystem == nullptr))
	{
		CosseratRodsSystem = LoadObject<UNiagaraSystem>(nullptr, TEXT("/HairStrands/Emitters/SimpleRodsSystem.SimpleRodsSystem"));
	}
	NiagaraComponents.SetNumZeroed(NumComponents);
	for (int32 i = 0; i < NumComponents; ++i)
	{
		UNiagaraComponent*& NiagaraComponent = NiagaraComponents[i];
		if (ValidComponents[i])
		{
			if (!NiagaraComponent)
			{
				NiagaraComponent = NewObject<UNiagaraComponent>(this, NAME_None, RF_Transient);
				if (GetOwner() && GetOwner()->GetWorld())
				{
					NiagaraComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
					NiagaraComponent->RegisterComponent();
				}
				else
				{
					NiagaraComponent->SetupAttachment(this);
				}
				NiagaraComponent->SetVisibleFlag(false);
			}
			if (GroomAsset->HairGroupsPhysics[i].SolverSettings.NiagaraSolver == EGroomNiagaraSolvers::AngularSprings)
			{
				NiagaraComponent->SetAsset(AngularSpringsSystem);
			}
			else if (GroomAsset->HairGroupsPhysics[i].SolverSettings.NiagaraSolver == EGroomNiagaraSolvers::CosseratRods)
			{
				NiagaraComponent->SetAsset(CosseratRodsSystem);
			}
			else 
			{
				NiagaraComponent->SetAsset(GroomAsset->HairGroupsPhysics[i].SolverSettings.CustomSystem.LoadSynchronous());
			}
			NiagaraComponent->ReinitializeSystem();
			if (NiagaraComponent->GetSystemInstance())
			{
				NiagaraComponent->GetSystemInstance()->Reset(FNiagaraSystemInstance::EResetMode::ReInit);
				NiagaraComponent->GetSystemInstance()->Reset(FNiagaraSystemInstance::EResetMode::ResetAll);
			}
		}
		else if (NiagaraComponent && !NiagaraComponent->IsBeingDestroyed())
		{
			NiagaraComponent->DeactivateImmediate();
			if (NiagaraComponent->GetSystemInstance() != nullptr)
			{
				NiagaraComponent->GetSystemInstance()->Reset(FNiagaraSystemInstance::EResetMode::ResetAll);
			}
		}
	}
	UpdateSimulatedGroups();
}

void UGroomComponent::SetGroomAsset(UGroomAsset* Asset)
{
	ReleaseResources();
	const bool bIsSameAsset = GroomAsset == Asset;
	if (Asset && Asset->IsValid())
	{
		GroomAsset = Asset;
	}
	else
	{
		GroomAsset = nullptr;
	}

	if (!UGroomBindingAsset::IsBindingAssetValid(BindingAsset, false, bValidationEnable) || !UGroomBindingAsset::IsCompatible(GroomAsset, BindingAsset, bValidationEnable))
	{
		BindingAsset = nullptr;
	}

	UpdateHairGroupsDesc(!bIsSameAsset);
	UpdateHairSimulation();
	if (!GroomAsset)
		return;
	InitResources();
}

void UGroomComponent::SetGroomAsset(UGroomAsset* Asset, UGroomBindingAsset* InBinding)
{
	ReleaseResources();
	const bool bIsSameAsset = GroomAsset == Asset;
	if (Asset && Asset->IsValid())
	{
		GroomAsset = Asset;
	}
	else
	{
		GroomAsset = nullptr;
	}
	BindingAsset = InBinding;
	if (!UGroomBindingAsset::IsBindingAssetValid(BindingAsset, false, bValidationEnable) || !UGroomBindingAsset::IsCompatible(GroomAsset, BindingAsset, bValidationEnable))
	{
		BindingAsset = nullptr;
	}

	if (BindingAsset)
	{
		bBindGroomToSkeletalMesh = true;
	}

	UpdateHairGroupsDesc(!bIsSameAsset);
	UpdateHairSimulation();
	if (!GroomAsset)
		return;
	InitResources();
}

void UGroomComponent::SetStableRasterization(bool bEnable)
{
	for (FHairGroupDesc& HairDesc : GroomGroupsDesc)
	{
		HairDesc.bUseStableRasterization = bEnable;
	}
	UpdateHairGroupsDescAndInvalidateRenderState();
}

void UGroomComponent::SetHairLengthScale(float Scale) 
{ 
	Scale = FMath::Clamp(Scale, 0.f, 1.f);
	for (FHairGroupDesc& HairDesc : GroomGroupsDesc)
	{
		HairDesc.HairClipLength = Scale * HairDesc.HairLength;
	}
	UpdateHairGroupsDescAndInvalidateRenderState();
}

void UGroomComponent::SetHairRootScale(float Scale)
{
	Scale = FMath::Clamp(Scale, 0.f, 10.f);
	for (FHairGroupDesc& HairDesc : GroomGroupsDesc)
	{
		HairDesc.HairRootScale = Scale;
	}
	UpdateHairGroupsDescAndInvalidateRenderState();
}

void UGroomComponent::SetHairWidth(float HairWidth)
{
	for (FHairGroupDesc& HairDesc : GroomGroupsDesc)
	{
		HairDesc.HairWidth = HairWidth;
	}
	UpdateHairGroupsDescAndInvalidateRenderState();
}

void UGroomComponent::SetScatterSceneLighting(bool Enable)
{
	for (FHairGroupDesc& HairDesc : GroomGroupsDesc)
	{
		HairDesc.bScatterSceneLighting = Enable;
	}
	UpdateHairGroupsDescAndInvalidateRenderState();
}

void UGroomComponent::SetForcedLOD(int32 LODIndex)
{
	if (GroomAsset)
	{
		LODIndex = FMath::Clamp(LODIndex, -1, GroomAsset->GetLODCount() - 1);
	}
	else
	{
		LODIndex = -1;
	}
	for (FHairGroupDesc& HairDesc : GroomGroupsDesc)
	{
		HairDesc.LODForcedIndex = LODIndex;
	}
	UpdateHairGroupsDescAndInvalidateRenderState();
}

int32 UGroomComponent::GetNumLODs() const 
{ 
	return GroomAsset ? GroomAsset->GetLODCount() : 0; 
}

int32 UGroomComponent::GetForcedLOD() const 
{ 
	if (GroomGroupsDesc.Num() > 0)
	{
		return GroomGroupsDesc[0].LODForcedIndex;
	}
	else
	{
		return -1;
	}
}

int32 UGroomComponent::GetDesiredSyncLOD() const
{
	return PredictedLODIndex;
}

void  UGroomComponent::SetSyncLOD(int32 InLODIndex)
{
	SetForcedLOD(InLODIndex);
}

int32 UGroomComponent::GetNumSyncLODs() const
{
	return GetNumLODs();
}

void UGroomComponent::SetBinding(bool bBind)
{
	if (bBind != bBindGroomToSkeletalMesh)
	{
		bBindGroomToSkeletalMesh = bBind;
		InitResources();
	}
}

void UGroomComponent::SetBinding(UGroomBindingAsset* InBinding)
{
	if (BindingAsset != InBinding)
	{
		const bool bIsValid = InBinding != nullptr ? UGroomBindingAsset::IsBindingAssetValid(BindingAsset, false, bValidationEnable) : true;
		if (bIsValid && UGroomBindingAsset::IsCompatible(GroomAsset, InBinding, bValidationEnable))
		{
			BindingAsset = InBinding;
			bBindGroomToSkeletalMesh = InBinding != nullptr;
			InitResources();
		}
	}
}

void UGroomComponent::UpdateHairGroupsDescAndInvalidateRenderState()
{
	UpdateHairGroupsDesc(false);

	uint32 GroupIndex = 0;
	for (FHairGroupInstance* Instance : HairGroupInstances)
	{
		Instance->Strands.Modifier  = GetGroomGroupsDesc(GroomAsset, this, GroupIndex);
#if WITH_EDITORONLY_DATA
		Instance->Debug.DebugMode = GroomAsset ? GroomAsset->GetDebugMode() : EHairStrandsDebugMode::NoneDebug;
#endif// #if WITH_EDITORONLY_DATA
		++GroupIndex;
	}
	MarkRenderStateDirty();
}

FPrimitiveSceneProxy* UGroomComponent::CreateSceneProxy()
{
	if (!GroomAsset || GroomAsset->GetNumHairGroups() == 0 || GroomAsset->HairGroupsData[0].Strands.Data.GetNumCurves() == 0 || HairGroupInstances.Num() == 0)
		return nullptr;

	return new FHairStrandsSceneProxy(this);
}


FBoxSphereBounds UGroomComponent::CalcBounds(const FTransform& InLocalToWorld) const
{
	if (GroomAsset && GroomAsset->GetNumHairGroups() > 0)
	{
		if (RegisteredSkeletalMeshComponent)
		{
			const FBox WorldSkeletalBound = RegisteredSkeletalMeshComponent->CalcBounds(InLocalToWorld).GetBox();
			return FBoxSphereBounds(WorldSkeletalBound);
		}
		else
		{
			FBox LocalBounds(EForceInit::ForceInitToZero);
			for (const FHairGroupData& GroupData : GroomAsset->HairGroupsData)
			{
				LocalBounds += GroupData.Strands.Data.BoundingBox;
			}
			return FBoxSphereBounds(LocalBounds.TransformBy(InLocalToWorld));
		}
	}
	else
	{
		return FBoxSphereBounds(EForceInit::ForceInitToZero);
	}
}

int32 UGroomComponent::GetNumMaterials() const
{
	if (GroomAsset)
	{
		return FMath::Max(GroomAsset->GetNumHairGroups(), 1);
	}
	return 1;
}

UMaterialInterface* UGroomComponent::GetMaterial(int32 ElementIndex) const
{
	UMaterialInterface* OverrideMaterial = Super::GetMaterial(ElementIndex);
	
	bool bUseHairDefaultMaterial = false;

	const ERHIFeatureLevel::Type FeatureLevel = GetScene() ? GetScene()->GetFeatureLevel() : ERHIFeatureLevel::Num;
	if (!OverrideMaterial && GroomAsset && ElementIndex < GroomAsset->GetNumHairGroups() && FeatureLevel != ERHIFeatureLevel::Num)
	{
		if (UMaterialInterface* Material = GroomAsset->HairGroupsRendering[ElementIndex].Material)
		{
			OverrideMaterial = Material;
		}
		else
		{
			bUseHairDefaultMaterial = true;
		}
	}

	if (IsHairMaterialCompatible(OverrideMaterial, FeatureLevel) != EHairMaterialCompatibility::Valid)
	{
		bUseHairDefaultMaterial = true;
	}

	if (bUseHairDefaultMaterial)
	{
		OverrideMaterial = Strands_DefaultMaterial;
	}

	return OverrideMaterial;
}

FHairStrandsDatas* UGroomComponent::GetGuideStrandsDatas(uint32 GroupIndex)
{
	if (!GroomAsset || GroupIndex >= uint32(GroomAsset->GetNumHairGroups()))
	{
		return nullptr;
	}

	return &GroomAsset->HairGroupsData[GroupIndex].Guides.Data;
}

FHairStrandsRestResource* UGroomComponent::GetGuideStrandsRestResource(uint32 GroupIndex)
{
	if (!GroomAsset || GroupIndex >= uint32(GroomAsset->GetNumHairGroups()))
	{
		return nullptr;
	}

	return GroomAsset->HairGroupsData[GroupIndex].Guides.RestResource;
}

FHairStrandsDeformedResource* UGroomComponent::GetGuideStrandsDeformedResource(uint32 GroupIndex)
{
	if (GroupIndex >= uint32(HairGroupInstances.Num()))
	{
		return nullptr;
	}

	return HairGroupInstances[GroupIndex]->Guides.DeformedResource;
}

FHairStrandsRestRootResource* UGroomComponent::GetGuideStrandsRestRootResource(uint32 GroupIndex)
{
	if (GroupIndex >= uint32(HairGroupInstances.Num()))
	{
		return nullptr;
	}

	return HairGroupInstances[GroupIndex]->Guides.RestRootResource;
}

FHairStrandsDeformedRootResource* UGroomComponent::GetGuideStrandsDeformedRootResource(uint32 GroupIndex)
{
	if (GroupIndex >= uint32(HairGroupInstances.Num()))
	{
		return nullptr;
	}

	return HairGroupInstances[GroupIndex]->Guides.DeformedRootResource;
}

EWorldType::Type UGroomComponent::GetWorldType() const
{
	EWorldType::Type WorldType = GetWorld() ? EWorldType::Type(GetWorld()->WorldType) : EWorldType::None;
	return WorldType == EWorldType::Inactive ? EWorldType::Editor : WorldType;
}

void UGroomComponent::UpdateSimulatedGroups()
{
	if (HairGroupInstances.Num()>0)
	{
		const uint32 Id = ComponentId.PrimIDValue;
		const EWorldType::Type WorldType = GetWorldType();
		
		TArray<FHairGroupInstance*> LocalInstances = HairGroupInstances;
		UGroomAsset* LocalGroomAsset = GroomAsset;
		UGroomBindingAsset* LocalBindingAsset = BindingAsset;
		ENQUEUE_RENDER_COMMAND(FHairStrandsTick_UEnableSimulatedGroups)(
			[LocalInstances, LocalGroomAsset, LocalBindingAsset, Id, WorldType](FRHICommandListImmediate& RHICmdList)
		{
			int32 GroupIt = 0;
			for (FHairGroupInstance* Instance : LocalInstances)
			{
				const bool bIsSimulationEnable = (LocalGroomAsset && GroupIt < LocalGroomAsset->HairGroupsPhysics.Num()) ? 
					(LocalGroomAsset->HairGroupsPhysics[GroupIt].SolverSettings.EnableSimulation  && (GHairStrandsSimulation == 1)): false;
				const bool bHasGlobalInterpolation = LocalBindingAsset && LocalGroomAsset && LocalGroomAsset->EnableGlobalInterpolation;
				Instance->Strands.HairInterpolationType =
					(LocalGroomAsset && LocalGroomAsset->HairInterpolationType == EGroomInterpolationType::RigidTransform) ? 0 :
					(LocalGroomAsset && LocalGroomAsset->HairInterpolationType == EGroomInterpolationType::OffsetTransform) ? 1 :
					(LocalGroomAsset && LocalGroomAsset->HairInterpolationType == EGroomInterpolationType::SmoothTransform) ? 2 : 0;
				Instance->Guides.bIsSimulationEnable = bIsSimulationEnable;
				Instance->Guides.bHasGlobalInterpolation = bHasGlobalInterpolation;
				++GroupIt;
			}
		});
	}
}

void UGroomComponent::OnChildDetached(USceneComponent* ChildComponent)
{}

void UGroomComponent::OnChildAttached(USceneComponent* ChildComponent)
{

}

void UGroomComponent::InitResources(bool bIsBindingReloading)
{
	ReleaseResources();
	bInitSimulation = true;
	bResetSimulation = true;

	UpdateHairGroupsDesc(false);

	if (!GroomAsset || GroomAsset->GetNumHairGroups() == 0)
		return;

	InitializedResources = GroomAsset;

	const FPrimitiveComponentId LocalComponentId = ComponentId;
	const EWorldType::Type WorldType = GetWorldType();

	// Insure the ticking of the Groom component always happens after the skeletalMeshComponent.
	USkeletalMeshComponent* SkeletalMeshComponent = bBindGroomToSkeletalMesh && GetAttachParent() ? Cast<USkeletalMeshComponent>(GetAttachParent()) : nullptr;
	const bool bHasValidSectionCount = SkeletalMeshComponent && SkeletalMeshComponent->GetNumMaterials() < int32(GetHairStrandsMaxSectionCount());
	const bool bHasValidSketalMesh = SkeletalMeshComponent && SkeletalMeshComponent->GetSkeletalMeshRenderData() && bHasValidSectionCount;

	// Report warning if the skeletal section count is larger than the supported count
	if (bBindGroomToSkeletalMesh && SkeletalMeshComponent && !bHasValidSectionCount)
	{
		FString Name = "";
		if (GetOwner())
		{
			Name += GetOwner()->GetName() + "/";
		}
		Name += GetName() + "/" + GroomAsset->GetName();

		UE_LOG(LogHairStrands, Warning, TEXT("[Groom] %s - Groom is bound to a skeletal mesh which has too many sections (%d), which is higher than the maximum supported for hair binding (%d). The groom binding will be disbled on this component."), *Name, SkeletalMeshComponent->GetNumMaterials(), GetHairStrandsMaxSectionCount());
	}

	USkeletalMeshComponent* SkelMesh = nullptr;
	if (bHasValidSketalMesh)
	{
		RegisteredSkeletalMeshComponent = SkeletalMeshComponent;
		SkelMesh = SkeletalMeshComponent;
		AddTickPrerequisiteComponent(SkeletalMeshComponent);
	}

	const bool bIsBindingCompatible = 
		UGroomBindingAsset::IsCompatible(SkeletalMeshComponent ? SkeletalMeshComponent->SkeletalMesh : nullptr, BindingAsset, bValidationEnable) &&
		UGroomBindingAsset::IsCompatible(GroomAsset, BindingAsset, bValidationEnable) &&
		UGroomBindingAsset::IsBindingAssetValid(BindingAsset, bIsBindingReloading, bValidationEnable);

	FTransform HairLocalToWorld = GetComponentTransform();
	FTransform SkinLocalToWorld = bBindGroomToSkeletalMesh && SkeletalMeshComponent ? SkeletalMeshComponent->GetComponentTransform() : FTransform::Identity;
	
	for (int32 GroupIt = 0, GroupCount = GroomAsset->HairGroupsData.Num(); GroupIt < GroupCount; ++GroupIt)
	{
		FHairGroupInstance* HairGroupInstance = new FHairGroupInstance();
		HairGroupInstances.Add(HairGroupInstance);
		HairGroupInstance->WorldType = WorldType;
		HairGroupInstance->Debug.ComponentId = ComponentId.PrimIDValue;
		HairGroupInstance->Debug.GroupIndex = GroupIt;
		HairGroupInstance->Debug.GroupCount = GroupCount;
		HairGroupInstance->Debug.GroomAssetName = GroomAsset->GetName();
		HairGroupInstance->Debug.SkeletalComponent = bHasValidSketalMesh ? SkeletalMeshComponent : nullptr;
		if (bHasValidSketalMesh)
		{
			HairGroupInstance->Debug.SkeletalComponentName = SkeletalMeshComponent->GetPathName();
		}
		HairGroupInstance->GeometryType = EHairGeometryType::Strands;

		FHairGroupData& GroupData = GroomAsset->HairGroupsData[GroupIt];
		if (!GroupData.Strands.RestResource)
			return;

		const uint32 SkeletalLODCount = bHasValidSketalMesh ? SkeletalMeshComponent->GetNumLODs() : 0;

		const uint32 HairInterpolationType =
			(GroomAsset && GroomAsset->HairInterpolationType == EGroomInterpolationType::RigidTransform) ? 0 :
			(GroomAsset && GroomAsset->HairInterpolationType == EGroomInterpolationType::OffsetTransform) ? 1 :
			(GroomAsset && GroomAsset->HairInterpolationType == EGroomInterpolationType::SmoothTransform) ? 2 : 0;

		// Sim data
		{
			HairGroupInstance->Guides.Data = &GroupData.Guides.Data;

			if (bHasValidSketalMesh)
			{
				if (BindingAsset && bIsBindingCompatible)
				{
					check(GroupIt < BindingAsset->HairGroupResources.Num());
					check(SkeletalMeshComponent->GetNumLODs() == BindingAsset->HairGroupResources[GroupIt].SimRootResources->RootData.MeshProjectionLODs.Num());

					HairGroupInstance->Guides.bOwnRootResourceAllocation = false;
					HairGroupInstance->Guides.RestRootResource = BindingAsset->HairGroupResources[GroupIt].SimRootResources;
				}
				else
				{
					if (SkeletalLODCount > 0)
					{
						HairGroupInstance->Guides.bOwnRootResourceAllocation = true;
						TArray<uint32> NumSamples; NumSamples.Init(0, SkeletalLODCount);
						HairGroupInstance->Guides.RestRootResource = new FHairStrandsRestRootResource(&GroupData.Guides.Data, SkeletalLODCount, NumSamples);
						
						BeginInitResource(HairGroupInstance->Guides.RestRootResource);
					}
				}

				HairGroupInstance->Guides.DeformedRootResource = new FHairStrandsDeformedRootResource(HairGroupInstance->Guides.RestRootResource);
				BeginInitResource(HairGroupInstance->Guides.DeformedRootResource);
			}
			HairGroupInstance->Guides.RestResource = GroupData.Guides.RestResource;

			HairGroupInstance->Guides.DeformedResource = new FHairStrandsDeformedResource(GroupData.Guides.Data.RenderData, true);
			BeginInitResource(HairGroupInstance->Guides.DeformedResource);

			HairGroupInstance->Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::EFrameType::Current)  = HairGroupInstance->Guides.RestResource->PositionOffset;
			HairGroupInstance->Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::EFrameType::Previous) = HairGroupInstance->Guides.RestResource->PositionOffset;

			HairGroupInstance->Guides.bIsSimulationEnable =
				(GroomAsset && GroupIt < GroomAsset->HairGroupsPhysics.Num()) ?
				(GroomAsset->HairGroupsPhysics[GroupIt].SolverSettings.EnableSimulation && (GHairStrandsSimulation ==1)) :
				false;

			HairGroupInstance->Guides.bHasGlobalInterpolation = BindingAsset && GroomAsset && GroomAsset->EnableGlobalInterpolation;
		}

		// Strands data/resources
		{
			HairGroupInstance->Strands.Data = &GroupData.Strands.Data;
			HairGroupInstance->Strands.InterpolationData = &GroupData.Strands.InterpolationData;
			HairGroupInstance->Strands.InterpolationResource = GroupData.Strands.InterpolationResource;

			#if RHI_RAYTRACING
			if (IsHairRayTracingEnabled())
			{
				HairGroupInstance->Strands.RenRaytracingResource = new FHairStrandsRaytracingResource(GroupData.Strands.Data);
				BeginInitResource(HairGroupInstance->Strands.RenRaytracingResource);
			}
			#endif

			if (bHasValidSketalMesh)
			{
				if (BindingAsset && bIsBindingCompatible)
				{
					check(GroupIt < BindingAsset->HairGroupResources.Num());
					check(SkeletalMeshComponent->GetNumLODs() == BindingAsset->HairGroupResources[GroupIt].RenRootResources->RootData.MeshProjectionLODs.Num());

					HairGroupInstance->Strands.bOwnRootResourceAllocation = false;
					HairGroupInstance->Strands.RestRootResource = BindingAsset->HairGroupResources[GroupIt].RenRootResources;
				}
				else
				{
					if (SkeletalLODCount > 0)
					{
						HairGroupInstance->Strands.bOwnRootResourceAllocation = true;
						TArray<uint32> NumSamples; NumSamples.Init(0, SkeletalLODCount);
						HairGroupInstance->Strands.RestRootResource = new FHairStrandsRestRootResource(&GroupData.Strands.Data, SkeletalLODCount, NumSamples);
						BeginInitResource(HairGroupInstance->Strands.RestRootResource);
					}
				}

				HairGroupInstance->Strands.DeformedRootResource = new FHairStrandsDeformedRootResource(HairGroupInstance->Strands.RestRootResource);
				BeginInitResource(HairGroupInstance->Strands.DeformedRootResource);
			}
		
			HairGroupInstance->Strands.RestResource = GroupData.Strands.RestResource;
			HairGroupInstance->Strands.DeformedResource = new FHairStrandsDeformedResource(GroupData.Strands.Data.RenderData, false);
			BeginInitResource(HairGroupInstance->Strands.DeformedResource);
			HairGroupInstance->Strands.ClusterCullingResource = GroupData.Strands.ClusterCullingResource;

			const uint32 HairControlPointCount = GroupData.Strands.Data.GetNumPoints();
			HairGroupInstance->HairGroupPublicData = new FHairGroupPublicData(GroupIt, HairGroupInstance->Strands.ClusterCullingResource->ClusterCount, HairControlPointCount);
			HairGroupInstance->HairGroupPublicData->SetLODScreenSizes(GroupData.Strands.ClusterCullingResource->CPULODScreenSize);
			HairGroupInstance->HairGroupPublicData->SetLODVisibilities(GroupData.Strands.ClusterCullingResource->LODVisibility);
			BeginInitResource(HairGroupInstance->HairGroupPublicData);

			// Initialize deformed position relative position offset to the rest pose offset
			HairGroupInstance->Strands.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::EFrameType::Current)  = HairGroupInstance->Strands.RestResource->PositionOffset;
			HairGroupInstance->Strands.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::EFrameType::Previous) = HairGroupInstance->Strands.RestResource->PositionOffset;

			check(GroupIt < GroomGroupsDesc.Num());
			HairGroupInstance->Strands.Modifier = GetGroomGroupsDesc(GroomAsset, this, GroupIt);

			HairGroupInstance->Strands.HairInterpolationType = HairInterpolationType;
		}

		// Cards resources
		for (FHairGroupData::FCards::FLOD& LOD : GroupData.Cards.LODs)
		{
			FHairGroupInstance::FCards::FLOD& InstanceLOD = HairGroupInstance->Cards.LODs.AddDefaulted_GetRef();
			if (LOD.IsValid())
			{
				InstanceLOD.Data = &LOD.Data;
				InstanceLOD.RestResource = LOD.RestResource;
				InstanceLOD.InterpolationData = &LOD.InterpolationData;
				InstanceLOD.InterpolationResource = LOD.InterpolationResource;
				InstanceLOD.DeformedResource = new FHairCardsDeformedResource(LOD.Data.RenderData, false);
				BeginInitResource(InstanceLOD.DeformedResource);

				// Strands data/resources
				{
					InstanceLOD.Guides.Data = &LOD.Guides.Data;
					InstanceLOD.Guides.InterpolationData = &LOD.Guides.InterpolationData;
					InstanceLOD.Guides.InterpolationResource = LOD.Guides.InterpolationResource;

					// #hair_todo: add hair card skin attachement/projection
					//if (bHasValidSketalMesh)
					//{
					//	if (BindingAsset && bIsBindingCompatible)
					//	{
					//		check(GroupIt < BindingAsset->HairGroupResources.Num());
					//		check(SkeletalMeshComponent->GetNumLODs() == BindingAsset->HairGroupResources[GroupIt].RenRootResources->RootData.MeshProjectionLODs.Num());
					//
					//		HairGroupInstance->Strands.bOwnRootResourceAllocation = false;
					//		HairGroupInstance->Strands.RestRootResource = BindingAsset->HairGroupResources[GroupIt].RenRootResources;
					//	}
					//	else
					//	{
					//		if (SkeletalLODCount > 0)
					//		{
					//			HairGroupInstance->Strands.bOwnRootResourceAllocation = true;
					//			TArray<uint32> NumSamples; NumSamples.Init(0, SkeletalLODCount);
					//			HairGroupInstance->Strands.RestRootResource = new FHairStrandsRestRootResource(&LOD.Guides.Data, SkeletalLODCount, NumSamples);
					//			BeginInitResource(HairGroupInstance->Strands.RestRootResource);
					//		}
					//	}
					//
					//	HairGroupInstance->Strands.DeformedRootResource = new FHairStrandsDeformedRootResource(HairGroupInstance->Strands.RestRootResource);
					//	BeginInitResource(HairGroupInstance->Strands.DeformedRootResource);
					//}

					InstanceLOD.Guides.RestResource = LOD.Guides.RestResource;
					InstanceLOD.Guides.DeformedResource = new FHairStrandsDeformedResource(LOD.Guides.Data.RenderData, false);
					BeginInitResource(InstanceLOD.Guides.DeformedResource);

					// Initialize deformed position relative position offset to the rest pose offset
					InstanceLOD.Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::EFrameType::Current) = InstanceLOD.Guides.RestResource->PositionOffset;
					InstanceLOD.Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::EFrameType::Previous) = InstanceLOD.Guides.RestResource->PositionOffset;

					InstanceLOD.Guides.HairInterpolationType = HairInterpolationType;
				}
			}
		}

		// Meshes resources
		for (FHairGroupData::FMeshes::FLOD& LOD : GroupData.Meshes.LODs)
		{
			FHairGroupInstance::FMeshes::FLOD& InstanceLOD = HairGroupInstance->Meshes.LODs.AddDefaulted_GetRef();
			if (LOD.IsValid())
			{
				InstanceLOD.Data = &LOD.Data;
				InstanceLOD.RestResource = LOD.RestResource;
			}
		}
	}

	// Does not run projection code when running with null RHI as this is not needed, and will crash as the skeletal GPU resources are not created
	if (GUsingNullRHI)
	{
		return;
	}

	USkeletalMesh* InSourceSkeletalMesh = SourceSkeletalMesh;
	const bool bRunMeshProjection = bHasValidSketalMesh && (!BindingAsset || !bIsBindingCompatible);
	TArray<FHairGroupInstance*> LocalInstances = HairGroupInstances;
	ENQUEUE_RENDER_COMMAND(FHairStrandsBuffers)(
		[
			SkelMesh,
			LocalInstances,
			HairLocalToWorld,
			bRunMeshProjection,
			SkeletalMeshComponent,
			InSourceSkeletalMesh
		]
		(FRHICommandListImmediate& RHICmdList)
	{
		const uint32 GroupCount = LocalInstances.Num();
		if (bRunMeshProjection)
		{				
			FSkeletalMeshRenderData* TargetRenderData = SkeletalMeshComponent ? SkeletalMeshComponent->GetSkeletalMeshRenderData() : nullptr;
			FHairStrandsProjectionMeshData TargetMeshData = ExtractMeshData(TargetRenderData);

			// Create mapping between the source & target using their UV
			// The lifetime of 'TransferredPositions' needs to encompass RunProjection
			TArray<FRWBuffer> TransferredPositions;
			
			FRDGBuilder GraphBuilder(RHICmdList);
			FHairStrandsProjectionMeshData SourceMeshData;
			if (FSkeletalMeshRenderData* SourceRenderData = InSourceSkeletalMesh ? InSourceSkeletalMesh->GetResourceForRendering() : nullptr)
			{
				SourceMeshData = ExtractMeshData(SourceRenderData);
				FGroomBindingBuilder::TransferMesh(
					GraphBuilder,
					SourceMeshData,
					TargetMeshData,
					TransferredPositions);

				const uint32 MeshLODCount = TargetMeshData.LODs.Num();
				for (uint32 MeshLODIndex = 0; MeshLODIndex < MeshLODCount; ++MeshLODIndex)
				{
					for (FHairStrandsProjectionMeshData::Section& Section : TargetMeshData.LODs[MeshLODIndex].Sections)
					{
						Section.PositionBuffer = TransferredPositions[MeshLODIndex].SRV;
					}
				}
			}

			for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
			{
				FHairGroupInstance* HairGroupInstance = LocalInstances[GroupIt];
				HairGroupInstance->Debug.SourceMeshData = SourceMeshData;
				HairGroupInstance->Debug.TargetMeshData = TargetMeshData;
				HairGroupInstance->Debug.TransferredPositions = TransferredPositions;

				// The offset is based on the center of the skeletal mesh (which is computed based on the physics capsules/boxes/...)
				FGroomBindingBuilder::ProjectStrands(
					GraphBuilder,
					HairLocalToWorld,
					TargetMeshData,
					HairGroupInstance->Strands.RestRootResource,
					HairGroupInstance->Guides.RestRootResource);
			}
			GraphBuilder.Execute();

		}

		for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
		{
			FHairGroupInstance* HairGroupInstance = LocalInstances[GroupIt];
			RegisterHairStrands(HairGroupInstance);
		}
	});
}

template<typename T>
void InternalResourceRelease(T*& In)
{
	if (In)
	{
		In->ReleaseResource();
		delete In;
		In = nullptr;
	}
}

void UGroomComponent::ReleaseResources()
{
	// Unregister component interpolation resources
	const FPrimitiveComponentId LocalComponentId = ComponentId;
	const uint32 Id = LocalComponentId.PrimIDValue;
	ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyInit)(
		[Id](FRHICommandListImmediate& RHICmdList)
	{
		UnregisterHairStrands(Id);
	});

	InitializedResources = nullptr;

	for (FHairGroupInstance* Instance : HairGroupInstances)
	{
		FHairGroupInstance* LocalInstance = Instance;
		ENQUEUE_RENDER_COMMAND(FHairStrandsBuffers)(
		[LocalInstance](FRHICommandListImmediate& RHICmdList)
		{
			// Release the root resources only if they have been created internally (vs. being created by external asset)
			if (LocalInstance->Strands.bOwnRootResourceAllocation)
			{
				InternalResourceRelease(LocalInstance->Strands.RestRootResource);
				InternalResourceRelease(LocalInstance->Guides.RestRootResource);
			}

			InternalResourceRelease(LocalInstance->Strands.DeformedRootResource);
			InternalResourceRelease(LocalInstance->Guides.DeformedRootResource);

			InternalResourceRelease(LocalInstance->Strands.DeformedResource);
			InternalResourceRelease(LocalInstance->Guides.DeformedResource);

			#if RHI_RAYTRACING
			InternalResourceRelease(LocalInstance->Strands.RenRaytracingResource);
			#endif

			InternalResourceRelease(LocalInstance->HairGroupPublicData);
			delete LocalInstance;
		});
	}
	HairGroupInstances.Empty();

	// Insure the ticking of the Groom component always happens after the skeletalMeshComponent.
	if (RegisteredSkeletalMeshComponent)
	{
		RemoveTickPrerequisiteComponent(RegisteredSkeletalMeshComponent);
	}
	SkeletalPreviousPositionOffset = FVector::ZeroVector;
	RegisteredSkeletalMeshComponent = nullptr;

	MarkRenderStateDirty();
}

void UGroomComponent::PostLoad()
{
	Super::PostLoad();

	if (GroomAsset)
	{
		// Make sure that the asset initialized its resources first since the component needs them to initialize its own resources
		GroomAsset->ConditionalPostLoad();
	}

	if (GroomAsset && !GroomAsset->IsValid())
	{
		GroomAsset = nullptr;
	}

	if (BindingAsset)
	{
		// Make sure that the asset initialized its resources first since the component needs them to initialize its own resources
		BindingAsset->ConditionalPostLoad();
	}
	InitResources();

#if WITH_EDITOR
	if (GroomAsset && !bIsGroomAssetCallbackRegistered)
	{
		GroomAsset->GetOnGroomAssetChanged().AddUObject(this, &UGroomComponent::Invalidate);
		GroomAsset->GetOnGroomAssetResourcesChanged().AddUObject(this, &UGroomComponent::InvalidateAndRecreate);
		bIsGroomAssetCallbackRegistered = true;
	}

	if (BindingAsset && !bIsGroomBindingAssetCallbackRegistered)
	{
		BindingAsset->GetOnGroomBindingAssetChanged().AddUObject(this, &UGroomComponent::InvalidateAndRecreate);
		bIsGroomBindingAssetCallbackRegistered = true;
	}

	// Do not validate the groom yet as the component count be loaded, but material/binding & co will be set later on
	// ValidateMaterials(false);
#endif
}

#if WITH_EDITOR
void UGroomComponent::Invalidate()
{
	UpdateHairSimulation();
	UpdateHairGroupsDescAndInvalidateRenderState();
	ValidateMaterials(false);
}

void UGroomComponent::InvalidateAndRecreate()
{
	InitResources(true);
	MarkRenderStateDirty();
}
#endif

void UGroomComponent::OnRegister()
{
	Super::OnRegister();
	UpdateHairSimulation();

	const bool bNeedInitialization = !InitializedResources || InitializedResources != GroomAsset;
	if (bNeedInitialization)
	{
		InitResources();
	}
	else
	{
		UpdateHairGroupsDescAndInvalidateRenderState();
	}

	// Insure the ticking of the Groom component always happens after the skeletalMeshComponent.
	USkeletalMeshComponent* SkeletalMeshComponent = GetAttachParent() ? Cast<USkeletalMeshComponent>(GetAttachParent()) : nullptr;

	const EWorldType::Type WorldType = GetWorldType();
	TArray<FHairGroupInstance*> LocalHairGroupInstances = HairGroupInstances;
	ENQUEUE_RENDER_COMMAND(FHairStrandsRegister)(
		[WorldType, LocalHairGroupInstances](FRHICommandListImmediate& RHICmdList)
	{
		for (FHairGroupInstance* Instance : LocalHairGroupInstances)
		{
			Instance->WorldType = WorldType;
		}
	});
}

void UGroomComponent::OnUnregister()
{
	Super::OnUnregister();
	ReleaseHairSimulation();
}

void UGroomComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	ReleaseResources();

#if WITH_EDITOR
	if (bIsGroomAssetCallbackRegistered && GroomAsset)
	{
		GroomAsset->GetOnGroomAssetChanged().RemoveAll(this);
		GroomAsset->GetOnGroomAssetResourcesChanged().RemoveAll(this);
		bIsGroomAssetCallbackRegistered = false;
	}
	if (bIsGroomBindingAssetCallbackRegistered && BindingAsset)
	{
		BindingAsset->GetOnGroomBindingAssetChanged().RemoveAll(this);
		bIsGroomBindingAssetCallbackRegistered = false;
	}
#endif

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UGroomComponent::OnAttachmentChanged()
{
	Super::OnAttachmentChanged();
	if (GroomAsset && !IsBeingDestroyed() && HasBeenCreated())
	{
		FlushRenderingCommands();
		InitResources();
	}
}

void UGroomComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);	
	
	const EWorldType::Type WorldType = GetWorldType();
	const uint32 Id = ComponentId.PrimIDValue;
	const ERHIFeatureLevel::Type FeatureLevel = GetWorld() ? ERHIFeatureLevel::Type(GetWorld()->FeatureLevel) : ERHIFeatureLevel::Num;


	bResetSimulation = bInitSimulation;
	if (!bInitSimulation)
	{
		if (USkeletalMeshComponent* ParentComp = Cast<USkeletalMeshComponent>(GetAttachParent()))
		{
			if (ParentComp->GetNumBones() > 0)
			{
				const int32 BoneIndex = FMath::Min(1, ParentComp->GetNumBones() - 1);
				const FMatrix NextBoneMatrix = ParentComp->GetBoneMatrix(BoneIndex);

				const float BoneDistance = FVector::DistSquared(PrevBoneMatrix.GetOrigin(), NextBoneMatrix.GetOrigin());
				if (ParentComp->GetTeleportDistanceThreshold() > 0.0 && BoneDistance >
					ParentComp->GetTeleportDistanceThreshold() * ParentComp->GetTeleportDistanceThreshold())
				{
					bResetSimulation = true;
				}
				PrevBoneMatrix = NextBoneMatrix;
			}
		}
	}
	bInitSimulation = false;

	if (HairGroupInstances.Num() == 0)
	{
		return;
	}


	USkeletalMeshComponent* SkeletalMeshComponent = RegisteredSkeletalMeshComponent;
	{
		FVector OutHairPositionOffset = FVector::ZeroVector;
		FVector OutHairPreviousPositionOffset = FVector::ZeroVector;
		bool bUpdatePositionOffset = SkeletalMeshComponent != nullptr;
		if (bUpdatePositionOffset)
		{
			// The offset is based on the center of the skeletal mesh (which is computed based on the physics capsules/boxes/...)
			// For skinned mesh update the relative center of hair positions after deformation
			FVector MeshPositionOffset = SkeletalMeshComponent->CalcBounds(FTransform::Identity).GetBox().GetCenter();

			OutHairPositionOffset = MeshPositionOffset;
			OutHairPreviousPositionOffset = SkeletalPreviousPositionOffset;

			// First frame will be wrong ... 
			SkeletalPreviousPositionOffset = OutHairPositionOffset;
		}

		TArray<FHairGroupInstance*> LocalInstances = HairGroupInstances;
		ENQUEUE_RENDER_COMMAND(FHairStrandsTick_OutHairPositionOffsetUpdate)(
			[OutHairPositionOffset, OutHairPreviousPositionOffset, bUpdatePositionOffset, LocalInstances](FRHICommandListImmediate& RHICmdList)
		{
			// Update deformed (sim/render) hair position offsets. This is used by Niagara.
			for (FHairGroupInstance* Instance : LocalInstances)
			{				
				if (Instance->Strands.DeformedResource) { Instance->Strands.DeformedResource->SwapBuffer(); }
				if (Instance->Guides.DeformedResource) { Instance->Guides.DeformedResource->SwapBuffer(); }

				const int32 IntLODIndex = Instance->HairGroupPublicData->GetLODIndex();
				if (Instance->Cards.IsValid(IntLODIndex))
				{
					Instance->Cards.LODs[IntLODIndex].DeformedResource->SwapBuffer();
					Instance->Cards.LODs[IntLODIndex].Guides.DeformedResource->SwapBuffer();
				}

				if (bUpdatePositionOffset)
				{
					if (Instance->Strands.DeformedResource)
					{
						Instance->Strands.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current) = OutHairPositionOffset;
						Instance->Strands.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Previous) = OutHairPreviousPositionOffset;
					}

					if (Instance->Guides.DeformedResource)
					{
						Instance->Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current) = OutHairPositionOffset;
						Instance->Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Previous) = OutHairPreviousPositionOffset;
					}

					if (Instance->Cards.IsValid(IntLODIndex))
					{
						Instance->Cards.LODs[IntLODIndex].Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current)  = OutHairPositionOffset;
						Instance->Cards.LODs[IntLODIndex].Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Previous) = OutHairPreviousPositionOffset;
					}
				}
			}
		});


		if (SkeletalMeshComponent)
		{
			// When a skeletal object with projection is enabled, activate the refresh of the bounding box to 
			// insure the component/proxy bounding box always lies onto the actual skinned mesh
			MarkRenderTransformDirty();
		}
	}

	const FTransform SkinLocalToWorld = SkeletalMeshComponent ? SkeletalMeshComponent->GetComponentTransform() : FTransform();
	const FTransform HairLocalToWorld = GetComponentTransform();
	TArray<FHairGroupInstance*> LocalHairGroupInstances = HairGroupInstances;
	ENQUEUE_RENDER_COMMAND(FHairStrandsTick_TransformUpdate)(
		[Id, WorldType, HairLocalToWorld, SkinLocalToWorld, FeatureLevel, LocalHairGroupInstances](FRHICommandListImmediate& RHICmdList)
	{		
		if (ERHIFeatureLevel::Num == FeatureLevel)
			return;

		for (FHairGroupInstance* Instance : LocalHairGroupInstances)
		{
			Instance->WorldType = WorldType;
			Instance->LocalToWorld = HairLocalToWorld;
			Instance->Debug.SkeletalLocalToWorld = SkinLocalToWorld;
		}
	});
}

void UGroomComponent::SendRenderTransform_Concurrent()
{
	if (RegisteredSkeletalMeshComponent)
	{
		if (ShouldComponentAddToScene() && ShouldRender())
		{
			GetWorld()->Scene->UpdatePrimitiveTransform(this);
		}
	}

	Super::SendRenderTransform_Concurrent();
}

void UGroomComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
#if WITH_EDITOR
	UMeshComponent::GetUsedMaterials(OutMaterials, bGetDebugMaterials);
	if (bGetDebugMaterials)
	{
		OutMaterials.Add(Strands_DebugMaterial);
		if (IsHairCardsEnable())
		{
			//OutMaterials.Add(Cards_DebugMaterial);
		}
		if (IsHairMeshesEnable())
		{
			//OutMaterials.Add(Meshes_DebugMaterial);
		}
	}

	const ERHIFeatureLevel::Type FeatureLevel = GetScene() ? GetScene()->GetFeatureLevel() : ERHIFeatureLevel::Num;
	bool bRegisterDefaultMaterial = false;
	for (UMaterialInterface* Material : OutMaterials)
	{
		if (IsHairMaterialCompatible(Material, FeatureLevel) != EHairMaterialCompatibility::Valid)
		{
			bRegisterDefaultMaterial = true;
			break;
		}
	}

	if (GroomAsset && IsHairCardsEnable())
	{
		for (const FHairGroupsCardsSourceDescription& Cards : GroomAsset->HairGroupsCards)
		{		
			OutMaterials.Add(Cards.Material);
		}
	}

	if (GroomAsset && IsHairMeshesEnable())
	{
		for (const FHairGroupsMeshesSourceDescription& Meshes : GroomAsset->HairGroupsMeshes)
		{
			OutMaterials.Add(Meshes.Material);
		}
	}

	if (bRegisterDefaultMaterial)
	{
		OutMaterials.Add(Strands_DefaultMaterial);		
	}

	if (IsHairCardsEnable())
	{
		OutMaterials.Add(Cards_DefaultMaterial);
	}
	if (IsHairMeshesEnable())
	{
		OutMaterials.Add(Meshes_DefaultMaterial);
	}
#endif
}

#if WITH_EDITOR
void UGroomComponent::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	const FName PropertyName = PropertyAboutToChange ? PropertyAboutToChange->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UGroomComponent, GroomAsset))
	{
		// Remove the callback on the GroomAsset about to be replaced
		if (bIsGroomAssetCallbackRegistered && GroomAsset)
		{
			GroomAsset->GetOnGroomAssetChanged().RemoveAll(this); 
			GroomAsset->GetOnGroomAssetResourcesChanged().RemoveAll(this);
		}
		bIsGroomAssetCallbackRegistered = false;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UGroomComponent, BindingAsset))
	{
		// Remove the callback on the GroomAsset about to be replaced
		if (bIsGroomBindingAssetCallbackRegistered && BindingAsset)
		{
			BindingAsset->GetOnGroomBindingAssetChanged().RemoveAll(this);
		}
		bIsGroomBindingAssetCallbackRegistered = false;
	}
}

void UGroomComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) 
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	const FName PropertyName = PropertyThatChanged ? PropertyThatChanged->GetFName() : NAME_None;

	//  Init/release resources when setting the GroomAsset (or undoing)
	const bool bAssetChanged = PropertyName == GET_MEMBER_NAME_CHECKED(UGroomComponent, GroomAsset);
	const bool bSourceSkeletalMeshChanged = PropertyName == GET_MEMBER_NAME_CHECKED(UGroomComponent, SourceSkeletalMesh);
	const bool bBindingAssetChanged = PropertyName == GET_MEMBER_NAME_CHECKED(UGroomComponent, BindingAsset);
	const bool bBindToSkeletalChanged = PropertyName == GET_MEMBER_NAME_CHECKED(UGroomComponent, bBindGroomToSkeletalMesh);
	const bool bIsBindingCompatible = UGroomBindingAsset::IsCompatible(GroomAsset, BindingAsset, bValidationEnable);
	if (!bIsBindingCompatible || !UGroomBindingAsset::IsBindingAssetValid(BindingAsset, false, bValidationEnable))
	{
		BindingAsset = nullptr;
	}
	if (BindingAsset)
	{
		bBindGroomToSkeletalMesh = true;
	}

	if (GroomAsset && !GroomAsset->IsValid())
	{
		GroomAsset = nullptr;
	}

	bool bIsEventProcess = false;

	const bool bRecreateResources = bAssetChanged || bBindingAssetChanged || PropertyThatChanged == nullptr || bBindToSkeletalChanged || bSourceSkeletalMeshChanged;
	if (bRecreateResources)
	{
		// Release the resources before Super::PostEditChangeProperty so that they get
		// re-initialized in OnRegister
		ReleaseResources();
		bIsEventProcess = true;
	}

#if WITH_EDITOR
	if (bAssetChanged)
	{
		if (GroomAsset)
		{
			// Set the callback on the new GroomAsset being assigned
			GroomAsset->GetOnGroomAssetChanged().AddUObject(this, &UGroomComponent::Invalidate);
			GroomAsset->GetOnGroomAssetResourcesChanged().AddUObject(this, &UGroomComponent::Invalidate);
			bIsGroomAssetCallbackRegistered = true;
		}
	}

	if (bBindingAssetChanged)
	{
		if (BindingAsset)
		{
			// Set the callback on the new GroomAsset being assigned
			BindingAsset->GetOnGroomBindingAssetChanged().AddUObject(this, &UGroomComponent::InvalidateAndRecreate);
			bIsGroomBindingAssetCallbackRegistered = true;
		}
	}
#endif

	if (PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairWidth) || 
		PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairLength) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairRootScale) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairTipScale) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairClipLength) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairShadowDensity) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairRaytracingRadiusScale) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, LODBias) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, bUseStableRasterization) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, bScatterSceneLighting))
	{	
		UpdateHairGroupsDescAndInvalidateRenderState();
		bIsEventProcess = true;
	}

	// Always call parent PostEditChangeProperty as parent expect such a call (would crash in certain case otherwise)
	//if (!bIsEventProcess)
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);
	}

#if WITH_EDITOR
	ValidateMaterials(false);
#endif
}

bool UGroomComponent::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		FString PropertyName = InProperty->GetName();

		if (FCString::Strcmp(*PropertyName, TEXT("HairRaytracingRadiusScale")) == 0)
		{
			bool bIsEditable = false;
			#if RHI_RAYTRACING
			if (IsHairRayTracingEnabled())
			{
				bIsEditable = true;
			}
			#endif
			return bIsEditable;
		}
	}

	return Super::CanEditChange(InProperty);
}
#endif

#if WITH_EDITOR

void UGroomComponent::ValidateMaterials(bool bMapCheck) const
{
	if (!GroomAsset)
		return;
	
	FString Name = "";
	if (GetOwner())
	{
		Name += GetOwner()->GetName() + "/";
	}
	Name += GetName() + "/" + GroomAsset->GetName();
	
	const ERHIFeatureLevel::Type FeatureLevel = GetScene() ? GetScene()->GetFeatureLevel() : ERHIFeatureLevel::Num;
	for (uint32 MaterialIt = 0, MaterialCount = GetNumMaterials(); MaterialIt < MaterialCount; ++MaterialIt)
	{
		UMaterialInterface* OverrideMaterial = Super::GetMaterial(MaterialIt);
		if (!OverrideMaterial && MaterialIt < uint32(GroomAsset->HairGroupsRendering.Num()) && GroomAsset->HairGroupsRendering[MaterialIt].Material)
		{
			OverrideMaterial = GroomAsset->HairGroupsRendering[MaterialIt].Material;
		}

		const EHairMaterialCompatibility Result = IsHairMaterialCompatible(OverrideMaterial, FeatureLevel);
		switch (Result)
		{
			case EHairMaterialCompatibility::Invalid_UsedWithHairStrands:
			{
				if (bMapCheck)
				{
					FMessageLog("MapCheck").Warning()
						->AddToken(FUObjectToken::Create(GroomAsset))
						->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_HairStrandsMissingUseHairStrands", "Groom's material needs to enable the UseHairStrands option. Groom's material will be replaced with default hair strands shader in editor.")))
						->AddToken(FMapErrorToken::Create(FMapErrors::InvalidHairStrandsMaterial));
				}
				else
				{
					UE_LOG(LogHairStrands, Warning, TEXT("[Groom] %s - Groom's material needs to enable the UseHairStrands option. Groom's material will be replaced with default hair strands shader in editor."), *Name);
				}
			} break;
			case EHairMaterialCompatibility::Invalid_ShadingModel:
			{
				if (bMapCheck)
				{
					FMessageLog("MapCheck").Warning()
						->AddToken(FUObjectToken::Create(GroomAsset))
						->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_HairStrandsInvalidShadingModel", "Groom's material needs to have Hair shading model. Groom's material will be replaced with default hair strands shader in editor.")))
						->AddToken(FMapErrorToken::Create(FMapErrors::InvalidHairStrandsMaterial));
				}
				else
				{
					UE_LOG(LogHairStrands, Warning, TEXT("[Groom] %s - Groom's material needs to have Hair shading model. Groom's material will be replaced with default hair strands shader in editor."), *Name);
				}
			}break;
			case EHairMaterialCompatibility::Invalid_BlendMode:
			{
				if (bMapCheck)
				{
					FMessageLog("MapCheck").Warning()
						->AddToken(FUObjectToken::Create(GroomAsset))
						->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_HairStrandsInvalidBlendMode", "Groom's material needs to have Opaque blend mode. Groom's material will be replaced with default hair strands shader in editor.")))
						->AddToken(FMapErrorToken::Create(FMapErrors::InvalidHairStrandsMaterial));
				}
				else
				{
					UE_LOG(LogHairStrands, Warning, TEXT("[Groom] %s - Groom's material needs to have Opaque blend mode. Groom's material will be replaced with default hair strands shader in editor."), *Name);
				}
			}break;
			case EHairMaterialCompatibility::Invalid_IsNull:
			{
				if (bMapCheck)
				{
					FMessageLog("MapCheck").Info()
						->AddToken(FUObjectToken::Create(GroomAsset))
						->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_HairStrandsMissingMaterial", "Groom's material is not set and will fallback on default hair strands shader in editor.")))
						->AddToken(FMapErrorToken::Create(FMapErrors::InvalidHairStrandsMaterial));
				}
				else
				{
					UE_LOG(LogHairStrands, Warning, TEXT("[Groom] %s - Groom's material is not set and will fallback on default hair strands shader in editor."), *Name);
				}
			}break;
		}
	}
}

void UGroomComponent::CheckForErrors()
{
	Super::CheckForErrors();

	const FCoreTexts& CoreTexts = FCoreTexts::Get();

	// Get the mesh owner's name.
	AActor* Owner = GetOwner();
	FString OwnerName(*(CoreTexts.None.ToString()));
	if (Owner)
	{
		OwnerName = Owner->GetName();
	}

	ValidateMaterials(true);
}
#endif

FGroomComponentRecreateRenderStateContext::FGroomComponentRecreateRenderStateContext(UGroomAsset* GroomAsset)
{
	if (!GroomAsset)
	{
		return;
	}

	for (TObjectIterator<UGroomComponent> HairStrandsComponentIt; HairStrandsComponentIt; ++HairStrandsComponentIt)
	{
		if (HairStrandsComponentIt->GroomAsset == GroomAsset)
		{
			if (HairStrandsComponentIt->IsRenderStateCreated())
			{
				HairStrandsComponentIt->DestroyRenderState_Concurrent();
				GroomComponents.Add(*HairStrandsComponentIt);
			}
		}
	}

	// Flush the rendering commands generated by the detachments.
	FlushRenderingCommands();
}

FGroomComponentRecreateRenderStateContext::~FGroomComponentRecreateRenderStateContext()
{
	const int32 ComponentCount = GroomComponents.Num();
	for (int32 ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
	{
		UGroomComponent* GroomComponent = GroomComponents[ComponentIndex];

		if (GroomComponent->IsRegistered() && !GroomComponent->IsRenderStateCreated())
		{
			GroomComponent->InitResources();
			GroomComponent->CreateRenderState_Concurrent(nullptr);
		}
	}
}

#undef LOCTEXT_NAMESPACE
