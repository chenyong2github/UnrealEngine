// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomComponent.h"
#include "GeometryCacheComponent.h"
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
#include "GroomCache.h"

static int32 GHairEnableAdaptiveSubsteps = 0;  
static FAutoConsoleVariableRef CVarHairEnableAdaptiveSubsteps(TEXT("r.HairStrands.EnableAdaptiveSubsteps"), GHairEnableAdaptiveSubsteps, TEXT("Enable adaptive solver substeps"));
bool IsHairAdaptiveSubstepsEnabled() { return (GHairEnableAdaptiveSubsteps == 1); }

static int32 GHairBindingValidationEnable = 0;
static FAutoConsoleVariableRef CVarHairBindingValidationEnable(TEXT("r.HairStrands.BindingValidation"), GHairBindingValidationEnable, TEXT("Enable groom binding validation, which report error/warnings with details about the cause."));

#define LOCTEXT_NAMESPACE "GroomComponent"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

EHairStrandsDebugMode GetHairStrandsGeometryDebugMode(const FHairGroupInstance* Instance);

const FLinearColor GetHairGroupDebugColor(int32 GroupIt)
{
	static TArray<FLinearColor> IndexedColor;
	if (IndexedColor.Num() == 0)
	{
		IndexedColor.Add(FLinearColor(FColor::Cyan));
		IndexedColor.Add(FLinearColor(FColor::Magenta));
		IndexedColor.Add(FLinearColor(FColor::Orange));
		IndexedColor.Add(FLinearColor(FColor::Silver));
		IndexedColor.Add(FLinearColor(FColor::Emerald));
		IndexedColor.Add(FLinearColor(FColor::Red));
		IndexedColor.Add(FLinearColor(FColor::Green));
		IndexedColor.Add(FLinearColor(FColor::Blue));
		IndexedColor.Add(FLinearColor(FColor::Yellow));
		IndexedColor.Add(FLinearColor(FColor::Purple));
		IndexedColor.Add(FLinearColor(FColor::Turquoise));
	
	}
	if (GroupIt >= IndexedColor.Num())
	{
		IndexedColor.Add(FLinearColor::MakeRandomColor());
	}
	check(GroupIt < IndexedColor.Num());
	return IndexedColor[GroupIt];	
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static EHairBindingType ToHairBindingType(EGroomBindingType In)
{
	switch (In)
	{
	case EGroomBindingType::Rigid		: return EHairBindingType::Rigid;
	case EGroomBindingType::Skinning	: return EHairBindingType::Skinning;
	case EGroomBindingType::NoneBinding	: return EHairBindingType::NoneBinding;
	}
	return EHairBindingType::NoneBinding;
}

static EHairInterpolationType ToHairInterpolationType(EGroomInterpolationType In)
{
	switch (In)
	{
	case EGroomInterpolationType::None				: return EHairInterpolationType::NoneSkinning; 
	case EGroomInterpolationType::RigidTransform	: return EHairInterpolationType::RigidSkinning;
	case EGroomInterpolationType::OffsetTransform	: return EHairInterpolationType::OffsetSkinning;
	case EGroomInterpolationType::SmoothTransform	: return EHairInterpolationType::SmoothSkinning;
	}
	return EHairInterpolationType::NoneSkinning;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FHairGroupDesc GetGroomGroupsDesc(const UGroomAsset* Asset, UGroomComponent* Component, uint32 GroupIndex)
{
	if (!Asset)
	{
		return FHairGroupDesc();
	}

	FHairGroupDesc O = Component->GroomGroupsDesc[GroupIndex];
	O.HairLength = Asset->HairGroupsData[GroupIndex].Strands.BulkData.MaxLength;
	O.LODBias 	 = Asset->EffectiveLODBias[GroupIndex] > 0 ? FMath::Max(O.LODBias, Asset->EffectiveLODBias[GroupIndex]) : O.LODBias;

	if (!O.HairWidth_Override)					{ O.HairWidth					= Asset->HairGroupsRendering[GroupIndex].GeometrySettings.HairWidth;					}
	if (!O.HairRootScale_Override)				{ O.HairRootScale				= Asset->HairGroupsRendering[GroupIndex].GeometrySettings.HairRootScale;				}
	if (!O.HairTipScale_Override)				{ O.HairTipScale				= Asset->HairGroupsRendering[GroupIndex].GeometrySettings.HairTipScale;					}
	if (!O.bSupportVoxelization_Override)		{ O.bSupportVoxelization		= Asset->HairGroupsRendering[GroupIndex].ShadowSettings.bVoxelize;						}
	if (!O.HairShadowDensity_Override)			{ O.HairShadowDensity			= Asset->HairGroupsRendering[GroupIndex].ShadowSettings.HairShadowDensity;				}
	if (!O.HairRaytracingRadiusScale_Override)	{ O.HairRaytracingRadiusScale	= Asset->HairGroupsRendering[GroupIndex].ShadowSettings.HairRaytracingRadiusScale;		}
	if (!O.bUseHairRaytracingGeometry_Override) { O.bUseHairRaytracingGeometry  = Asset->HairGroupsRendering[GroupIndex].ShadowSettings.bUseHairRaytracingGeometry;		}
	if (!O.bUseStableRasterization_Override)	{ O.bUseStableRasterization		= Asset->HairGroupsRendering[GroupIndex].AdvancedSettings.bUseStableRasterization;		}
	if (!O.bScatterSceneLighting_Override)		{ O.bScatterSceneLighting		= Asset->HairGroupsRendering[GroupIndex].AdvancedSettings.bScatterSceneLighting;		}

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
	const FVector HairGroupColor;

	FName DebugModeParamName;
	FName MinHairRadiusParamName;
	FName MaxHairRadiusParamName;
	FName HairGroupHairColorParamName;

	/** Initialization constructor. */
	FHairDebugModeMaterialRenderProxy(const FMaterialRenderProxy* InParent, float InMode, float InMinRadius, float InMaxRadius, FVector InGroupColor) :
		Parent(InParent),
		DebugMode(InMode),
		HairMinRadius(InMinRadius),
		HairMaxRadius(InMaxRadius),
		HairGroupColor(InGroupColor),
		DebugModeParamName(NAME_FloatProperty),
		MinHairRadiusParamName(NAME_ByteProperty),
		MaxHairRadiusParamName(NAME_IntProperty),
		HairGroupHairColorParamName(NAME_VectorProperty)
	{}

	virtual const FMaterial* GetMaterialNoFallback(ERHIFeatureLevel::Type InFeatureLevel) const override
	{
		return Parent->GetMaterialNoFallback(InFeatureLevel);
	}

	virtual const FMaterialRenderProxy* GetFallback(ERHIFeatureLevel::Type InFeatureLevel) const override
	{
		return Parent->GetFallback(InFeatureLevel);
	}

	virtual bool GetVectorValue(const FHashedMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const override
	{
		if (ParameterInfo.Name == HairGroupHairColorParamName)
		{
			*OutValue = HairGroupColor;
			return true;
		}
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
		return Parent->GetScalarValue(ParameterInfo, OutValue, Context);
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

static EHairMaterialCompatibility IsHairMaterialCompatible(UMaterialInterface* MaterialInterface, ERHIFeatureLevel::Type FeatureLevel, EHairGeometryType GeometryType)
{
	// Hair material and opaque material are enforced for strands material as the strands system is tailored for this type of shading
	// (custom packing of material attributes). However this is not needed/required for cards/meshes, when the relaxation for these type
	// of goemetry
	if (MaterialInterface)
	{
		if (GeometryType != Strands)
		{
			return EHairMaterialCompatibility::Valid;
		}
		const FMaterialRelevance Relevance = MaterialInterface->GetRelevance_Concurrent(FeatureLevel);
		const bool bIsRelevanceInitialized = Relevance.Raw != 0;
		if (bIsRelevanceInitialized && !Relevance.bHairStrands)
		{
			return EHairMaterialCompatibility::Invalid_UsedWithHairStrands;
		}
		if (!MaterialInterface->GetShadingModels().HasShadingModel(MSM_Hair) && GeometryType == EHairGeometryType::Strands)
		{
			return EHairMaterialCompatibility::Invalid_ShadingModel;
		}
		if (MaterialInterface->GetBlendMode() != BLEND_Opaque && MaterialInterface->GetBlendMode() != BLEND_Masked && GeometryType == EHairGeometryType::Strands)
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

inline int32 GetMaterialIndexWithFallback(int32 SlotIndex)
{
	// Default policy: if no slot index has been bound, fallback on slot 0. If there is no
	// slot, the material will fallback on the default material.
	return SlotIndex != INDEX_NONE ? SlotIndex : 0;
}

static EHairGeometryType ToHairGeometryType(EGroomGeometryType Type)
{
	switch (Type)
	{
	case EGroomGeometryType::Strands: return EHairGeometryType::Strands;
	case EGroomGeometryType::Cards:   return EHairGeometryType::Cards;
	case EGroomGeometryType::Meshes:  return EHairGeometryType::Meshes;
	}
	return EHairGeometryType::NoneGeometry;
}

class FHairStrandsSceneProxy final : public FPrimitiveSceneProxy
{
private:
	struct FHairGroup
	{
#if RHI_RAYTRACING
		FRayTracingGeometry* RayTracingGeometry_Strands = nullptr;
		TArray<FRayTracingGeometry*> RayTracingGeometries_Cards;  // Indexed by LOD
		TArray<FRayTracingGeometry*> RayTracingGeometries_Meshes;  // Indexed by LOD
#endif
		FHairGroupPublicData* PublicData = nullptr;
		TArray<FHairLODSettings> LODSettings;
		bool bIsVsibible = true;
	};

public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FHairStrandsSceneProxy(UGroomComponent* Component)
		: FPrimitiveSceneProxy(Component)
		, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
	{
		// Forcing primitive uniform as we don't support robustly GPU scene data
		bVFRequiresPrimitiveUniformBuffer = true;

		HairGroupInstances = Component->HairGroupInstances;
		check(Component);
		check(Component->GroomAsset);
		check(Component->GroomAsset->GetNumHairGroups() > 0);
		ComponentId = Component->ComponentId.PrimIDValue;
		Strands_DebugMaterial = Component->Strands_DebugMaterial;
		PredictedLODIndex = &Component->PredictedLODIndex;
		bAlwaysHasVelocity = false;
		if (IsHairStrandsBindingEnable() && Component->RegisteredMeshComponent)
		{
			bAlwaysHasVelocity = true;
		}

		check(Component->HairGroupInstances.Num());

		const ERHIFeatureLevel::Type FeatureLevel = GetScene().GetFeatureLevel();
		const EShaderPlatform ShaderPlatform = GetScene().GetShaderPlatform();

		const int32 GroupCount = Component->GroomAsset->GetNumHairGroups();
		check(Component->GroomAsset->HairGroupsData.Num() == Component->HairGroupInstances.Num());
		for (int32 GroupIt=0; GroupIt<GroupCount; GroupIt++)
		{
			const bool bIsVisible = Component->GroomAsset->HairGroupsInfo[GroupIt].bIsVisible;

			const FHairGroupData& InGroupData = Component->GroomAsset->HairGroupsData[GroupIt];
			FHairGroupInstance* HairInstance = Component->HairGroupInstances[GroupIt];
			check(HairInstance->HairGroupPublicData);
			HairInstance->ProxyBounds = &GetBounds();
			HairInstance->ProxyLocalBounds = &GetLocalBounds();
			HairInstance->bUseCPULODSelection = Component->GroomAsset->LODSelectionType == EHairLODSelectionType::Cpu;
			HairInstance->bForceCards = Component->bUseCards;
			HairInstance->bUpdatePositionOffset = Component->RegisteredMeshComponent != nullptr;
			HairInstance->bCastShadow = Component->CastShadow;

			if (HairInstance->Strands.IsValid() && HairInstance->Strands.VertexFactory == nullptr)
			{
				HairInstance->Strands.VertexFactory = new FHairStrandsVertexFactory(HairInstance, FeatureLevel, "FStrandsHairSceneProxy");
			}

			if (HairInstance->Cards.IsValid())
			{
				const uint32 LODCount = HairInstance->Cards.LODs.Num();
				for (uint32 LODIt = 0; LODIt < LODCount; ++LODIt)
				{
					if (HairInstance->Cards.IsValid(LODIt))
					{
						if (HairInstance->Cards.LODs[LODIt].VertexFactory[0] == nullptr) HairInstance->Cards.LODs[LODIt].VertexFactory[0] = new FHairCardsVertexFactory(HairInstance, GroupIt, LODIt, 0u, EHairGeometryType::Cards, ShaderPlatform, FeatureLevel, "HairCardsVertexFactory");
						if (HairInstance->Cards.LODs[LODIt].VertexFactory[1] == nullptr) HairInstance->Cards.LODs[LODIt].VertexFactory[1] = new FHairCardsVertexFactory(HairInstance, GroupIt, LODIt, 1u, EHairGeometryType::Cards, ShaderPlatform, FeatureLevel, "HairCardsVertexFactory");
					}
				}
			}

			if (HairInstance->Meshes.IsValid())
			{
				const uint32 LODCount = HairInstance->Meshes.LODs.Num();

				for (uint32 LODIt = 0; LODIt < LODCount; ++LODIt)
				{
					if (HairInstance->Meshes.IsValid(LODIt))
					{
						if (HairInstance->Meshes.LODs[LODIt].VertexFactory[0] == nullptr) HairInstance->Meshes.LODs[LODIt].VertexFactory[0] = new FHairCardsVertexFactory(HairInstance, GroupIt, LODIt, 0u, EHairGeometryType::Meshes, ShaderPlatform, FeatureLevel, "HairMeshesVertexFactory");
						if (HairInstance->Meshes.LODs[LODIt].VertexFactory[1] == nullptr) HairInstance->Meshes.LODs[LODIt].VertexFactory[1] = new FHairCardsVertexFactory(HairInstance, GroupIt, LODIt, 1u, EHairGeometryType::Meshes, ShaderPlatform, FeatureLevel, "HairMeshesVertexFactory");

					}
				}
			}

			{
				FHairGroup& OutGroupData = HairGroups.AddDefaulted_GetRef();
				OutGroupData.bIsVsibible = bIsVisible;
				OutGroupData.PublicData = HairInstance->HairGroupPublicData;
				OutGroupData.LODSettings = Component->GroomAsset->HairGroupsLOD[GroupIt].LODs;

				// If one of the group has simulation enable, then we enable velocity rendering for meshes/cards
				if (IsHairStrandsSimulationEnable() && HairInstance->Guides.IsValid() && HairInstance->Guides.bIsSimulationEnable)
				{
					bAlwaysHasVelocity = true;
				}

				#if RHI_RAYTRACING
				OutGroupData.RayTracingGeometry_Strands = nullptr;

				// Strands
				if (HairInstance->Strands.RenRaytracingResource && HairInstance->Strands.IsValid())
				{
					OutGroupData.RayTracingGeometry_Strands = &HairInstance->Strands.RenRaytracingResource->RayTracingGeometry;
				}

				// Cards
				OutGroupData.RayTracingGeometries_Cards.Reserve(HairInstance->Cards.LODs.Num());
				for (FHairGroupInstance::FCards::FLOD& LOD : HairInstance->Cards.LODs)
				{
					FRayTracingGeometry*& Geometry = OutGroupData.RayTracingGeometries_Cards.AddDefaulted_GetRef();
					Geometry = nullptr;
					if (LOD.IsValid() && LOD.RaytracingResource)
					{
						Geometry = &LOD.RaytracingResource->RayTracingGeometry;
					}
				}

				// Meshes
				OutGroupData.RayTracingGeometries_Meshes.Reserve(HairInstance->Meshes.LODs.Num());
				for (FHairGroupInstance::FMeshes::FLOD& LOD : HairInstance->Meshes.LODs)
				{
					FRayTracingGeometry*& Geometry = OutGroupData.RayTracingGeometries_Meshes.AddDefaulted_GetRef();
					Geometry = nullptr;
					if (LOD.IsValid() && LOD.RaytracingResource)
					{
						Geometry = &LOD.RaytracingResource->RayTracingGeometry;
					}
				}
				#endif
			}

			// Material - Strands
			if (IsHairStrandsEnabled(EHairStrandsShaderType::Strands, ShaderPlatform))
			{
				const int32 SlotIndex = Component->GroomAsset->GetMaterialIndex(Component->GroomAsset->HairGroupsRendering[GroupIt].MaterialSlotName);
				HairInstance->Strands.Material = Component->GetMaterial(GetMaterialIndexWithFallback(SlotIndex), EHairGeometryType::Strands, true);
			}

			// Material - Cards
			if (IsHairStrandsEnabled(EHairStrandsShaderType::Cards, ShaderPlatform))
			{
				uint32 CardsLODIndex = 0;
				for (const FHairGroupData::FCards::FLOD& LOD : InGroupData.Cards.LODs)
				{
					if (LOD.IsValid())
					{
						// Material
						int32 SlotIndex = INDEX_NONE;
						for (const FHairGroupsCardsSourceDescription& Desc : Component->GroomAsset->HairGroupsCards)
						{
							if (Desc.GroupIndex == GroupIt && Desc.LODIndex == CardsLODIndex)
							{
								SlotIndex = Component->GroomAsset->GetMaterialIndex(Desc.MaterialSlotName);
								break;
							}
						}
						HairInstance->Cards.LODs[CardsLODIndex].Material = Component->GetMaterial(GetMaterialIndexWithFallback(SlotIndex), EHairGeometryType::Cards, true);
					}
					++CardsLODIndex;
				}
			}

			// Material - Meshes
			if (IsHairStrandsEnabled(EHairStrandsShaderType::Meshes, ShaderPlatform))
			{
				uint32 MeshesLODIndex = 0;
				for (const FHairGroupData::FMeshes::FLOD& LOD : InGroupData.Meshes.LODs)
				{
					if (LOD.IsValid())
					{
						// Material
						int32 SlotIndex = INDEX_NONE;
						for (const FHairGroupsMeshesSourceDescription& Desc : Component->GroomAsset->HairGroupsMeshes)
						{
							if (Desc.GroupIndex == GroupIt && Desc.LODIndex == MeshesLODIndex)
							{
								SlotIndex = Component->GroomAsset->GetMaterialIndex(Desc.MaterialSlotName);
								break;
							}
						}
						HairInstance->Meshes.LODs[MeshesLODIndex].Material = Component->GetMaterial(GetMaterialIndexWithFallback(SlotIndex), EHairGeometryType::Meshes, true);
					}
					++MeshesLODIndex;
				}
			}

			// Initialization of the default skel. mesh transform (refresh during ticking).
			HairInstance->Debug.SkeletalLocalToWorld = HairInstance->Debug.SkeletalComponent ? HairInstance->Debug.SkeletalComponent->GetComponentTransform() : FTransform();
			HairInstance->Debug.SkeletalPreviousLocalToWorld = HairInstance->Debug.SkeletalLocalToWorld;
		}
	}

	virtual ~FHairStrandsSceneProxy()
	{
	}
	
	/**
	 *	Called when the rendering thread adds the proxy to the scene.
	 *	This function allows for generating renderer-side resources.
	 *	Called in the rendering thread.
	 */
	virtual void CreateRenderThreadResources() 
	{
		FPrimitiveSceneProxy::CreateRenderThreadResources();

		// Register the data to the scene
		FSceneInterface& LocalScene = GetScene();
		TArray<FHairGroupInstance*> LocalInstances = HairGroupInstances;
		for (FHairGroupInstance* Instance : LocalInstances)
		{
			if (Instance->IsValid() || Instance->Strands.ClusterCullingResource)
			{
				check(Instance->HairGroupPublicData != nullptr);
				Instance->AddRef();
				LocalScene.AddHairStrands(Instance);
			}
		}
	}

	/**
	 *	Called when the rendering thread removes the proxy from the scene.
	 *	This function allows for removing renderer-side resources.
	 *	Called in the rendering thread.
	 */
	virtual void DestroyRenderThreadResources() override
	{
		FPrimitiveSceneProxy::DestroyRenderThreadResources();

		// Unregister the data to the scene
		FSceneInterface& LocalScene = GetScene();
		TArray<FHairGroupInstance*> LocalInstances = HairGroupInstances;
		for (FHairGroupInstance* Instance : LocalInstances)
		{
			if (Instance->IsValid() || Instance->Strands.ClusterCullingResource)
			{
				check(Instance->GetRefCount() > 0);
				LocalScene.RemoveHairStrands(Instance);
				Instance->Release();
			}
		}
	}

	virtual void OnTransformChanged() override
	{
		const FTransform HairLocalToWorld = FTransform(GetLocalToWorld());
		for (FHairGroupInstance* Instance : HairGroupInstances)
		{
			Instance->LocalToWorld = HairLocalToWorld;
			if (Instance->BindingType == EHairBindingType::Skinning)
			{
				Instance->LocalToWorld = Instance->Debug.SkeletalLocalToWorld;
			}
		}
	}

	void GetOverrideLocalToTransform(const FHairGroupInstance* Instance, FMatrix& OutLocalToWorld) const
	{
		if (Instance->BindingType == EHairBindingType::Skinning)
		{
			OutLocalToWorld = Instance->Debug.SkeletalLocalToWorld.ToMatrixWithScale();
		}
	}

	void GetOverridePreviousLocalToTransform(const FHairGroupInstance* Instance, FMatrix& OutPreviousLocalToWorld) const
	{
		if (Instance->BindingType == EHairBindingType::Skinning)
		{
			OutPreviousLocalToWorld = Instance->Debug.SkeletalPreviousLocalToWorld.ToMatrixWithScale();
		}
	}

#if RHI_RAYTRACING
	virtual bool IsRayTracingRelevant() const { return true; }
	virtual bool IsRayTracingStaticRelevant() const { return false; }

	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext & Context, TArray<struct FRayTracingInstance>& OutRayTracingInstances) override
	{
		if (!IsHairRayTracingEnabled() || HairGroups.Num() == 0)
			return;

		const bool bWireframe = AllowDebugViewmodes() && Context.ReferenceViewFamily.EngineShowFlags.Wireframe;
		if (bWireframe)
			return;

		for (uint32 GroupIt = 0, GroupCount = HairGroups.Num(); GroupIt < GroupCount; ++GroupIt)
		{
			const FHairGroup& GroupData = HairGroups[GroupIt];

			FHairGroupInstance* Instance = HairGroupInstances[GroupIt];
			check(Instance->GetRefCount() > 0);
			FMatrix OverrideLocalToWorld = GetLocalToWorld();
			GetOverrideLocalToTransform(Instance, OverrideLocalToWorld);

			if (GroupData.PublicData->VFInput.GeometryType == EHairGeometryType::Strands)
			{
				if (GroupData.RayTracingGeometry_Strands && GroupData.RayTracingGeometry_Strands->RayTracingGeometryRHI.IsValid())
				{
					for (const FRayTracingGeometrySegment& Segment : GroupData.RayTracingGeometry_Strands->Initializer.Segments)
					{
						check(Segment.VertexBuffer.IsValid());
					}
					AddOpaqueRaytracingInstance(OverrideLocalToWorld, GroupData.RayTracingGeometry_Strands, RaytracingInstanceMask_ThinShadow, OutRayTracingInstances);
				}
			}
			else if (GroupData.PublicData->VFInput.GeometryType == EHairGeometryType::Cards)
			{
				const uint32 LODIndex = GroupData.PublicData->GetIntLODIndex();
				if (GroupData.RayTracingGeometries_Cards[LODIndex] && GroupData.RayTracingGeometries_Cards[LODIndex]->RayTracingGeometryRHI.IsValid())
				{
					for (const FRayTracingGeometrySegment& Segment : GroupData.RayTracingGeometries_Cards[LODIndex]->Initializer.Segments)
					{
						check(Segment.VertexBuffer.IsValid());
					}
				// Either use shadow only (no material evaluation) or full material evaluation during tracing
				#if 0
					AddOpaqueRaytracingInstance(OverrideLocalToWorld, GroupData.RayTracingGeometries_Cards[LODIndex], RaytracingInstanceMask_ThinShadow, OutRayTracingInstances);
				#else

					FMeshBatch* MeshBatch = CreateMeshBatch(Context.ReferenceView, Context.ReferenceViewFamily, Context.RayTracingMeshResourceCollector, GroupData, Instance, GroupIt, /*LODIndex,*/ nullptr);
					TArray<FMeshBatch> MeshBatches;
					MeshBatches.Add(*MeshBatch);
					AddOpaqueRaytracingInstance(OverrideLocalToWorld, GroupData.RayTracingGeometries_Cards[LODIndex], RaytracingInstanceMask_Opaque, MeshBatches, GetScene().GetFeatureLevel(), OutRayTracingInstances);
				#endif
				}
			}
			else if (GroupData.PublicData->VFInput.GeometryType == EHairGeometryType::Meshes)
			{
				const uint32 LODIndex = GroupData.PublicData->LODIndex;
				if (GroupData.RayTracingGeometries_Meshes[LODIndex] && GroupData.RayTracingGeometries_Meshes[LODIndex]->RayTracingGeometryRHI.IsValid())
				{
					for (const FRayTracingGeometrySegment& Segment : GroupData.RayTracingGeometries_Meshes[LODIndex]->Initializer.Segments)
					{
						check(Segment.VertexBuffer.IsValid());
					}
				// Either use shadow only (no material evaluation) or full material evaluation during tracing
				#if 0
					AddOpaqueRaytracingInstance(OverrideLocalToWorld, GroupData.RayTracingGeometries_Meshes[LODIndex], RaytracingInstanceMask_ThinShadow, OutRayTracingInstances);
				#else

					FMeshBatch* MeshBatch = CreateMeshBatch(Context.ReferenceView, Context.ReferenceViewFamily, Context.RayTracingMeshResourceCollector, GroupData, Instance, GroupIt, /*LODIndex,*/ nullptr);
					TArray<FMeshBatch> MeshBatches;
					MeshBatches.Add(*MeshBatch);
					AddOpaqueRaytracingInstance(OverrideLocalToWorld, GroupData.RayTracingGeometries_Meshes[LODIndex], RaytracingInstanceMask_Opaque, MeshBatches, GetScene().GetFeatureLevel(), OutRayTracingInstances);
				#endif
				}
			}
		}
	}
#endif

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		const EShaderPlatform Platform = ViewFamily.GetShaderPlatform();
		if (!IsHairStrandsEnabled(EHairStrandsShaderType::Strands, Platform) &&
			!IsHairStrandsEnabled(EHairStrandsShaderType::Cards, Platform) &&
			!IsHairStrandsEnabled(EHairStrandsShaderType::Meshes, Platform))
		{
			return;
		}

		TArray<FHairGroupInstance*> Instances = HairGroupInstances;
		if (Instances.Num() == 0)
		{
			return;
		}

		const uint32 GroupCount = Instances.Num();

		QUICK_SCOPE_CYCLE_COUNTER(STAT_HairStrandsSceneProxy_GetDynamicMeshElements);

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
					check(Instances[GroupIt]->GetRefCount() > 0);

					FMaterialRenderProxy* Strands_MaterialProxy = nullptr;
					const EHairStrandsDebugMode DebugMode = GetHairStrandsGeometryDebugMode(Instances[GroupIt]);
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
						case EHairStrandsDebugMode::RenderHairGroup				: DebugModeScalar = 9.f; break;
						};

						// TODO: fix this as the radius is incorrect. This code run before the interpolation code, which is where HairRadius is updated.
						float HairMaxRadius = 0;
						for (FHairGroupInstance* Instance : Instances)
						{
							HairMaxRadius = FMath::Max(HairMaxRadius, Instance->Strands.Modifier.HairWidth * 0.5f);
						}

						const FVector HairGroupColor = FVector(GetHairGroupDebugColor(GroupIt));
						auto DebugMaterial = new FHairDebugModeMaterialRenderProxy(Strands_DebugMaterial ? Strands_DebugMaterial->GetRenderProxy() : nullptr, DebugModeScalar, 0, HairMaxRadius, HairGroupColor);
						Collector.RegisterOneFrameMaterialProxy(DebugMaterial);
						Strands_MaterialProxy = DebugMaterial;
					}

					FMeshBatch* MeshBatch = CreateMeshBatch(View, ViewFamily, Collector, HairGroups[GroupIt], Instances[GroupIt], GroupIt, Strands_MaterialProxy);
					if (MeshBatch == nullptr)
					{
						continue;
					}
					Collector.AddMesh(ViewIndex, *MeshBatch);

				#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
					// Render bounds
					RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
				#endif
				}
			}
		}
	}

	FMeshBatch* CreateMeshBatch(
		const FSceneView* View,
		const FSceneViewFamily& ViewFamily,
		FMeshElementCollector& Collector,
		const FHairGroup& GroupData,
		const FHairGroupInstance* Instance,
		uint32 GroupIndex,
		FMaterialRenderProxy* Strands_MaterialProxy) const
	{
		const EHairGeometryType GeometryType = Instance->GeometryType;
		if (GeometryType == EHairGeometryType::NoneGeometry)
		{
			return nullptr;
		}

		check(Instance->GetRefCount());

		const int32 IntLODIndex = Instance->HairGroupPublicData->GetIntLODIndex();
		const FVertexFactory* VertexFactory = nullptr;
		FIndexBuffer* IndexBuffer = nullptr;
		FMaterialRenderProxy* MaterialRenderProxy = nullptr;

		uint32 NumPrimitive = 0;
		uint32 HairVertexCount = 0;
		uint32 MaxVertexIndex = 0;
		bool bUseCulling = false;
		bool bWireframe = false;
		if (GeometryType == EHairGeometryType::Meshes)
		{
			if (!Instance->Meshes.IsValid(IntLODIndex))
			{
				return nullptr;
			}
			VertexFactory = (FVertexFactory*)Instance->Meshes.LODs[IntLODIndex].GetVertexFactory();
			check(VertexFactory);
			HairVertexCount = Instance->Meshes.LODs[IntLODIndex].RestResource->GetPrimitiveCount() * 3;
			MaxVertexIndex = HairVertexCount;
			NumPrimitive = HairVertexCount / 3;
			IndexBuffer = &Instance->Meshes.LODs[IntLODIndex].RestResource->IndexBuffer;
			bUseCulling = false;
			MaterialRenderProxy = Instance->Meshes.LODs[IntLODIndex].Material->GetRenderProxy();
		}
		else if (GeometryType == EHairGeometryType::Cards)
		{
			if (!Instance->Cards.IsValid(IntLODIndex))
			{
				return nullptr;
			}

			VertexFactory = (FVertexFactory*)Instance->Cards.LODs[IntLODIndex].GetVertexFactory();
			check(VertexFactory);
			HairVertexCount = Instance->Cards.LODs[IntLODIndex].RestResource->GetPrimitiveCount() * 3;
			MaxVertexIndex = HairVertexCount;
			NumPrimitive = HairVertexCount / 3;
			IndexBuffer = &Instance->Cards.LODs[IntLODIndex].RestResource->RestIndexBuffer;
			bUseCulling = false;
			MaterialRenderProxy = Instance->Cards.LODs[IntLODIndex].Material->GetRenderProxy();
			bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;
		}
		else // if (GeometryType == EHairGeometryType::Strands)
		{
			VertexFactory = (FVertexFactory*)Instance->Strands.VertexFactory;
			HairVertexCount = Instance->Strands.RestResource->GetVertexCount();
			MaxVertexIndex = HairVertexCount * 6;
			bUseCulling = Instance->Strands.bIsCullingEnabled;
			NumPrimitive = bUseCulling ? 0 : HairVertexCount * 2;
			MaterialRenderProxy = Strands_MaterialProxy == nullptr ? Instance->Strands.Material->GetRenderProxy() : Strands_MaterialProxy;
			bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;
		}

		if (MaterialRenderProxy == nullptr || !GroupData.bIsVsibible)
		{
			return nullptr;
		}

		if (bWireframe)
		{
			MaterialRenderProxy = new FColoredMaterialRenderProxy( GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : NULL, FLinearColor(1.f, 0.5f, 0.f));
			Collector.RegisterOneFrameMaterialProxy(MaterialRenderProxy);
		}

		// Draw the mesh.
		FMeshBatch& Mesh = Collector.AllocateMesh();

		const bool bUseCardsOrMeshes = GeometryType == EHairGeometryType::Cards || GeometryType == EHairGeometryType::Meshes;
		Mesh.CastShadow = !IsHairStrandsComplexLightingEnabled() || bUseCardsOrMeshes;
		Mesh.bUseForMaterial  = true;
		Mesh.bUseForDepthPass = bUseCardsOrMeshes;
		Mesh.SegmentIndex = 0;

		FMeshBatchElement& BatchElement = Mesh.Elements[0];
		BatchElement.IndexBuffer = IndexBuffer;
		Mesh.bWireframe = bWireframe;
		Mesh.VertexFactory = VertexFactory;
		Mesh.MaterialRenderProxy = MaterialRenderProxy;
		bool bHasPrecomputedVolumetricLightmap;
		FMatrix PreviousLocalToWorld;
		int32 SingleCaptureIndex;
		bool bOutputVelocity = GeometryType == EHairGeometryType::Cards || GeometryType == EHairGeometryType::Meshes;
		bool bDrawVelocity = bOutputVelocity; // Velocity vector is done in a custom fashion
		GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);

		FMatrix OverrideLocalToWorld = GetLocalToWorld();
		GetOverrideLocalToTransform(Instance, OverrideLocalToWorld);
		GetOverridePreviousLocalToTransform(Instance, PreviousLocalToWorld);

		FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
		DynamicPrimitiveUniformBuffer.Set(OverrideLocalToWorld, PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, false, bDrawVelocity, bOutputVelocity);
		BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

		BatchElement.FirstIndex = 0;
		BatchElement.NumInstances = 1;
		BatchElement.PrimitiveIdMode = PrimID_ForceZero;
		if (bUseCulling)
		{
			BatchElement.NumPrimitives = 0;
			BatchElement.IndirectArgsBuffer = bUseCulling ? Instance->HairGroupPublicData->GetDrawIndirectBuffer().Buffer->GetRHI() : nullptr;
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

		return &Mesh;
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		bool bUseCardsOrMesh = false;
		for (FHairGroupInstance* Instance : HairGroupInstances)
		{
			check(Instance->GetRefCount());
			const EHairGeometryType GeometryType = Instance->GeometryType;
			bUseCardsOrMesh = bUseCardsOrMesh || GeometryType == EHairGeometryType::Cards || GeometryType == EHairGeometryType::Meshes;
		}

		FPrimitiveViewRelevance Result;
		Result.bHairStrands = IsShown(View);

		// Special pass for hair strands geometry (not part of the base pass, and shadowing is handlded in a custom fashion). When cards rendering is enabled we reusethe base pass
		Result.bDrawRelevance		= true;
		Result.bRenderInMainPass	= bUseCardsOrMesh;
		Result.bShadowRelevance		= true;
		Result.bDynamicRelevance	= true;
		Result.bRenderCustomDepth	= ShouldRenderCustomDepth();
		Result.bVelocityRelevance	= bUseCardsOrMesh;
		Result.bUsesLightingChannels= GetLightingChannelMask() != GetDefaultLightingChannelMask();

		// Selection only
		#if WITH_EDITOR
		{
			Result.bEditorStaticSelectionRelevance = true;
		}
		#endif
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		return Result;
	}

	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }

	uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }

	TArray<FHairGroupInstance*> HairGroupInstances;

private:
	uint32 ComponentId = 0;
	FMaterialRelevance MaterialRelevance;
	UMaterialInterface* Strands_DebugMaterial = nullptr;
	int32* PredictedLODIndex = nullptr;

	TArray<FHairGroup> HairGroups;
};

/** GroomCacheBuffers implementation that hold copies of the GroomCacheAnimationData needed for playback */
class FGroomCacheBuffers : public IGroomCacheBuffers
{
public:
	virtual FGroomCacheAnimationData& GetCurrentFrameBuffer() override
	{
		return CurrentFrame;
	}

	virtual FGroomCacheAnimationData& GetNextFrameBuffer() override
	{
		return NextFrame;
	}

	virtual FGroomCacheAnimationData& GetInterpolatedFrameBuffer() override
	{
		return InterpolatedFrame;
	}

	virtual int32 GetCurrentFrameIndex() const override
	{
		return CurrentFrameIndex;
	}

	virtual int32 GetNextFrameIndex() const override
	{
		return NextFrameIndex;
	}

	virtual float GetInterpolationFactor() const override
	{
		return InterpolationFactor;
	}

	virtual void SetCurrentFrameIndex(int32 FrameIndex)
	{
		CurrentFrameIndex = FrameIndex;
	}

	virtual void SetNextFrameIndex(int32 FrameIndex)
	{
		NextFrameIndex = FrameIndex;
	}

	virtual void SetInterpolationFactor(float Factor)
	{
		InterpolationFactor = Factor;
	}

private:
	FGroomCacheAnimationData CurrentFrame;
	FGroomCacheAnimationData NextFrame;
	FGroomCacheAnimationData InterpolatedFrame;

	int32 CurrentFrameIndex = 0;
	int32 NextFrameIndex = 0;
	float InterpolationFactor = 0.0f;
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
	RegisteredMeshComponent = nullptr;
	SkeletalPreviousPositionOffset = FVector::ZeroVector;
	InitializedResources = nullptr;
	Mobility = EComponentMobility::Movable;
	bIsGroomAssetCallbackRegistered = false;
	bIsGroomBindingAssetCallbackRegistered = false;
	SourceSkeletalMesh = nullptr;
	NiagaraComponents.Empty();
	PhysicsAsset = nullptr;
	bCanEverAffectNavigation = false;
	bValidationEnable = GHairBindingValidationEnable > 0;
	bRunning = true;
	bLooping = true;
	bManualTick = false;
	ElapsedTime = 0.0f;

	// Overlap events are expensive and not needed (at least at the moment) as we don't need to collide against other component.
	SetGenerateOverlapEvents(false);
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

#if WITH_EDITORONLY_DATA
	GroomAssetBeingLoaded = nullptr;
	BindingAssetBeingLoaded = nullptr;
#endif
}

void UGroomComponent::UpdateHairGroupsDesc()
{
	if (!GroomAsset)
	{
		GroomGroupsDesc.Empty();
		return;
	}

	const uint32 GroupCount = GroomAsset->GetNumHairGroups();
	const bool bNeedResize = GroupCount != GroomGroupsDesc.Num();
	if (bNeedResize)
	{
		GroomGroupsDesc.Init(FHairGroupDesc(), GroupCount);
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
bool UGroomComponent::IsSimulationEnable(int32 GroupIndex, int32 LODIndex) const
{
	bool bIsSimulationEnable = GroomAsset ? GroomAsset->IsSimulationEnable(GroupIndex, LODIndex) : false;
	if (SimulationSettings.bOverrideSettings)
	{
		bIsSimulationEnable = bIsSimulationEnable && SimulationSettings.SolverSettings.bEnableSimulation;
	}

	return bIsSimulationEnable;
}

static void InternalUpdateHairSimulation(UGroomComponent* GroomComponent, const bool bHasWorldReady)
{
	if (!GroomComponent || GroomComponent->GroomGroupsDesc.Num() == 0)
	{
		return;
	}
	UGroomAsset* GroomAsset = GroomComponent->GroomAsset;
	const int32 NumGroups = GroomAsset ? GroomAsset->HairGroupsPhysics.Num() : 0;
	const int32 NumComponents = FMath::Max(NumGroups, GroomComponent->NiagaraComponents.Num());

	TArray<bool> ValidComponents;
	ValidComponents.Init(false, NumComponents);

	bool NeedSpringsSolver = false;
	bool NeedRodsSolver = false;
	if (GroomAsset)
	{
		for (int32 i = 0; i < NumGroups; ++i)
		{
			const int32 LODIndex = GroomComponent->GroomGroupsDesc[i].LODForcedIndex;
			ValidComponents[i] = GroomComponent->IsSimulationEnable(i, LODIndex);
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
	if (bHasWorldReady)
	{
		if (IsHairAdaptiveSubstepsEnabled())
		{
			if (NeedSpringsSolver)
			{
				GroomComponent->AngularSpringsSystem = LoadObject<UNiagaraSystem>(nullptr, TEXT("/HairStrands/Emitters/SimpleSpringsSystem.SimpleSpringsSystem"));
			}
			if (NeedRodsSolver)
			{
				GroomComponent->CosseratRodsSystem = LoadObject<UNiagaraSystem>(nullptr, TEXT("/HairStrands/Emitters/SimpleRodsSystem.SimpleRodsSystem"));
			}
		}
		else
		{
			if (NeedSpringsSolver)
			{
				GroomComponent->AngularSpringsSystem = LoadObject<UNiagaraSystem>(nullptr, TEXT("/HairStrands/Emitters/StableSpringsSystem.StableSpringsSystem"));
			}
			if (NeedRodsSolver)
			{
				GroomComponent->CosseratRodsSystem = LoadObject<UNiagaraSystem>(nullptr, TEXT("/HairStrands/Emitters/StableRodsSystem.StableRodsSystem"));
			}
		}
	}
	GroomComponent->NiagaraComponents.SetNumZeroed(NumComponents);
	for (int32 i = 0; i < NumComponents; ++i)
	{
		UNiagaraComponent*& NiagaraComponent = GroomComponent->NiagaraComponents[i];
		if (ValidComponents[i])
		{
			if (!NiagaraComponent)
			{
				NiagaraComponent = NewObject<UNiagaraComponent>(GroomComponent, NAME_None, RF_Transient);
				if (GroomComponent->GetOwner() && GroomComponent->GetOwner()->GetWorld() && GroomComponent->GetOwner()->GetWorld()->bIsWorldInitialized)
				{
					NiagaraComponent->AttachToComponent(GroomComponent, FAttachmentTransformRules::KeepRelativeTransform);
					NiagaraComponent->RegisterComponent();
				}
				else
				{
					NiagaraComponent->SetupAttachment(GroomComponent);
				}
				NiagaraComponent->SetVisibleFlag(false);
			}
			if (bHasWorldReady)
			{
				if (GroomAsset->HairGroupsPhysics[i].SolverSettings.NiagaraSolver == EGroomNiagaraSolvers::AngularSprings)
				{
					NiagaraComponent->SetAsset(GroomComponent->AngularSpringsSystem);
				}
				else if (GroomAsset->HairGroupsPhysics[i].SolverSettings.NiagaraSolver == EGroomNiagaraSolvers::CosseratRods)
				{
					NiagaraComponent->SetAsset(GroomComponent->CosseratRodsSystem);
				}
				else
				{
					NiagaraComponent->SetAsset(GroomAsset->HairGroupsPhysics[i].SolverSettings.CustomSystem.LoadSynchronous());
				}
			}
			NiagaraComponent->ReinitializeSystem();
		}
		else if (NiagaraComponent && !NiagaraComponent->IsBeingDestroyed())
		{
			NiagaraComponent->DeactivateImmediate();
		}
	}
	GroomComponent->UpdateSimulatedGroups();
}

void UGroomComponent::UpdateHairSimulation()
{
	InternalUpdateHairSimulation(this, true);
}

void UGroomComponent::SetGroomAsset(UGroomAsset* Asset)
{
	SetGroomAsset(Asset, BindingAsset);
}

void UGroomComponent::SetGroomAsset(UGroomAsset* Asset, UGroomBindingAsset* InBinding)
{
	ReleaseResources();
	if (Asset && Asset->IsValid())
	{
		GroomAsset = Asset;

#if WITH_EDITORONLY_DATA
		if (InBinding && !InBinding->IsValid())
		{
			// The binding could be invalid if the groom asset was previously invalid.
			// This will re-fetch the binding data from the DDC to make it valid
			InBinding->InvalidateBinding();
		}
		GroomAssetBeingLoaded = nullptr;
		BindingAssetBeingLoaded = nullptr;
#endif
	}
#if WITH_EDITORONLY_DATA
	else if (Asset)
	{
		// The asset is still being loaded. This will allow the assets to be re-set once the groom is finished loading
		GroomAssetBeingLoaded = Asset;
		BindingAssetBeingLoaded = InBinding;
	}
#endif
	else
	{
		GroomAsset = nullptr;
	}
	if (BindingAsset != InBinding
#if WITH_EDITORONLY_DATA
		// With the groom still being loaded, the binding is still invalid
		&& !BindingAssetBeingLoaded
#endif
		)
	{
		BindingAsset = InBinding;
	}

	if (!UGroomBindingAsset::IsBindingAssetValid(BindingAsset, false, bValidationEnable) || !UGroomBindingAsset::IsCompatible(GroomAsset, BindingAsset, bValidationEnable))
	{
		BindingAsset = nullptr;
	}

	UpdateHairGroupsDesc();
	//UpdateHairSimulation();
	InternalUpdateHairSimulation(this,false);
	if (!GroomAsset || !GroomAsset->IsValid())
	{
		return;
	}
	InitResources();
}

void UGroomComponent::SetStableRasterization(bool bEnable)
{
	for (FHairGroupDesc& HairDesc : GroomGroupsDesc)
	{
		HairDesc.bUseStableRasterization = bEnable;
		HairDesc.bUseStableRasterization_Override = true;
	}
	UpdateHairGroupsDescAndInvalidateRenderState();
}

void UGroomComponent::SetHairRootScale(float Scale)
{
	Scale = FMath::Clamp(Scale, 0.f, 10.f);
	for (FHairGroupDesc& HairDesc : GroomGroupsDesc)
	{
		HairDesc.HairRootScale = Scale;
		HairDesc.HairRootScale_Override = true;
	}
	UpdateHairGroupsDescAndInvalidateRenderState();
}

void UGroomComponent::SetHairWidth(float HairWidth)
{
	for (FHairGroupDesc& HairDesc : GroomGroupsDesc)
	{
		HairDesc.HairWidth = HairWidth;
		HairDesc.HairWidth_Override = true;
	}
	UpdateHairGroupsDescAndInvalidateRenderState();
}

void UGroomComponent::SetScatterSceneLighting(bool Enable)
{
	for (FHairGroupDesc& HairDesc : GroomGroupsDesc)
	{
		HairDesc.bScatterSceneLighting = Enable;
		HairDesc.bScatterSceneLighting_Override = true;
	}
	UpdateHairGroupsDescAndInvalidateRenderState();
}

void UGroomComponent::SetUseCards(bool InbUseCards)
{
	bUseCards = InbUseCards;
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

	bool bHasLODChanged = false;
	for (FHairGroupDesc& HairDesc : GroomGroupsDesc)
	{
		bHasLODChanged = bHasLODChanged || HairDesc.LODForcedIndex != LODIndex;
		HairDesc.LODForcedIndex = LODIndex;
	}
	UpdateHairGroupsDesc();
	
	if (bHasLODChanged)
	{
		InternalUpdateHairSimulation(this, true);
	}

	// Do not invalidate completly the proxy, but just update LOD index on the rendering thread
	FHairStrandsSceneProxy* GroomSceneProxy = (FHairStrandsSceneProxy*)SceneProxy;
	if (GroomSceneProxy)
	{
		ENQUEUE_RENDER_COMMAND(FHairComponentSendLODIndex)(
		[GroomSceneProxy, LODIndex](FRHICommandListImmediate& RHICmdList)
		{
			for (FHairGroupInstance* Instance : GroomSceneProxy->HairGroupInstances)
			{
				Instance->Strands.Modifier.LODForcedIndex = LODIndex;
			}
		});
	}
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

int32 UGroomComponent::GetCurrentSyncLOD() const
{
	return GetForcedLOD();
}

void UGroomComponent::SetBinding(UGroomBindingAsset* InBinding)
{
	SetBindingAsset(InBinding);
}

void UGroomComponent::SetBindingAsset(UGroomBindingAsset* InBinding)
{
	if (BindingAsset != InBinding)
	{
		const bool bIsValid = InBinding != nullptr ? UGroomBindingAsset::IsBindingAssetValid(BindingAsset, false, bValidationEnable) : true;
		if (bIsValid && UGroomBindingAsset::IsCompatible(GroomAsset, InBinding, bValidationEnable))
		{
			BindingAsset = InBinding;
			InitResources();
		}
	}
}

void UGroomComponent::SetEnableSimulation(bool bInEnableSimulation)
{
	if (SimulationSettings.SolverSettings.bEnableSimulation != bInEnableSimulation)
	{
		SimulationSettings.SolverSettings.bEnableSimulation = bInEnableSimulation;

		ReleaseResources();
		UpdateHairGroupsDesc();
		UpdateHairSimulation();
		InitResources();
	}
}

void UGroomComponent::UpdateHairGroupsDescAndInvalidateRenderState()
{
	UpdateHairGroupsDesc();

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
	DeleteDeferredHairGroupInstances();

	if (!GroomAsset || GroomAsset->GetNumHairGroups() == 0 || HairGroupInstances.Num() == 0)
		return nullptr;

	bool bIsValid = false;
	for (FHairGroupInstance* Instance : HairGroupInstances)
	{
		bIsValid |= Instance->IsValid();
	}
	if (!bIsValid)
	{
		return nullptr;
	}
	return new FHairStrandsSceneProxy(this);
}


FBoxSphereBounds UGroomComponent::CalcBounds(const FTransform& InLocalToWorld) const
{
	if (GroomAsset && GroomAsset->GetNumHairGroups() > 0)
	{
		if (RegisteredMeshComponent)
		{
			const FBox WorldSkeletalBound = RegisteredMeshComponent->CalcBounds(InLocalToWorld).GetBox();
			return FBoxSphereBounds(WorldSkeletalBound);
		}
		else
		{
			FBox LocalBounds(EForceInit::ForceInitToZero);
			for (const FHairGroupData& GroupData : GroomAsset->HairGroupsData)
			{
				if (IsHairStrandsEnabled(EHairStrandsShaderType::Strands) && GroupData.Strands.HasValidData())
				{
					LocalBounds += GroupData.Strands.GetBounds();
				}
				else if (IsHairStrandsEnabled(EHairStrandsShaderType::Cards) && GroupData.Cards.HasValidData())
				{
					LocalBounds += GroupData.Cards.GetBounds();
				}
				else if (IsHairStrandsEnabled(EHairStrandsShaderType::Meshes) && GroupData.Meshes.HasValidData())
				{
					LocalBounds += GroupData.Meshes.GetBounds();
				}
				else if (GroupData.Guides.HasValidData())
				{
					LocalBounds += GroupData.Guides.GetBounds();
				}
			}
			return FBoxSphereBounds(LocalBounds.TransformBy(InLocalToWorld));
		}
	}
	else
	{
		FBoxSphereBounds LocalBounds(EForceInit::ForceInitToZero);
		return FBoxSphereBounds(LocalBounds.TransformBy(InLocalToWorld));
	}
}

/* Return the material slot index corresponding to the material name */
int32 UGroomComponent::GetMaterialIndex(FName MaterialSlotName) const
{
	if (GroomAsset)
	{
		return GroomAsset->GetMaterialIndex(MaterialSlotName);
	}

	return INDEX_NONE;
}

bool UGroomComponent::IsMaterialSlotNameValid(FName MaterialSlotName) const
{
	return GetMaterialIndex(MaterialSlotName) != INDEX_NONE;
}

TArray<FName> UGroomComponent::GetMaterialSlotNames() const
{
	TArray<FName> MaterialNames;
	if (GroomAsset)
	{
		MaterialNames = GroomAsset->GetMaterialSlotNames();
	}

	return MaterialNames;
}

int32 UGroomComponent::GetNumMaterials() const
{
	if (GroomAsset)
	{
		return FMath::Max(GroomAsset->HairGroupsMaterials.Num(), 1);
	}
	return 1;
}

UMaterialInterface* UGroomComponent::GetMaterial(int32 ElementIndex, EHairGeometryType GeometryType, bool bUseDefaultIfIncompatible) const
{
	UMaterialInterface* OverrideMaterial = Super::GetMaterial(ElementIndex);

	bool bUseHairDefaultMaterial = false;

	if (!OverrideMaterial && GroomAsset && ElementIndex < GroomAsset->HairGroupsMaterials.Num())
	{
		if (UMaterialInterface* Material = GroomAsset->HairGroupsMaterials[ElementIndex].Material)
		{
			OverrideMaterial = Material;
		}
		else if (bUseDefaultIfIncompatible)
		{
			bUseHairDefaultMaterial = true;
		}
	}

	if (bUseDefaultIfIncompatible)
	{
		const ERHIFeatureLevel::Type FeatureLevel = GetScene() ? GetScene()->GetFeatureLevel() : ERHIFeatureLevel::Num;
		if (FeatureLevel != ERHIFeatureLevel::Num && IsHairMaterialCompatible(OverrideMaterial, FeatureLevel, GeometryType) != EHairMaterialCompatibility::Valid)
		{
			bUseHairDefaultMaterial = true;
		}
	}

	if (bUseHairDefaultMaterial)
	{
		if (GeometryType == EHairGeometryType::Strands)
		{
			OverrideMaterial = Strands_DefaultMaterial;
		}
		else if (GeometryType == EHairGeometryType::Cards)
		{
			OverrideMaterial = Cards_DefaultMaterial;
		}
		else if (GeometryType == EHairGeometryType::Meshes)
		{
			OverrideMaterial = Meshes_DefaultMaterial;
		}
	}

	return OverrideMaterial;
}

EHairGeometryType UGroomComponent::GetMaterialGeometryType(int32 ElementIndex) const
{
	if (!GroomAsset)
	{
		// If we don't know, enforce strands, as it has the most requirement.
		return EHairGeometryType::Strands;
	}

	const EShaderPlatform Platform = GetScene() ? GetScene()->GetShaderPlatform() : EShaderPlatform::SP_NumPlatforms;
	for (uint32 GroupIt = 0, GroupCount = GroomAsset->HairGroupsRendering.Num(); GroupIt < GroupCount; ++GroupIt)
	{
		// Material - Strands
		const FHairGroupData& InGroupData = GroomAsset->HairGroupsData[GroupIt];
		if (IsHairStrandsEnabled(EHairStrandsShaderType::Strands, Platform))
		{
			const int32 SlotIndex = GroomAsset->GetMaterialIndex(GroomAsset->HairGroupsRendering[GroupIt].MaterialSlotName);
			if (SlotIndex == ElementIndex)
			{
				return EHairGeometryType::Strands;
			}
		}

		// Material - Cards
		if (IsHairStrandsEnabled(EHairStrandsShaderType::Cards, Platform))
		{
			uint32 CardsLODIndex = 0;
			for (const FHairGroupData::FCards::FLOD& LOD : InGroupData.Cards.LODs)
			{
				if (LOD.IsValid())
				{
					// Material
					int32 SlotIndex = INDEX_NONE;
					for (const FHairGroupsCardsSourceDescription& Desc : GroomAsset->HairGroupsCards)
					{
						if (Desc.GroupIndex == GroupIt && Desc.LODIndex == CardsLODIndex)
						{
							SlotIndex = GroomAsset->GetMaterialIndex(Desc.MaterialSlotName);
							break;
						}
					}
					if (SlotIndex == ElementIndex)
					{
						return EHairGeometryType::Cards;
					}
				}
				++CardsLODIndex;
			}
		}

		// Material - Meshes
		if (IsHairStrandsEnabled(EHairStrandsShaderType::Meshes, Platform))
		{
			uint32 MeshesLODIndex = 0;
			for (const FHairGroupData::FMeshes::FLOD& LOD : InGroupData.Meshes.LODs)
			{
				if (LOD.IsValid())
				{
					// Material
					int32 SlotIndex = INDEX_NONE;
					for (const FHairGroupsMeshesSourceDescription& Desc : GroomAsset->HairGroupsMeshes)
					{
						if (Desc.GroupIndex == GroupIt && Desc.LODIndex == MeshesLODIndex)
						{
							SlotIndex = GroomAsset->GetMaterialIndex(Desc.MaterialSlotName);
							break;
						}
					}
					if (SlotIndex == ElementIndex)
					{
						return EHairGeometryType::Meshes;
					}
				}
				++MeshesLODIndex;
			}
		}
	}
	// If we don't know, enforce strands, as it has the most requirement.
	return EHairGeometryType::Strands;
}

UMaterialInterface* UGroomComponent::GetMaterial(int32 ElementIndex) const
{
	const EHairGeometryType GeometryType = GetMaterialGeometryType(ElementIndex);
	return GetMaterial(ElementIndex, GeometryType, true);
}

FHairStrandsRestResource* UGroomComponent::GetGuideStrandsRestResource(uint32 GroupIndex)
{
	if (!GroomAsset || GroupIndex >= uint32(GroomAsset->GetNumHairGroups()))
	{
		return nullptr;
	}

	if (!GroomAsset->HairGroupsData[GroupIndex].Guides.IsValid())
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

	if (!HairGroupInstances[GroupIndex]->Guides.IsValid())
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

	if (!HairGroupInstances[GroupIndex]->Guides.IsValid())
	{
		return nullptr;
	}
	return HairGroupInstances[GroupIndex]->Guides.RestRootResource;
}

const FTransform& UGroomComponent::GetGuideStrandsLocalToWorld(uint32 GroupIndex) const
{
	if (GroupIndex >= uint32(HairGroupInstances.Num()))
	{
		return FTransform::Identity;
	}

	if (!HairGroupInstances[GroupIndex]->Guides.IsValid())
	{
		return FTransform::Identity;
	}
	return HairGroupInstances[GroupIndex]->LocalToWorld;
}

FHairStrandsDeformedRootResource* UGroomComponent::GetGuideStrandsDeformedRootResource(uint32 GroupIndex)
{
	if (GroupIndex >= uint32(HairGroupInstances.Num()))
	{
		return nullptr;
	}

	if (!HairGroupInstances[GroupIndex]->Guides.IsValid())
	{
		return nullptr;
	}
	return HairGroupInstances[GroupIndex]->Guides.DeformedRootResource;
}

void UGroomComponent::UpdateSimulatedGroups()
{
	if (HairGroupInstances.Num()>0)
	{
		const uint32 Id = ComponentId.PrimIDValue;

		const bool bIsStrandsEnabled = IsHairStrandsEnabled(EHairStrandsShaderType::Strands);

		// For now use the force LOD to drive enabling/disabling simulation & RBF
		const int32 LODIndex = GetForcedLOD();

		TArray<FHairGroupInstance*> LocalInstances = HairGroupInstances;
		UGroomAsset* LocalGroomAsset = GroomAsset;
		UGroomBindingAsset* LocalBindingAsset = BindingAsset;
		ENQUEUE_RENDER_COMMAND(FHairStrandsTick_UEnableSimulatedGroups)(
			[LocalInstances, LocalGroomAsset, LocalBindingAsset, Id, bIsStrandsEnabled, LODIndex](FRHICommandListImmediate& RHICmdList)
		{
			int32 GroupIt = 0;
			for (FHairGroupInstance* Instance : LocalInstances)
			{
				Instance->Strands.HairInterpolationType = EHairInterpolationType::NoneSkinning;
				if (bIsStrandsEnabled && LocalGroomAsset)
				{
					Instance->Strands.HairInterpolationType = ToHairInterpolationType(LocalGroomAsset->HairInterpolationType);
				}
				if (Instance->Guides.IsValid())
				{
					check(Instance->HairGroupPublicData);
					Instance->Guides.bIsSimulationEnable	 = Instance->HairGroupPublicData->IsSimulationEnable(LODIndex);
					Instance->Guides.bHasGlobalInterpolation = Instance->HairGroupPublicData->IsGlobalInterpolationEnable(LODIndex);
				}
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

static UGeometryCacheComponent* ValidateBindingAsset(
	UGroomAsset* GroomAsset, 
	UGroomBindingAsset* BindingAsset, 
	UGeometryCacheComponent* GeometryCacheComponent, 
	bool bIsBindingReloading, 
	bool bValidationEnable, 
	const USceneComponent* Component)
{
	if (!GroomAsset || !BindingAsset || !GeometryCacheComponent)
	{
		return nullptr;
	}

	bool bHasValidSectionCount = GeometryCacheComponent && GeometryCacheComponent->GetNumMaterials() < int32(GetHairStrandsMaxSectionCount());

	// Report warning if the section count is larger than the supported count
	if (GeometryCacheComponent && !bHasValidSectionCount)
	{
		FString Name = "";
		if (Component->GetOwner())
		{
			Name += Component->GetOwner()->GetName() + "/";
		}
		Name += Component->GetName() + "/" + GroomAsset->GetName();

		UE_LOG(LogHairStrands, Warning, TEXT("[Groom] %s - Groom is bound to a GeometryCache which has too many sections (%d), which is higher than the maximum supported for hair binding (%d). The groom binding will be disbled on this component."), *Name, GeometryCacheComponent->GetNumMaterials(), GetHairStrandsMaxSectionCount());
	}

	const bool bIsBindingCompatible =
		UGroomBindingAsset::IsCompatible(GeometryCacheComponent ? GeometryCacheComponent->GeometryCache : nullptr, BindingAsset, bValidationEnable) &&
		UGroomBindingAsset::IsCompatible(GroomAsset, BindingAsset, bValidationEnable) &&
		UGroomBindingAsset::IsBindingAssetValid(BindingAsset, bIsBindingReloading, bValidationEnable);

	return bIsBindingCompatible ? GeometryCacheComponent : nullptr;
}

// Return a non-null skeletal mesh Component if the binding asset is compatible with the current component
static USkeletalMeshComponent* ValidateBindingAsset(
	UGroomAsset* GroomAsset,
	UGroomBindingAsset* BindingAsset,
	USkeletalMeshComponent* SkeletalMeshComponent,
	bool bIsBindingReloading,
	bool bValidationEnable,
	const USceneComponent* Component)
{
	if (!GroomAsset || !BindingAsset || !SkeletalMeshComponent)
	{
		return nullptr;
	}

	// Optional advanced check
	bool bHasValidSectionCount = SkeletalMeshComponent && SkeletalMeshComponent->GetNumMaterials() < int32(GetHairStrandsMaxSectionCount());
	if (SkeletalMeshComponent && SkeletalMeshComponent->SkeletalMesh && SkeletalMeshComponent->SkeletalMesh->GetResourceForRendering())
	{
		const FSkeletalMeshRenderData* RenderData = SkeletalMeshComponent->SkeletalMesh->GetResourceForRendering();
		const int32 MaxSectionCount = GetHairStrandsMaxSectionCount();
		// Check that all LOD are below the number sections
		const uint32 MeshLODCount = RenderData->LODRenderData.Num();
		for (uint32 LODIt = 0; LODIt < MeshLODCount; ++LODIt)
		{
			if (RenderData->LODRenderData[LODIt].RenderSections.Num() >= MaxSectionCount)
			{
				bHasValidSectionCount = false;
			}
		}
	}

	// Report warning if the skeletal section count is larger than the supported count
	if (SkeletalMeshComponent && !bHasValidSectionCount)
	{
		FString Name = "";
		if (Component->GetOwner())
		{
			Name += Component->GetOwner()->GetName() + "/";
		}
		Name += Component->GetName() + "/" + GroomAsset->GetName();

		UE_LOG(LogHairStrands, Warning, TEXT("[Groom] %s - Groom is bound to a skeletal mesh which has too many sections (%d), which is higher than the maximum supported for hair binding (%d). The groom binding will be disbled on this component."), *Name, SkeletalMeshComponent->GetNumMaterials(), GetHairStrandsMaxSectionCount());
	}

	const bool bIsBindingCompatible =
		UGroomBindingAsset::IsCompatible(SkeletalMeshComponent ? SkeletalMeshComponent->SkeletalMesh : nullptr, BindingAsset, bValidationEnable) &&
		UGroomBindingAsset::IsCompatible(GroomAsset, BindingAsset, bValidationEnable) &&
		UGroomBindingAsset::IsBindingAssetValid(BindingAsset, bIsBindingReloading, bValidationEnable);

	if (!bIsBindingCompatible)
	{
		return nullptr;
	}

	// Validate against cards data
	for (int32 GroupIt = 0, GroupCount = GroomAsset->HairGroupsData.Num(); GroupIt < GroupCount; ++GroupIt)
	{
		FHairGroupData& GroupData = GroomAsset->HairGroupsData[GroupIt];
		uint32 CardsLODIndex = 0;
		for (FHairGroupData::FCards::FLOD& LOD : GroupData.Cards.LODs)
		{
			if (LOD.IsValid())
			{
				const bool bIsCardsBindingCompatible =
					bIsBindingCompatible &&
					BindingAsset &&
					GroupIt < BindingAsset->HairGroupResources.Num() &&
					CardsLODIndex < uint32(BindingAsset->HairGroupResources[GroupIt].CardsRootResources.Num()) &&
					BindingAsset->HairGroupResources[GroupIt].CardsRootResources[CardsLODIndex] != nullptr &&
					((SkeletalMeshComponent && SkeletalMeshComponent->SkeletalMesh) ? SkeletalMeshComponent->SkeletalMesh->GetLODInfoArray().Num() == BindingAsset->HairGroupResources[GroupIt].CardsRootResources[CardsLODIndex]->RootData.MeshProjectionLODs.Num() : false);

				if (!bIsCardsBindingCompatible)
				{
					return nullptr;
				}
			}
			CardsLODIndex++;
		}
	}

	return SkeletalMeshComponent;
}

static UMeshComponent* ValidateBindingAsset(
	UGroomAsset* GroomAsset,
	UGroomBindingAsset* BindingAsset,
	UMeshComponent* MeshComponent,
	bool bIsBindingReloading,
	bool bValidationEnable,
	const USceneComponent* Component)
{
	if (!GroomAsset || !BindingAsset || !MeshComponent)
	{
		return nullptr;
	}

	if (BindingAsset->GroomBindingType == EGroomBindingMeshType::SkeletalMesh)
	{
		return ValidateBindingAsset(GroomAsset, BindingAsset, Cast<USkeletalMeshComponent>(MeshComponent), bIsBindingReloading, bValidationEnable, Component);
	}
	return ValidateBindingAsset(GroomAsset, BindingAsset, Cast<UGeometryCacheComponent>(MeshComponent), bIsBindingReloading, bValidationEnable, Component);
}

static EGroomGeometryType GetEffectiveGeometryType(EGroomGeometryType Type, bool bUseCards)
{
	return Type == EGroomGeometryType::Strands && (!IsHairStrandsEnabled(EHairStrandsShaderType::Strands) || bUseCards) ? EGroomGeometryType::Cards : Type;
}

void UGroomComponent::InitResources(bool bIsBindingReloading)
{
	LLM_SCOPE(ELLMTag::Meshes) // This should be a Groom LLM tag, but there is no LLM tag bit left

	ReleaseResources();
	bInitSimulation = true;
	bResetSimulation = true;

	UpdateHairGroupsDesc();

	if (!GroomAsset || GroomAsset->GetNumHairGroups() == 0)
	{
		return;
	}

	InitializedResources = GroomAsset;

	// 1. Check if we need any kind of binding data, simulation data, or RBF data
	//
	//					  Requires    	| Requires     | Requires
	//         Features | Skeletal mesh	| Binding data | Guides
	// -----------------|---------------|--------------|-------------
	//    Rigid binding | No			| No		   | No
	// Skinning binding | Yes			| Yes		   | No
	//       Simulation | No			| No		   | Yes
	//              RBF | Yes		    | Yes		   | Yes
	//
	bool bHasNeedSkinningBinding = false;			   
	bool bHasNeedGlobalDeformation = false;
	bool bHasNeedSimulation = false;
	bool bHasNeedSkeletalMesh = false;
	for (int32 GroupIt = 0, GroupCount = GroomAsset->HairGroupsData.Num(); GroupIt < GroupCount; ++GroupIt)
	{
		for (uint32 LODIt = 0, LODCount = GroomAsset->GetLODCount(); LODIt < LODCount; ++LODIt)
		{
			const EGroomGeometryType GeometryType = GetEffectiveGeometryType(GroomAsset->GetGeometryType(GroupIt, LODIt), bUseCards);
			const EGroomBindingType BindingType = GroomAsset->GetBindingType(GroupIt, LODIt);

			// Note on Global Deformation:
			// * Global deformation require to have skinning binding
			// * Force global interpolation to be enable for meshes with skinning binding as we use RBF defomation for 'sticking' meshes onto skel. mesh surface			
			bHasNeedSkeletalMesh	  = bHasNeedSkeletalMesh || BindingType == EGroomBindingType::Rigid;
			bHasNeedSkinningBinding   = bHasNeedSkinningBinding || BindingType == EGroomBindingType::Skinning;
			bHasNeedSimulation		  = bHasNeedSimulation || IsSimulationEnable(GroupIt, LODIt);
			bHasNeedGlobalDeformation = bHasNeedGlobalDeformation || (BindingType == EGroomBindingType::Skinning && GroomAsset->IsGlobalInterpolationEnable(GroupIt, LODIt));
		}
	}
	const bool bHasNeedBindingData = bHasNeedSkinningBinding || bHasNeedGlobalDeformation;

	// 2. Insure that the binding asset is compatible, otherwise no binding
	UMeshComponent* ParentMeshComponent = GetAttachParent() ? Cast<UMeshComponent>(GetAttachParent()) : nullptr;
	UMeshComponent* ValidatedMeshComponent = nullptr;
	if (bHasNeedBindingData && ParentMeshComponent)
	{
		ValidatedMeshComponent = ValidateBindingAsset(GroomAsset, BindingAsset, ParentMeshComponent, bIsBindingReloading, bValidationEnable, this);
		if (ValidatedMeshComponent)
		{
			if (BindingAsset && 
				((BindingAsset->GroomBindingType == EGroomBindingMeshType::SkeletalMesh && Cast<USkeletalMeshComponent>(ValidatedMeshComponent)->SkeletalMesh == nullptr) ||
				(BindingAsset->GroomBindingType == EGroomBindingMeshType::GeometryCache && Cast<UGeometryCacheComponent>(ValidatedMeshComponent)->GeometryCache == nullptr)))
			{
				ValidatedMeshComponent = nullptr;
			}
		}
	}
	else if (bHasNeedSkeletalMesh && ParentMeshComponent)
	{
		if (USkeletalMeshComponent* ParentSkelMeshComponent = Cast<USkeletalMeshComponent>(ParentMeshComponent))
		{
			ValidatedMeshComponent = ParentSkelMeshComponent->SkeletalMesh ? ParentMeshComponent : nullptr;
		}
	}

	// Insure the ticking of the Groom component always happens after the skeletalMeshComponent.
	UGroomBindingAsset* LocalBindingAsset = nullptr;
	if (ValidatedMeshComponent)
	{
		RegisteredMeshComponent = ValidatedMeshComponent;
		AddTickPrerequisiteComponent(ValidatedMeshComponent);
		bUseAttachParentBound = true;
		LocalBindingAsset = bHasNeedBindingData ? BindingAsset : nullptr;
	}

	// Update requirement based on binding asset availability
	if (LocalBindingAsset == nullptr)
	{
		bHasNeedSkinningBinding = false;
		bHasNeedGlobalDeformation = false;
	}

	if (GroomCache)
	{
		GroomCacheBuffers = MakeShared<FGroomCacheBuffers, ESPMode::ThreadSafe>();
		UpdateGroomCache(ElapsedTime);
	}
	else
	{
		GroomCacheBuffers.Reset();
	}

	for (int32 GroupIt = 0, GroupCount = GroomAsset->HairGroupsData.Num(); GroupIt < GroupCount; ++GroupIt)
	{
		FHairGroupInstance* HairGroupInstance = new FHairGroupInstance();
		HairGroupInstance->AddRef();
		HairGroupInstances.Add(HairGroupInstance);
		HairGroupInstance->Debug.ComponentId = ComponentId.PrimIDValue;
		HairGroupInstance->Debug.GroupIndex = GroupIt;
		HairGroupInstance->Debug.GroupCount = GroupCount;
		HairGroupInstance->Debug.GroomAssetName = GroomAsset->GetName();
		HairGroupInstance->Debug.MeshComponent = RegisteredMeshComponent;
		HairGroupInstance->Debug.GroomBindingType = BindingAsset ? BindingAsset->GroomBindingType : EGroomBindingMeshType::SkeletalMesh;
		HairGroupInstance->Debug.GroomCacheType = GroomCache ? GroomCache->GetType() : EGroomCacheType::None;
		HairGroupInstance->Debug.GroomCacheBuffers = GroomCacheBuffers;
		if (RegisteredMeshComponent)
		{
			HairGroupInstance->Debug.MeshComponentName = RegisteredMeshComponent->GetPathName();
		}
		HairGroupInstance->GeometryType = EHairGeometryType::NoneGeometry;
		HairGroupInstance->BindingType = EHairBindingType::NoneBinding;

		FHairGroupData& GroupData = GroomAsset->HairGroupsData[GroupIt];

		const EHairInterpolationType HairInterpolationType = ToHairInterpolationType(GroomAsset->HairInterpolationType);

		// Initialize LOD screen size & visibility
		bool bNeedStrandsData = false;
		{
			HairGroupInstance->HairGroupPublicData = new FHairGroupPublicData(GroupIt);
			HairGroupInstance->HairGroupPublicData->Instance = HairGroupInstance;
			TArray<float> CPULODScreenSize;
			TArray<bool> LODVisibility;
			TArray<EHairGeometryType> LODGeometryTypes;
			const FHairGroupsLOD& GroupLOD = GroomAsset->HairGroupsLOD[GroupIt];
			for (uint32 LODIt = 0, LODCount = GroupLOD.LODs.Num(); LODIt < LODCount; ++LODIt)
			{
				const FHairLODSettings& LODSettings = GroupLOD.LODs[LODIt];
				EHairBindingType BindingType = ToHairBindingType(GroomAsset->GetBindingType(GroupIt, LODIt));
				if (BindingType == EHairBindingType::Skinning && !LocalBindingAsset)
				{
					BindingType = EHairBindingType::NoneBinding;
				}
				else if (BindingType == EHairBindingType::Rigid && !RegisteredMeshComponent)
				{
					BindingType = EHairBindingType::NoneBinding;
				}

				// * Force global interpolation to be enable for meshes with skinning binding as we use RBF defomation for 'sticking' meshes onto skel. mesh surface
				// * Global deformation are allowed only with 'Skinning' binding type
				const EHairGeometryType GeometryType = ToHairGeometryType(GetEffectiveGeometryType(GroomAsset->GetGeometryType(GroupIt, LODIt), bUseCards));
				const bool LODSimulation = IsSimulationEnable(GroupIt, LODIt);
				const bool LODGlobalInterpolation = LocalBindingAsset && BindingType == EHairBindingType::Skinning && GroomAsset->IsGlobalInterpolationEnable(GroupIt, LODIt);
				bNeedStrandsData = bNeedStrandsData || GeometryType == EHairGeometryType::Strands;

				CPULODScreenSize.Add(LODSettings.ScreenSize);
				LODVisibility.Add(LODSettings.bVisible);
				LODGeometryTypes.Add(GeometryType);
				HairGroupInstance->HairGroupPublicData->BindingTypes.Add(BindingType);
				HairGroupInstance->HairGroupPublicData->LODSimulations.Add(LODSimulation);
				HairGroupInstance->HairGroupPublicData->LODGlobalInterpolations.Add(LODGlobalInterpolation);
			}
			HairGroupInstance->HairGroupPublicData->SetLODScreenSizes(CPULODScreenSize);
			HairGroupInstance->HairGroupPublicData->SetLODVisibilities(LODVisibility);
			HairGroupInstance->HairGroupPublicData->SetLODGeometryTypes(LODGeometryTypes);
		}

		// Not needed anymore
		//const bool bDynamicResources = IsHairStrandsSimulationEnable() || IsHairStrandsBindingEnable();

		// Sim data
		// Simulation guides are used for two purposes:
		// * Physics simulation
		// * RBF deformation.
		// Therefore, even if simulation is disabled, we need to run partially the update if the binding system is enabled (skin deformation + RBF correction)
		const bool bNeedGuides = GroupData.Guides.HasValidData() && (bHasNeedSimulation || bHasNeedGlobalDeformation || bPreviewMode);
		if (bNeedGuides)
		{
			HairGroupInstance->Guides.Data = &GroupData.Guides.BulkData;

			if (LocalBindingAsset)
			{
				check(RegisteredMeshComponent);
				check(LocalBindingAsset);
				check(GroupIt < LocalBindingAsset->HairGroupResources.Num());
				if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(RegisteredMeshComponent))
				{
					check(SkeletalMeshComponent->SkeletalMesh ? SkeletalMeshComponent->SkeletalMesh->GetLODInfoArray().Num() == LocalBindingAsset->HairGroupResources[GroupIt].SimRootResources->RootData.MeshProjectionLODs.Num() : false);
				}

				HairGroupInstance->Guides.RestRootResource = LocalBindingAsset->HairGroupResources[GroupIt].SimRootResources;
				HairGroupInstance->Guides.DeformedRootResource = new FHairStrandsDeformedRootResource(HairGroupInstance->Guides.RestRootResource, EHairStrandsResourcesType::Guides);
			}

			// Lazy allocation of the guide resources
			HairGroupInstance->Guides.RestResource = GroomAsset->AllocateGuidesResources(GroupIt);
			check(GroupData.Guides.RestResource);

			// If guides are allocated, deformed resources are always needs since they are either used with simulation, or RBF deformation. Both are dynamics, and require deformed positions
			HairGroupInstance->Guides.DeformedResource = new FHairStrandsDeformedResource(GroupData.Guides.BulkData, true, EHairStrandsResourcesType::Guides);

			// Initialize the simulation and the global deformation to its default behavior by setting it with LODIndex = -1
			const int32 LODIndex = -1;
			HairGroupInstance->Guides.bIsSimulationEnable = IsSimulationEnable(GroupIt,LODIndex);
			HairGroupInstance->Guides.bHasGlobalInterpolation = LocalBindingAsset && GroomAsset->IsGlobalInterpolationEnable(GroupIt,LODIndex);
		}

		// LODBias is in the Modifier which is needed for LOD selection regardless if the strands are there or not
		HairGroupInstance->Strands.Modifier = GetGroomGroupsDesc(GroomAsset, this, GroupIt);
		#if WITH_EDITORONLY_DATA
		HairGroupInstance->Debug.DebugMode = GroomAsset->GetDebugMode();
		#endif// #if WITH_EDITORONLY_DATA

		// Strands data/resources
		if (bNeedStrandsData && GroupData.Strands.IsValid())
		{
			check(GroupIt < GroomGroupsDesc.Num());

			HairGroupInstance->Strands.Data = &GroupData.Strands.BulkData;
//			HairGroupInstance->Strands.InterpolationData = &GroupData.Strands.InterpolationBulkData;

			// (Lazy) Allocate interpolation resources, only if guides are required
			if (bNeedGuides)
			{
				HairGroupInstance->Strands.InterpolationResource = GroomAsset->AllocateInterpolationResources(GroupIt);
				check(HairGroupInstance->Strands.InterpolationResource);
			}

			// Material
			HairGroupInstance->Strands.Material = nullptr;


			// Check if strands needs to load/allocate root resources
			bool bNeedRootResources = false;
			bool bNeedDynamicResources = false;
			{
				const FHairGroupsLOD& GroupLOD = GroomAsset->HairGroupsLOD[GroupIt];
				for (uint32 LODIt = 0, LODCount = GroupLOD.LODs.Num(); LODIt < LODCount; ++LODIt)
				{
					if (HairGroupInstance->HairGroupPublicData->GetGeometryType(LODIt) == EHairGeometryType::Strands)
					{
						if (LocalBindingAsset &&
							(HairGroupInstance->HairGroupPublicData->GetBindingType(LODIt) == EHairBindingType::Skinning ||
							 HairGroupInstance->HairGroupPublicData->IsGlobalInterpolationEnable(LODIt)))
						{
							bNeedRootResources = true;
						}

						if ((LocalBindingAsset && HairGroupInstance->HairGroupPublicData->GetBindingType(LODIt) == EHairBindingType::Skinning) ||
							HairGroupInstance->HairGroupPublicData->IsSimulationEnable(LODIt))
						{
							bNeedDynamicResources = true;
						}
					}
				}
			}

			#if RHI_RAYTRACING
			if (IsHairRayTracingEnabled() && HairGroupInstance->Strands.Modifier.bUseHairRaytracingGeometry)
			{
				if (bNeedDynamicResources)
				{
					// Allocate dynamic raytracing resources (owned by the groom component/instance)
					HairGroupInstance->Strands.RenRaytracingResource = new FHairStrandsRaytracingResource(GroupData.Strands.BulkData);
					HairGroupInstance->Strands.RenRaytracingResourceOwned = true;
				}
				else
				{
					// (Lazy) Allocate static raytracing resources (owned by the grooom asset)
					HairGroupInstance->Strands.RenRaytracingResourceOwned = false;
					HairGroupInstance->Strands.RenRaytracingResource = GroomAsset->AllocateStrandsRaytracingResources(GroupIt);
					check(HairGroupInstance->Strands.RenRaytracingResource);
				}
			}
			#endif

			if (bNeedRootResources)
			{
				check(LocalBindingAsset);
				check(RegisteredMeshComponent);
				check(GroupIt < LocalBindingAsset->HairGroupResources.Num());
				if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(RegisteredMeshComponent))
				{
					check(SkeletalMeshComponent->SkeletalMesh ? SkeletalMeshComponent->SkeletalMesh->GetLODInfoArray().Num() == LocalBindingAsset->HairGroupResources[GroupIt].RenRootResources->RootData.MeshProjectionLODs.Num() : false);
				}

				HairGroupInstance->Strands.RestRootResource = LocalBindingAsset->HairGroupResources[GroupIt].RenRootResources;
				HairGroupInstance->Strands.DeformedRootResource = new FHairStrandsDeformedRootResource(HairGroupInstance->Strands.RestRootResource, EHairStrandsResourcesType::Strands);
			}

			HairGroupInstance->Strands.RestResource = GroupData.Strands.RestResource;
			if (bNeedDynamicResources)
			{
				HairGroupInstance->Strands.DeformedResource = new FHairStrandsDeformedResource(GroupData.Strands.BulkData, false, EHairStrandsResourcesType::Strands);
			} 

			// An empty groom doesn't have a ClusterCullingResource
			HairGroupInstance->Strands.ClusterCullingResource = GroupData.Strands.ClusterCullingResource;
			if (HairGroupInstance->Strands.ClusterCullingResource)
			{
				// This codes assumes strands LOD are contigus and the highest (i.e., 0...x). Change this code to something more robust
				check(HairGroupInstance->HairGroupPublicData);
				const int32 StrandsLODCount = GroupData.Strands.ClusterCullingResource->Data.CPULODScreenSize.Num();
				const TArray<float>& LODScreenSizes = HairGroupInstance->HairGroupPublicData->GetLODScreenSizes();
				const TArray<bool>& LODVisibilities = HairGroupInstance->HairGroupPublicData->GetLODVisibilities();
				check(StrandsLODCount <= LODScreenSizes.Num());
				check(StrandsLODCount <= LODVisibilities.Num());
				for (int32 LODIt = 0; LODIt < StrandsLODCount; ++LODIt)
				{
					// ClusterCullingData seriazlizes only the screen size related to strands geometry.
					// Other type of geometry are not serizalized, and so won't match
					if (GroomAsset->HairGroupsLOD[GroupIt].LODs[LODIt].GeometryType == EGroomGeometryType::Strands)
					{
						check(GroupData.Strands.ClusterCullingResource->Data.CPULODScreenSize[LODIt] == LODScreenSizes[LODIt]);
						check(GroupData.Strands.ClusterCullingResource->Data.LODVisibility[LODIt] == LODVisibilities[LODIt]);
					}
				}
				HairGroupInstance->HairGroupPublicData->SetClusters(HairGroupInstance->Strands.ClusterCullingResource->Data.ClusterCount, GroupData.Strands.BulkData.GetNumPoints());
				BeginInitResource(HairGroupInstance->HairGroupPublicData);
			}


			HairGroupInstance->Strands.HairInterpolationType = HairInterpolationType;
		}

		// Cards resources
		for (FHairGroupData::FCards::FLOD& LOD : GroupData.Cards.LODs)
		{
			const uint32 CardsLODIndex = HairGroupInstance->Cards.LODs.Num();
			FHairGroupInstance::FCards::FLOD& InstanceLOD = HairGroupInstance->Cards.LODs.AddDefaulted_GetRef();
			if (LOD.IsValid())
			{
				const EHairBindingType BindingType	= HairGroupInstance->HairGroupPublicData->GetBindingType(CardsLODIndex);
				const bool bHasSimulation			= HairGroupInstance->HairGroupPublicData->IsSimulationEnable(CardsLODIndex);
				const bool bHasGlobalDeformation	= HairGroupInstance->HairGroupPublicData->IsGlobalInterpolationEnable(CardsLODIndex);
				const bool bNeedDeformedPositions	= bHasSimulation || bHasGlobalDeformation || BindingType == EHairBindingType::Skinning;
				const bool bNeedRootData			= bHasGlobalDeformation || BindingType == EHairBindingType::Skinning;

				// Sanity check
				if (bHasGlobalDeformation)
				{
					check(BindingType == EHairBindingType::Skinning);
				}

				InstanceLOD.Data = &LOD.BulkData;
				InstanceLOD.RestResource = LOD.RestResource;
				InstanceLOD.InterpolationResource = LOD.InterpolationResource;

				if (bNeedDeformedPositions)
				{
					InstanceLOD.DeformedResource = new FHairCardsDeformedResource(LOD.BulkData, false);
				}

				#if RHI_RAYTRACING
				if (IsRayTracingEnabled())
				{
					if (bNeedDeformedPositions)
					{
						// Allocate dynamic raytracing resources (owned by the groom component/instance)
						InstanceLOD.RaytracingResource = new FHairStrandsRaytracingResource(*InstanceLOD.Data);
						InstanceLOD.RaytracingResourceOwned = true;
					}
					else
					{
						// (Lazy) Allocate static raytracing resources (owned by the grooom asset)
						InstanceLOD.RaytracingResourceOwned = false;
						InstanceLOD.RaytracingResource = GroomAsset->AllocateCardsRaytracingResources(GroupIt, CardsLODIndex);
						check(InstanceLOD.RaytracingResource);
					}
				}
				#endif

				// Material
				InstanceLOD.Material = nullptr;

				// Strands data/resources
				if (bNeedDeformedPositions)
				{
					InstanceLOD.Guides.Data = &LOD.Guides.BulkData;
					InstanceLOD.Guides.InterpolationResource = LOD.Guides.InterpolationResource;

					if (bNeedRootData)
					{
						check(LocalBindingAsset);
						check(GroupIt < LocalBindingAsset->HairGroupResources.Num());
						if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(RegisteredMeshComponent))
						{
							check(SkeletalMeshComponent->SkeletalMesh ? SkeletalMeshComponent->SkeletalMesh->GetLODInfoArray().Num() == LocalBindingAsset->HairGroupResources[GroupIt].CardsRootResources[CardsLODIndex]->RootData.MeshProjectionLODs.Num() : false);
						}

						InstanceLOD.Guides.RestRootResource = LocalBindingAsset->HairGroupResources[GroupIt].CardsRootResources[CardsLODIndex];
						InstanceLOD.Guides.DeformedRootResource = new FHairStrandsDeformedRootResource(InstanceLOD.Guides.RestRootResource, EHairStrandsResourcesType::Cards);
					}

					InstanceLOD.Guides.RestResource = LOD.Guides.RestResource;
					{
						InstanceLOD.Guides.DeformedResource = new FHairStrandsDeformedResource(LOD.Guides.BulkData, false, EHairStrandsResourcesType::Cards);
					}

					InstanceLOD.Guides.HairInterpolationType = HairInterpolationType;
				}
			}
		}

		// Meshes resources
		for (FHairGroupData::FMeshes::FLOD& LOD : GroupData.Meshes.LODs)
		{
			const int32 MeshLODIndex = HairGroupInstance->Meshes.LODs.Num();
			FHairGroupInstance::FMeshes::FLOD& InstanceLOD = HairGroupInstance->Meshes.LODs.AddDefaulted_GetRef();
			if (LOD.IsValid())
			{
				const EHairBindingType BindingType = HairGroupInstance->HairGroupPublicData->GetBindingType(MeshLODIndex);
				const bool bHasGlobalDeformation   = HairGroupInstance->HairGroupPublicData->IsGlobalInterpolationEnable(MeshLODIndex);
				const bool bNeedDeformedPositions  = bHasGlobalDeformation && BindingType == EHairBindingType::Skinning;

				InstanceLOD.Data = &LOD.BulkData;
				InstanceLOD.RestResource = LOD.RestResource;
				if (bNeedDeformedPositions)
				{
					InstanceLOD.DeformedResource = new FHairMeshesDeformedResource(LOD.BulkData, true);
				}

				#if RHI_RAYTRACING
				if (IsRayTracingEnabled() && bNeedDeformedPositions)
				{
					if (bNeedDeformedPositions)
					{
						// Allocate dynamic raytracing resources (owned by the groom component/instance)
						InstanceLOD.RaytracingResource = new FHairStrandsRaytracingResource(*InstanceLOD.Data);
						InstanceLOD.RaytracingResourceOwned = true;
					}
					else
					{
						// (Lazy) Allocate static raytracing resources (owned by the grooom asset)
						InstanceLOD.RaytracingResourceOwned = false;
						InstanceLOD.RaytracingResource = GroomAsset->AllocateMeshesRaytracingResources(GroupIt, MeshLODIndex);
						check(InstanceLOD.RaytracingResource);
					}
				}
				#endif

				// Material
				InstanceLOD.Material = nullptr;
			}
		}
	}

	// Does not run projection code when running with null RHI as this is not needed, and will crash as the skeletal GPU resources are not created
	if (GUsingNullRHI)
	{
		return;
	}
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

	FHairStrandsSceneProxy* GroomSceneProxy = (FHairStrandsSceneProxy*)SceneProxy;
	InitializedResources = nullptr;

	// Deferring instances deletion to insure scene proxy are done with the rendering data
	DeferredDeleteHairGroupInstances.Append(HairGroupInstances);
	HairGroupInstances.Empty();

	// Insure the ticking of the Groom component always happens after the skeletalMeshComponent.
	if (RegisteredMeshComponent)
	{
		RemoveTickPrerequisiteComponent(RegisteredMeshComponent);
	}
	SkeletalPreviousPositionOffset = FVector::ZeroVector;
	RegisteredMeshComponent = nullptr;

	GroomCacheBuffers.Reset();

	MarkRenderStateDirty();
}

void UGroomComponent::DeleteDeferredHairGroupInstances()
{
	FHairStrandsSceneProxy* GroomSceneProxy = (FHairStrandsSceneProxy*)SceneProxy;
	for (FHairGroupInstance* Instance : DeferredDeleteHairGroupInstances)
	{
		FHairGroupInstance* LocalInstance = Instance;
		ENQUEUE_RENDER_COMMAND(FHairStrandsBuffers)(
		[LocalInstance, GroomSceneProxy](FRHICommandListImmediate& RHICmdList)
		{
			// Sanity check
			check(LocalInstance->GetRefCount() == 1);

			// Guides
			if (LocalInstance->Guides.IsValid())
			{
				InternalResourceRelease(LocalInstance->Guides.DeformedRootResource);
				InternalResourceRelease(LocalInstance->Guides.DeformedResource);
			}

			// Strands
			if (LocalInstance->Strands.IsValid())
			{
				InternalResourceRelease(LocalInstance->Strands.DeformedRootResource);
				InternalResourceRelease(LocalInstance->Strands.DeformedResource);

				#if RHI_RAYTRACING
				if (LocalInstance->Strands.RenRaytracingResourceOwned)
				{
					InternalResourceRelease(LocalInstance->Strands.RenRaytracingResource);
				}
				#endif

				#if WITH_EDITOR
				LocalInstance->Strands.DebugAttributeBuffer.Release();
				#endif

				InternalResourceRelease(LocalInstance->Strands.VertexFactory);
			}

			// Cards
			{
				const uint32 CardLODCount = LocalInstance->Cards.LODs.Num();
				for (uint32 CardLODIt = 0; CardLODIt < CardLODCount; ++CardLODIt)
				{
					if (LocalInstance->Cards.IsValid(CardLODIt))
					{
						InternalResourceRelease(LocalInstance->Cards.LODs[CardLODIt].Guides.DeformedRootResource);
						InternalResourceRelease(LocalInstance->Cards.LODs[CardLODIt].Guides.DeformedResource);
						InternalResourceRelease(LocalInstance->Cards.LODs[CardLODIt].DeformedResource);
						#if RHI_RAYTRACING
						if (LocalInstance->Cards.LODs[CardLODIt].RaytracingResourceOwned)
						{
							InternalResourceRelease(LocalInstance->Cards.LODs[CardLODIt].RaytracingResource);
						}
						#endif

						InternalResourceRelease(LocalInstance->Cards.LODs[CardLODIt].VertexFactory[0]);
						InternalResourceRelease(LocalInstance->Cards.LODs[CardLODIt].VertexFactory[1]);
					}
				}
			}

			// Meshes
			{
				const uint32 MeshesLODCount = LocalInstance->Meshes.LODs.Num();
				for (uint32 MeshesLODIt = 0; MeshesLODIt < MeshesLODCount; ++MeshesLODIt)
				{
					if (LocalInstance->Meshes.IsValid(MeshesLODIt))
					{
						InternalResourceRelease(LocalInstance->Meshes.LODs[MeshesLODIt].DeformedResource);
						#if RHI_RAYTRACING
						if (LocalInstance->Meshes.LODs[MeshesLODIt].RaytracingResourceOwned)
						{
							InternalResourceRelease(LocalInstance->Meshes.LODs[MeshesLODIt].RaytracingResource);
						}
						#endif

						InternalResourceRelease(LocalInstance->Meshes.LODs[MeshesLODIt].VertexFactory[0]);
						InternalResourceRelease(LocalInstance->Meshes.LODs[MeshesLODIt].VertexFactory[1]);
					}
				}
			}

			InternalResourceRelease(LocalInstance->HairGroupPublicData);
			LocalInstance->Release();
			delete LocalInstance;
		});
	}
	DeferredDeleteHairGroupInstances.Empty();
}

void UGroomComponent::PostLoad()
{
	LLM_SCOPE(ELLMTag::Meshes) // This should be a Groom LLM tag, but there is no LLM tag bit left

	Super::PostLoad();

	if (GroomAsset)
	{
		// Make sure that the asset initialized its resources first since the component needs them to initialize its own resources
		GroomAsset->ConditionalPostLoad();
	}

	if (BindingAsset)
	{
		// Make sure that the asset initialized its resources first since the component needs them to initialize its own resources
		BindingAsset->ConditionalPostLoad();
	}

	// This call will handle the GroomAsset properly if it's still being loaded
	SetGroomAsset(GroomAsset, BindingAsset);

#if WITH_EDITOR
	if (GroomAsset && !bIsGroomAssetCallbackRegistered)
	{
		// Delegate used for notifying groom data invalidation
		GroomAsset->GetOnGroomAssetChanged().AddUObject(this, &UGroomComponent::Invalidate);

		// Delegate used for notifying groom data & groom resoures invalidation
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
	UpdateHairGroupsDescAndInvalidateRenderState();
	UpdateHairSimulation();
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
	UpdateHairGroupsDesc();
	UpdateHairSimulation();

	// Insure the parent skeletal mesh is the same than the registered skeletal mesh, and if not reinitialized resources
	// This can happens when the OnAttachment callback is not called, but the skeletal mesh change (e.g., the skeletal mesh get recompiled within a blueprint)
	UMeshComponent* MeshComponent = GetAttachParent() ? Cast<UMeshComponent>(GetAttachParent()) : nullptr;

	const bool bNeedInitialization = !InitializedResources || InitializedResources != GroomAsset || MeshComponent != RegisteredMeshComponent;
	if (bNeedInitialization)
	{
		InitResources();
	}
}

void UGroomComponent::OnUnregister()
{
	Super::OnUnregister();
	ReleaseHairSimulation();
}

void UGroomComponent::BeginDestroy()
{
	ReleaseResources();
	Super::BeginDestroy();
}

void UGroomComponent::FinishDestroy()
{
	DeleteDeferredHairGroupInstances();
	Super::FinishDestroy();
}

void UGroomComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	ReleaseResources();
	DeleteDeferredHairGroupInstances();

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
		UMeshComponent* NewMeshComponent = Cast<UMeshComponent>(GetAttachParent());
		const bool bHasAttachmentChanged = RegisteredMeshComponent != NewMeshComponent;
		if (bHasAttachmentChanged)
		{
			InitResources();
		}
	}
}

// Override the DetachFromComponent so that we do not mark the actor as modified/dirty when attaching to a specific bone
void UGroomComponent::DetachFromComponent(const FDetachmentTransformRules& In)
{
	FDetachmentTransformRules DetachmentRules = In;
	DetachmentRules.bCallModify = false;
	Super::DetachFromComponent(DetachmentRules);
}

void UGroomComponent::UpdateGroomCache(float Time)
{
	if (GroomCache && GroomCacheBuffers.IsValid() && bRunning)
	{
		float AnimationTime = Time;
		const float Duration = GroomCache->GetDuration();
		if (bLooping && Duration > 0.0f)
		{
			AnimationTime = AnimationTime - Duration * FMath::FloorToFloat(AnimationTime / Duration);
		}
		FScopeLock Lock(GroomCacheBuffers->GetCriticalSection());
		GroomCache->GetGroomDataAtTime(AnimationTime, GroomCacheBuffers->GetInterpolatedFrameBuffer());
	}
}

void UGroomComponent::SetGroomCache(UGroomCache* InGroomCache)
{
	if (GroomCache != InGroomCache)
	{
		ReleaseResources();
		ResetAnimationTime();
		GroomCache = InGroomCache;
		InitResources();
	}
}

float UGroomComponent::GetGroomCacheDuration() const
{
	return GroomCache ? GroomCache->GetDuration() : 0.0f;
}

void UGroomComponent::SetManualTick(bool bInManualTick)
{
	bManualTick = bInManualTick;
}

bool UGroomComponent::GetManualTick() const
{
	return bManualTick;
}

void UGroomComponent::ResetAnimationTime()
{
	ElapsedTime = 0.0f;
}

void UGroomComponent::TickAtThisTime(const float Time, bool bInIsRunning, bool bInBackwards, bool bInIsLooping)
{
	if (GroomCache && bRunning && bManualTick)
	{
		UpdateGroomCache(Time);
	}
}

void UGroomComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);	
	
	const uint32 Id = ComponentId.PrimIDValue;
	const ERHIFeatureLevel::Type FeatureLevel = GetWorld() ? ERHIFeatureLevel::Type(GetWorld()->FeatureLevel) : ERHIFeatureLevel::Num;

	// When a groom binding and simulation are disabled, and the groom component is parented with a skeletal mesh, we can optionally 
	// attach the groom to a particular socket/bone
	USkeletalMeshComponent* SkeletelMeshComponent = Cast<USkeletalMeshComponent>(RegisteredMeshComponent);
	if (SkeletelMeshComponent && SkeletelMeshComponent->SkeletalMesh && !AttachmentName.IsEmpty())
	{
		const FName BoneName(AttachmentName);
		if (GetAttachSocketName() != BoneName)
		{
			AttachToComponent(SkeletelMeshComponent, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false), BoneName);
			const uint32 BoneIndex = SkeletelMeshComponent->GetBoneIndex(BoneName);
			const FMatrix BoneTransformRaw = SkeletelMeshComponent->SkeletalMesh->GetComposedRefPoseMatrix(BoneIndex);
			const FVector BoneLocation = BoneTransformRaw.GetOrigin();
			const FQuat BoneRotation = BoneTransformRaw.ToQuat();

			FTransform BoneTransform = FTransform::Identity;
			BoneTransform.SetLocation(BoneLocation);
			BoneTransform.SetRotation(BoneRotation);

			FTransform InvBoneTransform = BoneTransform.Inverse();
			SetRelativeLocation(InvBoneTransform.GetLocation());
			SetRelativeRotation(InvBoneTransform.GetRotation());
		}
	}

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
		bResetSimulation |= SimulationSettings.bResetSimulation;
	}
	bInitSimulation = false;

	if (HairGroupInstances.Num() == 0)
	{
		return;
	}

	if (RegisteredMeshComponent)
		{
			// When a skeletal object with projection is enabled, activate the refresh of the bounding box to
			// insure the component/proxy bounding box always lies onto the actual skinned mesh
			MarkRenderTransformDirty();
		}

	if (GroomCache && bRunning && !bManualTick)
	{
		ElapsedTime += DeltaTime;
		UpdateGroomCache(ElapsedTime);
	}

	const FTransform SkinLocalToWorld = RegisteredMeshComponent ? RegisteredMeshComponent->GetComponentTransform() : FTransform();
	TArray<FHairGroupInstance*> LocalHairGroupInstances = HairGroupInstances;
	ENQUEUE_RENDER_COMMAND(FHairStrandsTick_TransformUpdate)(
		[Id, SkinLocalToWorld, FeatureLevel, LocalHairGroupInstances](FRHICommandListImmediate& RHICmdList)
	{
		if (ERHIFeatureLevel::Num == FeatureLevel)
			return;

		for (FHairGroupInstance* Instance : LocalHairGroupInstances)
		{
			Instance->Debug.SkeletalPreviousLocalToWorld = Instance->Debug.SkeletalLocalToWorld;
			Instance->Debug.SkeletalLocalToWorld = SkinLocalToWorld;
		}
	});
}

void UGroomComponent::SendRenderTransform_Concurrent()
{
	if (RegisteredMeshComponent)
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
	UMeshComponent::GetUsedMaterials(OutMaterials, bGetDebugMaterials);
	if (bGetDebugMaterials)
	{
		if (IsHairStrandsEnabled(EHairStrandsShaderType::Tool))
		{
			OutMaterials.Add(Strands_DebugMaterial);
		}
	}

	if (IsHairStrandsEnabled(EHairStrandsShaderType::Strands))
	{
		OutMaterials.Add(Strands_DefaultMaterial);
	}

	if (IsHairStrandsEnabled(EHairStrandsShaderType::Cards))
	{
		OutMaterials.Add(Cards_DefaultMaterial);
	}
	if (IsHairStrandsEnabled(EHairStrandsShaderType::Meshes))
	{
		OutMaterials.Add(Meshes_DefaultMaterial);
	}
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
	const bool bIsBindingCompatible = UGroomBindingAsset::IsCompatible(GroomAsset, BindingAsset, bValidationEnable);
	const bool bEnableSolverChanged = PropertyName == GET_MEMBER_NAME_CHECKED(FHairSimulationSolver, bEnableSimulation);
	const bool bGroomCacheChanged = PropertyName == GET_MEMBER_NAME_CHECKED(UGroomComponent, GroomCache);
	if (!bIsBindingCompatible || !UGroomBindingAsset::IsBindingAssetValid(BindingAsset, false, bValidationEnable))
	{
		BindingAsset = nullptr;
	}

	if (GroomAsset && !GroomAsset->IsValid())
	{
		GroomAsset = nullptr;
	}

	bool bIsEventProcess = false;

	// If the raytracing flag change, then force to recreate resources
	// Update the raytracing resources only if the groom asset hasn't changed. Otherwise the group desc might be
	// invalid. If the groom asset has change, the raytracing resources will be correctly be rebuild witin the
	// InitResources function
	bool bRayTracingGeometryChanged = false;
	#if RHI_RAYTRACING
	if (GroomAsset && !bAssetChanged)
	{
		for (const FHairGroupInstance* Instance : HairGroupInstances)
		{
			const FHairGroupDesc GroupDesc = GetGroomGroupsDesc(GroomAsset, this, Instance->Debug.GroupIndex);
			const bool bRecreate =
				(Instance->Strands.RenRaytracingResource == nullptr && GroupDesc.bUseHairRaytracingGeometry) ||
				(Instance->Strands.RenRaytracingResource != nullptr && !GroupDesc.bUseHairRaytracingGeometry);
			if (bRecreate)
			{
				bRayTracingGeometryChanged = true;
				break;
			}
		}
	}
	#endif

	const bool bRecreateResources = bAssetChanged || bBindingAssetChanged || bGroomCacheChanged || PropertyThatChanged == nullptr || bSourceSkeletalMeshChanged || bRayTracingGeometryChanged || bEnableSolverChanged;
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
		PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairRootScale) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, HairTipScale) ||
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
		// Do not fallback on default material, so that we can detect that a material is not valid, and we can emit warning/validation error for this material
		const EHairGeometryType GeometryType = GetMaterialGeometryType(MaterialIt);
		UMaterialInterface* OverrideMaterial = GetMaterial(MaterialIt, GeometryType, false);

		const EHairMaterialCompatibility Result = IsHairMaterialCompatible(OverrideMaterial, FeatureLevel, GeometryType);
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

template<typename T>
void InternalAddDedicatedVideoMemoryBytes(FResourceSizeEx& CumulativeResourceSize, T Resource)
{
	if (Resource)
	{
		CumulativeResourceSize.AddDedicatedVideoMemoryBytes(Resource->GetResourcesSize());
	}
}

void UGroomComponent::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	for (const FHairGroupInstance* Instance : HairGroupInstances)
	{
		InternalAddDedicatedVideoMemoryBytes(CumulativeResourceSize, Instance->Guides.DeformedResource);
		InternalAddDedicatedVideoMemoryBytes(CumulativeResourceSize, Instance->Guides.DeformedRootResource);

		InternalAddDedicatedVideoMemoryBytes(CumulativeResourceSize, Instance->Strands.DeformedResource);
		InternalAddDedicatedVideoMemoryBytes(CumulativeResourceSize, Instance->Strands.DeformedRootResource);

		for (const FHairGroupInstance::FCards::FLOD& LOD : Instance->Cards.LODs)
		{
			InternalAddDedicatedVideoMemoryBytes(CumulativeResourceSize, LOD.DeformedResource);
		}

		for (const FHairGroupInstance::FMeshes::FLOD& LOD : Instance->Meshes.LODs)
		{
			InternalAddDedicatedVideoMemoryBytes(CumulativeResourceSize, LOD.DeformedResource);
		}
	}
}

#if WITH_EDITORONLY_DATA
FGroomComponentRecreateRenderStateContext::FGroomComponentRecreateRenderStateContext(UGroomAsset* GroomAsset)
{
	if (!GroomAsset)
	{
		return;
	}

	for (TObjectIterator<UGroomComponent> HairStrandsComponentIt; HairStrandsComponentIt; ++HairStrandsComponentIt)
	{
		if (HairStrandsComponentIt->GroomAsset == GroomAsset ||
			HairStrandsComponentIt->GroomAssetBeingLoaded == GroomAsset) // A GroomAsset was set on the component while it was still loading
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
			if (GroomComponent->GroomAssetBeingLoaded && GroomComponent->GroomAssetBeingLoaded->IsValid())
			{
				// Re-set the assets on the component now that they are loaded
				GroomComponent->SetGroomAsset(GroomComponent->GroomAssetBeingLoaded, GroomComponent->BindingAssetBeingLoaded);
			}
			else
			{
				GroomComponent->InitResources();
			}
			GroomComponent->CreateRenderState_Concurrent(nullptr);
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE
