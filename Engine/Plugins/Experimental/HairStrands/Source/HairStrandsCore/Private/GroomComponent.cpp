// Copyright Epic Games, Inc. All Rights Reserved. 

#include "GroomComponent.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "PrimitiveSceneProxy.h"
#include "HairStrandsRendering.h"
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

static float GHairClipLength = -1;
static FAutoConsoleVariableRef CVarHairClipLength(TEXT("r.HairStrands.DebugClipLength"), GHairClipLength, TEXT("Clip hair strands which have a lenth larger than this value. (default is -1, no effect)"));
float GetHairClipLength() { return GHairClipLength > 0 ? GHairClipLength : 100000;  }

static int32 GHairStrandsMeshUseRelativePosition = 1;
static FAutoConsoleVariableRef CVarHairStrandsMeshUseRelativePosition(TEXT("r.HairStrands.MeshProjection.RelativePosition"), GHairStrandsMeshUseRelativePosition, TEXT("Enable/Disable relative triangle position for improving positions"));

#define LOCTEXT_NAMESPACE "GroomComponent"

/**
 * An material render proxy which overrides the debug mode parameter.
 */
class ENGINE_VTABLE FHairDebugModeMaterialRenderProxy : public FMaterialRenderProxy
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

	virtual const FMaterial& GetMaterialWithFallback(ERHIFeatureLevel::Type InFeatureLevel, const FMaterialRenderProxy*& OutFallbackMaterialRenderProxy) const
	{
		return Parent->GetMaterialWithFallback(InFeatureLevel, OutFallbackMaterialRenderProxy);
	}

	virtual bool GetVectorValue(const FMaterialParameterInfo& ParameterInfo, FLinearColor* OutValue, const FMaterialRenderContext& Context) const
	{
		return Parent->GetVectorValue(ParameterInfo, OutValue, Context);
	}

	virtual bool GetScalarValue(const FMaterialParameterInfo& ParameterInfo, float* OutValue, const FMaterialRenderContext& Context) const
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

	virtual bool GetTextureValue(const FMaterialParameterInfo& ParameterInfo, const URuntimeVirtualTexture** OutValue, const FMaterialRenderContext& Context) const
	{
		return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
	}

	virtual bool GetTextureValue(const FMaterialParameterInfo& ParameterInfo, const UTexture** OutValue, const FMaterialRenderContext& Context) const
	{
		return Parent->GetTextureValue(ParameterInfo, OutValue, Context);
	}
};


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
		, VertexFactory(GetScene().GetFeatureLevel(), "FStrandHairSceneProxy")
		, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))	
	{
		check(Component);
		check(Component->GroomAsset);
		check(Component->GroomAsset->GetNumHairGroups() > 0);
		check(Component->InterpolationOutput);
		ComponentId = Component->ComponentId.PrimIDValue;

		FHairStrandsVertexFactory::FDataType VFData;
		VFData.InterpolationOutput = Component->InterpolationOutput;

		const uint32 GroupCount = Component->GroomAsset->GetNumHairGroups();
		check(Component->GroomAsset->HairGroupsData.Num() == Component->HairGroupResources.Num());
		for (uint32 GroupIt=0;GroupIt<GroupCount; GroupIt++)
		{		
			const FHairGroupData& InGroupData = Component->GroomAsset->HairGroupsData[GroupIt];
			const FHairGroupDesc& InGroupDesc = Component->GroomGroupsDesc[GroupIt];

			const UGroomComponent::FHairGroupResource& GroupResources = Component->HairGroupResources[GroupIt];

			UMaterialInterface* Material = Component->GetMaterial(GroupIt);
			if (Material == nullptr || !Material->GetMaterialResource(GetScene().GetFeatureLevel())->IsUsedWithHairStrands())
			{
				Material = GEngine->HairDefaultMaterial;
			}

			#if RHI_RAYTRACING
			FRayTracingGeometry* RayTracingGeometry = nullptr;
			if (IsHairRayTracingEnabled() && GroupResources.RaytracingResources)
			{
				RayTracingGeometry = &GroupResources.RaytracingResources->RayTracingGeometry;
			}
			#endif

			HairGroup& OutGroupData = HairGroups.Add_GetRef(
			{
				Material
				#if RHI_RAYTRACING
				, RayTracingGeometry
				#endif
				, GroupResources.HairGroupPublicDatas
			});

		}

		FHairStrandsVertexFactory* LocalVertexFactory = &VertexFactory;
		ENQUEUE_RENDER_COMMAND(InitHairStrandsVertexFactory)(
			[LocalVertexFactory, VFData](FRHICommandListImmediate& RHICmdList)
		{
			LocalVertexFactory->SetData(VFData);
			LocalVertexFactory->InitResource();
		});
	}

	virtual ~FHairStrandsSceneProxy()
	{
		VertexFactory.ReleaseResource();
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

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		if (!VertexFactory.GetData().InterpolationOutput)
			return;

		bool bHasOneElementValid = false;
		for (FHairStrandsInterpolationOutput::HairGroup& HairGroup : VertexFactory.GetData().InterpolationOutput->HairGroups)
		{
			if (HairGroup.VFInput.VertexCount > 0)
			{
				bHasOneElementValid = true;
				break;
			}
		}
		if (!bHasOneElementValid) 
			return;

		const uint32 GroupCount = VertexFactory.GetData().InterpolationOutput->HairGroups.Num();

		QUICK_SCOPE_CYCLE_COUNTER(STAT_HairStrandsSceneProxy_GetDynamicMeshElements);


		FMaterialRenderProxy* MaterialProxy = nullptr;
		const EHairStrandsDebugMode DebugMode = GetHairStrandsDebugStrandsMode();
		if (DebugMode != EHairStrandsDebugMode::None)
		{
			float DebugModeScalar = 0;
			switch(DebugMode)
			{
			case EHairStrandsDebugMode::None						: DebugModeScalar =99.f; break;
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


			float HairMaxRadius = 0;
			for (FHairStrandsInterpolationOutput::HairGroup& HairGroup : VertexFactory.GetData().InterpolationOutput->HairGroups)
			{
				HairMaxRadius = FMath::Max(HairMaxRadius, HairGroup.VFInput.HairRadius);
			}

			const float HairClipLength = GetHairClipLength();
			auto DebugMaterial = new FHairDebugModeMaterialRenderProxy(
				GEngine->HairDebugMaterial ? GEngine->HairDebugMaterial->GetRenderProxy() : nullptr,
				DebugModeScalar, 0, HairMaxRadius, HairClipLength);
			Collector.RegisterOneFrameMaterialProxy(DebugMaterial);
			MaterialProxy = DebugMaterial;
		}
		
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
				{
					const HairGroup& GroupData = HairGroups[GroupIt];
					const uint32 HairVertexCount = VertexFactory.GetData().InterpolationOutput->HairGroups[GroupIt].VFInput.VertexCount;

					// Draw the mesh.
					FMeshBatch& Mesh = Collector.AllocateMesh();
					FMeshBatchElement& BatchElement = Mesh.Elements[0];
					BatchElement.IndexBuffer = nullptr;
					Mesh.bWireframe = false;
					Mesh.VertexFactory = &VertexFactory;
					Mesh.MaterialRenderProxy = MaterialProxy == nullptr ? GroupData.Material->GetRenderProxy() : MaterialProxy;
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
					BatchElement.NumPrimitives = 0;
					BatchElement.IndirectArgsBuffer = GroupData.PublicData->GetDrawIndirectBuffer().Buffer.GetReference();
					BatchElement.IndirectArgsOffset = 0;
					
					// Setup our vertex factor custom data
					BatchElement.VertexFactoryUserData = const_cast<void*>(reinterpret_cast<const void*>(GroupData.PublicData));

					BatchElement.MinVertexIndex = 0;
					BatchElement.MaxVertexIndex = HairVertexCount * 6;
					BatchElement.UserData = reinterpret_cast<void*>(uint64(ComponentId));
					Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
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
		const bool bIsViewModeValid = View->Family->ViewMode == VMI_Lit;

		const EHairStrandsDebugMode DebugMode = GetHairStrandsDebugStrandsMode();
		if (DebugMode != EHairStrandsDebugMode::None)
		{
			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = IsShown(View);
			Result.bShadowRelevance = IsShadowCast(View);
			Result.bDynamicRelevance = true;
			MaterialRelevance.SetPrimitiveViewRelevance(Result);
			return Result;
		}

		FPrimitiveViewRelevance Result;
		Result.bHairStrandsRelevance = bIsViewModeValid;

		// Special pass for hair strands geometry (not part of the base pass, and shadowing is handlded in a custom fashion)
		Result.bDrawRelevance = false;		
		Result.bShadowRelevance = false;
		Result.bRenderInMainPass = false;
		Result.bDynamicRelevance = true;

		// Selection only
		#if WITH_EDITOR
		{
			const bool bIsSelected = (IsSelected() || IsHovered()) && bIsViewModeValid;
			Result.bEditorStaticSelectionRelevance = bIsSelected;
			Result.bDrawRelevance = bIsSelected;
		}
		#endif
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		return Result;
	}

	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }

	uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }
private:

	uint32 ComponentId = 0;
	FHairStrandsVertexFactory VertexFactory;
	FMaterialRelevance MaterialRelevance;
	struct HairGroup
	{
		UMaterialInterface* Material = nullptr;
	#if RHI_RAYTRACING
		FRayTracingGeometry* RayTracingGeometry = nullptr;
	#endif
		FHairGroupPublicData* PublicData = nullptr;
	};
	TArray<HairGroup> HairGroups;
};

static void UpdateHairGroupsDesc(UGroomAsset* GroomAsset, TArray<FHairGroupDesc>& GroomGroupsDesc)
{
	if (!GroomAsset)
	{
		GroomGroupsDesc.Empty();
		return;
	}

	check(GroomAsset->HairGroupsInfo.Num() == GroomAsset->HairGroupsData.Num());

	const uint32 GroupCount = GroomAsset->HairGroupsInfo.Num();
	const bool bReinitOverride = GroupCount != GroomGroupsDesc.Num();
	if (bReinitOverride)
	{
		GroomGroupsDesc.SetNum(GroupCount);
	}

	for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
	{
		FHairGroupInfo& GroupInfo = GroomAsset->HairGroupsInfo[GroupIt];
		FHairGroupData& GroupData = GroomAsset->HairGroupsData[GroupIt];

		FHairGroupDesc& Desc = GroomGroupsDesc[GroupIt];
		Desc.GuideCount	= GroupInfo.NumGuides;
		Desc.HairCount	= GroupInfo.NumCurves;
		Desc.HairLength = GroupData.HairRenderData.StrandsCurves.MaxLength;

		if (bReinitOverride || Desc.HairWidth == 0)
		{
			Desc.HairWidth = GroupData.HairRenderData.StrandsCurves.MaxRadius * 2.0f;
		}
		if (bReinitOverride || Desc.HairShadowDensity == 0)
		{
			Desc.HairShadowDensity = GroupData.HairRenderData.HairDensity;
		}

		if (bReinitOverride)
		{
			Desc.ReInit();
		}
	}
}

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

	SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
}

FPrimitiveSceneProxy* UGroomComponent::CreateSceneProxy()
{
	if (!GroomAsset || GroomAsset->GetNumHairGroups() == 0 || GroomAsset->HairGroupsData[0].HairRenderData.GetNumCurves() == 0 || !InterpolationOutput || !InterpolationInput)
		return nullptr;

	return new FHairStrandsSceneProxy(this);
}


FBoxSphereBounds UGroomComponent::CalcBounds(const FTransform& InLocalToWorld) const
{
	FBox HairBox(ForceInit);
	if (GroomAsset && GroomAsset->GetNumHairGroups() > 0)
	{
		if (RegisteredSkeletalMeshComponent)
		{
			FBox LocalBound(EForceInit::ForceInit);
			for (const FHairGroupData& GroupData : GroomAsset->HairGroupsData)
			{
				LocalBound += GroupData.HairRenderData.BoundingBox;
			}
			FBox WorldBound = LocalBound.TransformBy(InLocalToWorld);

			const FVector MeshTranslation = RegisteredSkeletalMeshComponent->CalcBounds(InLocalToWorld).GetBox().GetCenter();
			const FVector LocalAnimationTranslation = MeshTranslation - WorldBound.GetCenter();

			WorldBound.Max += LocalAnimationTranslation;
			WorldBound.Min += LocalAnimationTranslation;
			return FBoxSphereBounds(WorldBound);
		}
		else
		{
			FBox LocalBounds(EForceInit::ForceInit);
			for (const FHairGroupData& GroupData : GroomAsset->HairGroupsData)
			{
				LocalBounds += GroupData.HairRenderData.BoundingBox;
			}
			return FBoxSphereBounds(LocalBounds.TransformBy(InLocalToWorld));
		}
	}
	else
	{
		return FBoxSphereBounds();
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

	const ERHIFeatureLevel::Type FeatureLevel = GetScene() ? GetScene()->GetFeatureLevel() : ERHIFeatureLevel::Num;
	if (!OverrideMaterial && GroomAsset && ElementIndex < GroomAsset->GetNumHairGroups() && FeatureLevel != ERHIFeatureLevel::Num)
	{
		if (UMaterialInterface* Material = GroomAsset->HairGroupsInfo[ElementIndex].Material)
		{
			if (!Material->GetMaterialResource(FeatureLevel)->IsUsedWithHairStrands())
			{
				Material = GEngine->HairDefaultMaterial;
			}
			return Material;
		}
		else
		{
			return GEngine->HairDefaultMaterial;
		}
	}

	if (OverrideMaterial == nullptr || !OverrideMaterial->GetMaterialResource(FeatureLevel)->IsUsedWithHairStrands())
	{
		OverrideMaterial = GEngine->HairDefaultMaterial;
	}

	return OverrideMaterial;
}

static FHairStrandsProjectionHairData::HairGroup ToProjectionHairData(FHairStrandsRootResource* In)
{
	check(IsInRenderingThread());

	FHairStrandsProjectionHairData::HairGroup Out = {};
	if (!In)
		return Out;

	Out.RootCount = In->RootCount;
	Out.RootPositionBuffer = In->RootPositionBuffer.SRV;
	Out.RootNormalBuffer = In->RootNormalBuffer.SRV;
	Out.VertexToCurveIndexBuffer = &In->VertexToCurveIndexBuffer;

	for (FHairStrandsRootResource::FMeshProjectionLOD& MeshLODData : In->MeshProjectionLODs)
	{
		FHairStrandsProjectionHairData::LODData& LODData = Out.LODDatas.AddDefaulted_GetRef();
		LODData.LODIndex = MeshLODData.LODIndex;
		LODData.RootTriangleIndexBuffer = &MeshLODData.RootTriangleIndexBuffer;
		LODData.RootTriangleBarycentricBuffer = &MeshLODData.RootTriangleBarycentricBuffer;
		
		LODData.RestPositionOffset				= &MeshLODData.RestRootOffset;
		LODData.RestRootTrianglePosition0Buffer = &MeshLODData.RestRootTrianglePosition0Buffer;
		LODData.RestRootTrianglePosition1Buffer = &MeshLODData.RestRootTrianglePosition1Buffer;
		LODData.RestRootTrianglePosition2Buffer = &MeshLODData.RestRootTrianglePosition2Buffer;

		LODData.DeformedPositionOffset				= &MeshLODData.DeformedRootOffset;
		LODData.DeformedRootTrianglePosition0Buffer = &MeshLODData.DeformedRootTrianglePosition0Buffer;
		LODData.DeformedRootTrianglePosition1Buffer = &MeshLODData.DeformedRootTrianglePosition1Buffer;
		LODData.DeformedRootTrianglePosition2Buffer = &MeshLODData.DeformedRootTrianglePosition2Buffer;

		LODData.Status = &MeshLODData.Status;
	}

	return Out;
}

FHairStrandsDatas* UGroomComponent::GetGuideStrandsDatas(uint32 GroupIndex)
{
	if (!GroomAsset || GroupIndex >= uint32(GroomAsset->GetNumHairGroups()))
	{
		return nullptr;
	}

	return &GroomAsset->HairGroupsData[GroupIndex].HairSimulationData;
}

FHairStrandsRestResource* UGroomComponent::GetGuideStrandsRestResource(uint32 GroupIndex)
{
	if (!GroomAsset || GroupIndex >= uint32(GroomAsset->GetNumHairGroups()))
	{
		return nullptr;
	}

	return GroomAsset->HairGroupsData[GroupIndex].HairSimulationRestResource;
}

FHairStrandsDeformedResource* UGroomComponent::GetGuideStrandsDeformedResource(uint32 GroupIndex)
{
	if (GroupIndex >= uint32(HairGroupResources.Num()))
	{
		return nullptr;
	}

	return HairGroupResources[GroupIndex].SimDeformedResources;
}

FHairStrandsRootResource* UGroomComponent::GetGuideStrandsRootResource(uint32 GroupIndex)
{
	if (GroupIndex >= uint32(HairGroupResources.Num()))
	{
		return nullptr;
	}

	return HairGroupResources[GroupIndex].SimRootResources;
}

template<typename T> void SafeDelete(T*& Data) 
{ 
	if (Data)
	{
		T* LocalData = Data;
		ENQUEUE_RENDER_COMMAND(FHairStrandsBuffers)(
			[LocalData](FRHICommandListImmediate& RHICmdList)
		{
			delete LocalData;
		});
		Data = nullptr;
	}

}
template<typename T> void SafeRelease(T*& Data) 
{ 
	if (Data)
	{
		T* LocalData = Data;
		ENQUEUE_RENDER_COMMAND(FHairStrandsBuffers)(
			[LocalData](FRHICommandListImmediate& RHICmdList)
		{
			LocalData->ReleaseResource();
			delete LocalData;
		});
		Data = nullptr;
	}
}

void CallbackMeshObjectCallback(
	FSkeletalMeshObjectCallbackData::EEventType Event,
	FSkeletalMeshObject* MeshObject,
	uint64 UserData) 
{
	ENQUEUE_RENDER_COMMAND(FHairStrandsMeshObjectUpdate)(
		[Event, MeshObject, UserData](FRHICommandListImmediate& RHICmdList)
	{
		const uint64 ComponentId = uint64(UserData & 0xFFFFFFFF);
		const EWorldType::Type WorldType = EWorldType::Type((UserData>>32) & 0xFFFFFFFF);
		if (Event == FSkeletalMeshObjectCallbackData::EEventType::Register || Event == FSkeletalMeshObjectCallbackData::EEventType::Update)
		{
			UpdateHairStrands(ComponentId, WorldType, MeshObject);
		}
		else
		{
			UpdateHairStrands(ComponentId, WorldType, nullptr);
		}
	});
}

static bool IsSimulationEnabled(const USceneComponent* Component)
{
	check(Component);

	// If the groom component has an Niagara component attached, we assume it has simulation capabilities
	bool bHasNiagaraSimulationComponent = false;
	for (int32 ChildIt = 0, ChildCount = Component->GetNumChildrenComponents(); ChildIt < ChildCount; ++ChildIt)
	{
		const USceneComponent* ChildComponent = Component->GetChildComponent(ChildIt);
		const UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(ChildComponent);
		if (NiagaraComponent != nullptr)
		{
			bHasNiagaraSimulationComponent = true;
			break;
		}
	}

	return bHasNiagaraSimulationComponent;
}

void UGroomComponent::OnChildAttached(USceneComponent* ChildComponent)
{
	const UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(ChildComponent);
	if (NiagaraComponent && InterpolationInput)
	{
		FHairStrandsInterpolationInput* LocalInterpolationInput = InterpolationInput;
		ENQUEUE_RENDER_COMMAND(FHairStrandsTick_UpdateSimulationEnable)(
			[LocalInterpolationInput](FRHICommandListImmediate& RHICmdList)
		{
			for (FHairStrandsInterpolationInput::FHairGroup& HairGroup : LocalInterpolationInput->HairGroups)
			{
				HairGroup.bIsSimulationEnable = true;
			}
		});
	}
}

void UGroomComponent::OnChildDetached(USceneComponent* ChildComponent)
{
	const UNiagaraComponent* NiagaraComponent = Cast<UNiagaraComponent>(ChildComponent);
	if (NiagaraComponent && InterpolationInput)
	{
		FHairStrandsInterpolationInput* LocalInterpolationInput = InterpolationInput;
		ENQUEUE_RENDER_COMMAND(FHairStrandsTick_UpdateSimulationDisable)(
			[LocalInterpolationInput](FRHICommandListImmediate& RHICmdList)
		{
			for (FHairStrandsInterpolationInput::FHairGroup& HairGroup : LocalInterpolationInput->HairGroups)
			{
				HairGroup.bIsSimulationEnable = false;
			}
		});
	}
}

void UGroomComponent::ResetSimulation()
{
	bResetSimulation = false;
	//UE_LOG(LogHairStrands, Warning, TEXT("Groom Reset = %d"), bResetSimulation);
}

void UGroomComponent::InitResources()
{
	ReleaseResources();
	bResetSimulation = true;
	//UE_LOG(LogHairStrands, Warning, TEXT("Groom Init = %d"), bResetSimulation);

	UpdateHairGroupsDesc(GroomAsset, GroomGroupsDesc);

	if (!GroomAsset || GroomAsset->GetNumHairGroups() == 0)
		return;

	InitializedResources = GroomAsset;

	const FPrimitiveComponentId LocalComponentId = ComponentId;
	EWorldType::Type WorldType = GetWorld() ? EWorldType::Type(GetWorld()->WorldType) : EWorldType::None;
	WorldType = WorldType == EWorldType::Inactive ? EWorldType::Editor : WorldType;

	// Insure the ticking of the Groom component always happens after the skeletalMeshComponent.
	USkeletalMeshComponent* SkeletalMeshComponent = bBindGroomToSkeletalMesh && GetAttachParent() ? Cast<USkeletalMeshComponent>(GetAttachParent()) : nullptr;
	const bool bHasValidSketalMesh = SkeletalMeshComponent && SkeletalMeshComponent->GetSkeletalMeshRenderData();

	// Always setup the callback, even if the SkeletalMeshData are not ready yet
	if (SkeletalMeshComponent)
	{
		FSkeletalMeshObjectCallbackData CallbackData;
		CallbackData.Run = CallbackMeshObjectCallback;
		CallbackData.UserData = (uint64(LocalComponentId.PrimIDValue) & 0xFFFFFFFF) | (uint64(WorldType) << 32);
		SkeletalMeshComponent->MeshObjectCallbackData = CallbackData;
	}

	if (bHasValidSketalMesh)
	{
		RegisteredSkeletalMeshComponent = SkeletalMeshComponent;
		AddTickPrerequisiteComponent(SkeletalMeshComponent);
		SkeletalMeshComponent->OnBoneTransformsFinalized.AddDynamic(this, &UGroomComponent::ResetSimulation);
	}

	const bool bIsSimulationEnable = IsSimulationEnabled(this);

	FTransform HairLocalToWorld = GetComponentTransform();
	FTransform SkinLocalToWorld = bBindGroomToSkeletalMesh && SkeletalMeshComponent ? SkeletalMeshComponent->GetComponentTransform() : FTransform::Identity;
	
	InterpolationOutput = new FHairStrandsInterpolationOutput();
	InterpolationInput = new FHairStrandsInterpolationInput();

	FHairStrandsDebugInfo DebugGroupInfo;
	int32 GroupIt = 0;
	for (FHairGroupData& GroupData : GroomAsset->HairGroupsData)
	{
		if (!GroupData.HairStrandsRestResource)
			return;

		FHairStrandsDebugInfo::HairGroup& DebugHairGroup = DebugGroupInfo.HairGroups.AddDefaulted_GetRef();
		{
			DebugHairGroup.MaxLength	= GroupData.HairRenderData.StrandsCurves.MaxLength;
			DebugHairGroup.MaxRadius	= GroupData.HairRenderData.StrandsCurves.MaxRadius;
			DebugHairGroup.VertexCount	= GroupData.HairRenderData.GetNumPoints();
			DebugHairGroup.CurveCount	= GroupData.HairRenderData.GetNumCurves();
		}

		FHairGroupResource& Res = HairGroupResources.AddDefaulted_GetRef();
		Res.InterpolationResource = new FHairStrandsInterpolationResource(GroupData.HairInterpolationData.RenderData, GroupData.HairSimulationData);
		BeginInitResource(Res.InterpolationResource);

		#if RHI_RAYTRACING
		if (IsHairRayTracingEnabled())
		{
			Res.RaytracingResources = new FHairStrandsRaytracingResource(GroupData.HairRenderData);
			BeginInitResource(Res.RaytracingResources);
		}
		#endif

		if (bHasValidSketalMesh)
		{
			const uint32 LODCount = SkeletalMeshComponent->GetNumLODs();
			if (LODCount > 0)
			{
				Res.RenRootResources = new FHairStrandsRootResource(&GroupData.HairRenderData, LODCount);
				Res.SimRootResources = new FHairStrandsRootResource(&GroupData.HairSimulationData, LODCount);
				BeginInitResource(Res.RenRootResources);
				BeginInitResource(Res.SimRootResources);
			}
		}
		
		Res.RenderRestResources = GroupData.HairStrandsRestResource;
		Res.SimRestResources = GroupData.HairSimulationRestResource;

		Res.RenderDeformedResources = new FHairStrandsDeformedResource(GroupData.HairRenderData.RenderData, false);
		Res.SimDeformedResources = new FHairStrandsDeformedResource(GroupData.HairSimulationData.RenderData, true);
		Res.ClusterCullingResources = new FHairStrandsClusterCullingResource(GroupData);

		const uint32 HairVertexCount = GroupData.HairRenderData.GetNumPoints();
		const uint32 GroupInstanceVertexCount = HairVertexCount * 6; // 6 vertex per point for a quad
		Res.HairGroupPublicDatas = new FHairGroupPublicData(GroupIt, GroupInstanceVertexCount, Res.ClusterCullingResources->ClusterCount, HairVertexCount);

		BeginInitResource(Res.RenderDeformedResources);
		BeginInitResource(Res.SimDeformedResources);
		BeginInitResource(Res.ClusterCullingResources);
		BeginInitResource(Res.HairGroupPublicDatas);

		const FVector RenderRestHairPositionOffset = Res.RenderRestResources->PositionOffset;
		const FVector SimRestHairPositionOffset = Res.SimRestResources->PositionOffset;

		Res.RenderDeformedResources->PositionOffset = RenderRestHairPositionOffset;
		Res.RenderRestResources->PositionOffset = RenderRestHairPositionOffset;
		Res.SimDeformedResources->PositionOffset = SimRestHairPositionOffset;
		Res.SimRestResources->PositionOffset = SimRestHairPositionOffset;

		FHairStrandsInterpolationOutput::HairGroup& InterpolationOutputGroup = InterpolationOutput->HairGroups.AddDefaulted_GetRef();
		FHairStrandsInterpolationInput::FHairGroup& InterpolationInputGroup = InterpolationInput->HairGroups.AddDefaulted_GetRef();

		check(GroupIt < GroomGroupsDesc.Num());
		InterpolationInputGroup.GroupDesc = GroomGroupsDesc[GroupIt];

		InterpolationOutputGroup.HairGroupPublicData = Res.HairGroupPublicDatas;

		// Rest post offset
		InterpolationInputGroup.InRenderHairPositionOffset = RenderRestHairPositionOffset;
		InterpolationInputGroup.InSimHairPositionOffset = SimRestHairPositionOffset;

		// For skinned groom, these value will be updated during TickComponent() call
		// Deformed sim & render are expressed within the referential (unlike rest pose)
		InterpolationInputGroup.OutHairPositionOffset = RenderRestHairPositionOffset;
		InterpolationInputGroup.OutHairPreviousPositionOffset = RenderRestHairPositionOffset;
		InterpolationInputGroup.bIsSimulationEnable = bIsSimulationEnable;

		GroupIt++;
	}

	FHairStrandsInterpolationData Interpolation;
	Interpolation.Input  = InterpolationInput;
	Interpolation.Output = InterpolationOutput;
	Interpolation.Function = ComputeHairStrandsInterpolation;
	Interpolation.ResetFunction = ResetHairStrandsInterpolation;

	FHairGroupResources* LocalResources = &HairGroupResources;
	const uint64 Id = LocalComponentId.PrimIDValue;
	ENQUEUE_RENDER_COMMAND(FHairStrandsBuffers)(
		[
			Id,
			Interpolation,
			LocalResources,
			HairLocalToWorld, SkinLocalToWorld,
			WorldType,
			DebugGroupInfo,
			bHasValidSketalMesh,
			SkeletalMeshComponent
		]
		(FRHICommandListImmediate& RHICmdList)
	{
		FHairStrandsProjectionHairData RenProjectionDatas;
		FHairStrandsProjectionHairData SimProjectionDatas;
		FHairStrandsPrimitiveResources PrimitiveResources;
		const uint32 GroupCount = LocalResources->Num();
		for (uint32 GroupIt=0;GroupIt<GroupCount; ++GroupIt)
		{
			FHairGroupResource& Res = (*LocalResources)[GroupIt];

			FHairStrandsInterpolationInput::FHairGroup& InputGroup 	= Interpolation.Input->HairGroups[GroupIt];
			FHairStrandsInterpolationOutput::HairGroup& OutputGroup	= Interpolation.Output->HairGroups[GroupIt];

			InputGroup.RenderRestPosePositionBuffer	= &Res.RenderRestResources->RestPositionBuffer;
			InputGroup.RenderAttributeBuffer		= &Res.RenderRestResources->AttributeBuffer;
			InputGroup.RenderVertexCount			=  Res.RenderRestResources->RenderData.RenderingPositions.Num() / FHairStrandsPositionFormat::ComponentCount;

			InputGroup.SimRestPosePositionBuffer	= &Res.SimRestResources->RestPositionBuffer;
			InputGroup.SimAttributeBuffer			= &Res.SimRestResources->AttributeBuffer;
			InputGroup.SimVertexCount				=  Res.SimRestResources->RenderData.RenderingPositions.Num() / FHairStrandsPositionFormat::ComponentCount;
			InputGroup.SimRootPointIndexBuffer		= &Res.InterpolationResource->SimRootPointIndexBuffer;

			InputGroup.Interpolation0Buffer			= &Res.InterpolationResource->Interpolation0Buffer;
			InputGroup.Interpolation1Buffer			= &Res.InterpolationResource->Interpolation1Buffer;
			
			InputGroup.ClusterCount					= Res.ClusterCullingResources->ClusterCount;
			InputGroup.ClusterVertexCount			= Res.ClusterCullingResources->VertexCount;
			InputGroup.VertexToClusterIdBuffer		= &Res.ClusterCullingResources->VertexToClusterIdBuffer;
			InputGroup.ClusterVertexIdBuffer		= &Res.ClusterCullingResources->ClusterVertexIdBuffer;
			InputGroup.ClusterIndexRadiusScaleInfoBuffer= &Res.ClusterCullingResources->ClusterIndexRadiusScaleInfoBuffer;

			#if RHI_RAYTRACING
			if (IsHairRayTracingEnabled())
			{
				InputGroup.RaytracingGeometry		= &Res.RaytracingResources->RayTracingGeometry;
				InputGroup.RaytracingPositionBuffer	= &Res.RaytracingResources->PositionBuffer;
				InputGroup.RaytracingVertexCount	=  Res.RaytracingResources->VertexCount;
			}
			#endif

			OutputGroup.SimDeformedPositionBuffer[0]	= &Res.SimDeformedResources->DeformedPositionBuffer[0];
			OutputGroup.SimDeformedPositionBuffer[1]	= &Res.SimDeformedResources->DeformedPositionBuffer[1];
			OutputGroup.RenderDeformedPositionBuffer[0] = &Res.RenderDeformedResources->DeformedPositionBuffer[0];
			OutputGroup.RenderDeformedPositionBuffer[1] = &Res.RenderDeformedResources->DeformedPositionBuffer[1];
			OutputGroup.RenderAttributeBuffer			= &Res.RenderRestResources->AttributeBuffer;
			OutputGroup.RenderMaterialBuffer			= &Res.RenderRestResources->MaterialBuffer;
			OutputGroup.RenderTangentBuffer				= &Res.RenderDeformedResources->TangentBuffer;
			OutputGroup.SimTangentBuffer				= &Res.SimDeformedResources->TangentBuffer;
			
			OutputGroup.RenderClusterAABBBuffer			= &Res.HairGroupPublicDatas->GetClusterAABBBuffer();
			OutputGroup.RenderGroupAABBBuffer			= &Res.HairGroupPublicDatas->GetGroupAABBBuffer();
			OutputGroup.ClusterInfoBuffer				= &Res.ClusterCullingResources->ClusterInfoBuffer;

			RenProjectionDatas.HairGroups.Add(ToProjectionHairData(Res.RenRootResources));
			SimProjectionDatas.HairGroups.Add(ToProjectionHairData(Res.SimRootResources));

			FHairStrandsPrimitiveResources::FHairGroup& Group = PrimitiveResources.Groups.AddDefaulted_GetRef();
			Group.ClusterAABBBuffer = &Res.HairGroupPublicDatas->GetClusterAABBBuffer();
			Group.GroupAABBBuffer	= &Res.HairGroupPublicDatas->GetGroupAABBBuffer();
			Group.ClusterCount		= Res.ClusterCullingResources->ClusterCount;
		}

		if (bHasValidSketalMesh)
		{		
			// Convert FSkeletalMeshRenderData into FHairStrandsProjectionMeshData for the initial projection
			FHairStrandsProjectionMeshData MeshData;
			FSkeletalMeshRenderData* RenderData = SkeletalMeshComponent->GetSkeletalMeshRenderData();

			uint32 LODIndex = 0;
			for (FSkeletalMeshLODRenderData& LODRenderData : RenderData->LODRenderData)
			{
				FHairStrandsProjectionMeshData::LOD& LOD = MeshData.LODs.AddDefaulted_GetRef();
				uint32 SectionIndex = 0;
				for (FSkelMeshRenderSection& InSection : LODRenderData.RenderSections)
				{
					FHairStrandsProjectionMeshData::Section& OutSection = LOD.Sections.AddDefaulted_GetRef();
					
					OutSection.PositionBuffer	= LODRenderData.StaticVertexBuffers.PositionVertexBuffer.GetSRV();
					OutSection.IndexBuffer		= LODRenderData.MultiSizeIndexContainer.GetIndexBuffer()->GetSRV();
					OutSection.TotalVertexCount	= LODRenderData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();
					OutSection.TotalIndexCount	= LODRenderData.MultiSizeIndexContainer.GetIndexBuffer()->Num();
					OutSection.NumPrimitives	= InSection.NumTriangles;
					OutSection.VertexBaseIndex	= InSection.BaseVertexIndex;
					OutSection.IndexBaseIndex	= InSection.BaseIndex;
					OutSection.SectionIndex		= SectionIndex;
					OutSection.LODIndex			= LODIndex;

					++SectionIndex;
				}
				++LODIndex;
			}

			// The offset is based on the center of the skeletal mesh (which is computed based on the physics capsules/boxes/...)
			const FVector MeshPositionOffset = SkeletalMeshComponent->CalcBounds(FTransform::Identity).GetBox().GetCenter();
			RunProjection(
				RHICmdList,
				HairLocalToWorld,
				MeshPositionOffset,
				MeshData, 
				RenProjectionDatas,
				SimProjectionDatas);
		}

		RegisterHairStrands(
			Id,
			WorldType,
			Interpolation,
			RenProjectionDatas,
			SimProjectionDatas,
			PrimitiveResources,
			DebugGroupInfo);
	});
}

void UGroomComponent::ReleaseResources()
{
	// Unregister component interpolation resources
	const FPrimitiveComponentId LocalComponentId = ComponentId;
	const uint64 Id = LocalComponentId.PrimIDValue;
	ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyInit)(
		[Id](FRHICommandListImmediate& RHICmdList)
	{
		UnregisterHairStrands(Id);
	});

	for (FHairGroupResource& Res : HairGroupResources)
	{
		SafeRelease(Res.InterpolationResource);
		SafeRelease(Res.RenRootResources);
		SafeRelease(Res.SimRootResources);
		SafeRelease(Res.RenderDeformedResources);
		SafeRelease(Res.ClusterCullingResources);
		SafeRelease(Res.HairGroupPublicDatas);
		SafeRelease(Res.SimDeformedResources);
	#if RHI_RAYTRACING
		SafeRelease(Res.RaytracingResources);
	#endif
	}
	HairGroupResources.Empty();

	// Delay destruction as resources reference by the interpolation 
	// structs are used on the rendering thread, 
	FHairStrandsInterpolationInput* Input = InterpolationInput;
	FHairStrandsInterpolationOutput* Output = InterpolationOutput;
	ENQUEUE_RENDER_COMMAND(FHairStrandsBuffers)(
		[Input, Output](FRHICommandListImmediate& RHICmdList)
	{
		delete Input;
		delete Output;
	});
	InterpolationInput = nullptr;
	InterpolationOutput = nullptr;
	InitializedResources = nullptr;

	// Insure the ticking of the Groom component always happens after the skeletalMeshComponent.
	if (RegisteredSkeletalMeshComponent)
	{
		RemoveTickPrerequisiteComponent(RegisteredSkeletalMeshComponent);
	}
	SkeletalPreviousPositionOffset = FVector::ZeroVector;
	RegisteredSkeletalMeshComponent = nullptr;

	USkeletalMeshComponent* SkeletalMeshComponent = GetAttachParent() ? Cast<USkeletalMeshComponent>(GetAttachParent()) : nullptr;
	if (SkeletalMeshComponent)
	{
		SkeletalMeshComponent->OnBoneTransformsFinalized.RemoveDynamic(this, &UGroomComponent::ResetSimulation);
		bResetSimulation = true;
	}

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

	InitResources();

#if WITH_EDITOR
	if (GroomAsset && !bIsGroomAssetCallbackRegistered)
	{
		GroomAsset->GetOnGroomAssetChanged().AddUObject(this, &UGroomComponent::Invalidate);
		bIsGroomAssetCallbackRegistered = true;
	}
	ValidateMaterials(false);
#endif
}

#if WITH_EDITOR
void UGroomComponent::Invalidate()
{
	MarkRenderStateDirty();
	ValidateMaterials(false);
}
#endif

void UGroomComponent::OnRegister()
{
	Super::OnRegister();

	if (!InitializedResources)
	{
		InitResources();
	}

	// Insure the ticking of the Groom component always happens after the skeletalMeshComponent.
	USkeletalMeshComponent* SkeletalMeshComponent = GetAttachParent() ? Cast<USkeletalMeshComponent>(GetAttachParent()) : nullptr;
	/*if (SkeletalMeshComponent)
	{
		SkeletalMeshComponent->OnBoneTransformsFinalized.AddDynamic(this, &UGroomComponent::ResetSimulation);
	}*/

	const EWorldType::Type WorldType = GetWorld() ? EWorldType::Type(GetWorld()->WorldType) : EWorldType::None;
	const uint64 Id = ComponentId.PrimIDValue;

	ENQUEUE_RENDER_COMMAND(FHairStrandsRegister)(
		[Id, WorldType](FRHICommandListImmediate& RHICmdList)
	{
		UpdateHairStrands(Id, WorldType);
	});
}

void UGroomComponent::OnUnregister()
{
	Super::OnUnregister();
}

void UGroomComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	ReleaseResources();

#if WITH_EDITOR
	if (bIsGroomAssetCallbackRegistered)
	{
		GroomAsset->GetOnGroomAssetChanged().RemoveAll(this);
		bIsGroomAssetCallbackRegistered = false;
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
	
	const EWorldType::Type WorldType = GetWorld() ? EWorldType::Type(GetWorld()->WorldType) : EWorldType::None;
	const uint64 Id = ComponentId.PrimIDValue;
	const ERHIFeatureLevel::Type FeatureLevel = GetWorld() ? ERHIFeatureLevel::Type(GetWorld()->FeatureLevel) : ERHIFeatureLevel::Num;

	FVector MeshPositionOffset = FVector::ZeroVector;
	USkeletalMeshComponent* SkeletalMeshComponent = RegisteredSkeletalMeshComponent;

	if (USkeletalMeshComponent* ParentComp = Cast<USkeletalMeshComponent>(GetAttachParent()))
	{
		if (ParentComp->GetNumBones() > 0)
		{
			const int32 BoneIndex = FMath::Min(1, ParentComp->GetNumBones()-1);
			const FMatrix NextBoneMatrix = ParentComp->GetBoneMatrix(BoneIndex);

			const float BoneDistance = FVector::DistSquared(PrevBoneMatrix.GetOrigin(), NextBoneMatrix.GetOrigin());
			if (ParentComp->GetTeleportDistanceThreshold() > 0.0 && BoneDistance >
				ParentComp->GetTeleportDistanceThreshold() * ParentComp->GetTeleportDistanceThreshold())
			{
				bResetSimulation = true;
			}
			else
			{
				bResetSimulation = false;
			}
			PrevBoneMatrix = NextBoneMatrix;
		}
	}
	if (SkeletalMeshComponent)
	{
		// The offset is based on the center of the skeletal mesh (which is computed based on the physics capsules/boxes/...)
		// For skinned mesh update the relative center of hair positions after deformation
		MeshPositionOffset = SkeletalMeshComponent->CalcBounds(FTransform::Identity).GetBox().GetCenter();
		{
			FHairGroupResources* LocalResources = &HairGroupResources;

			const FVector OutHairPositionOffset = MeshPositionOffset;
			const FVector OutHairPreviousPositionOffset = SkeletalPreviousPositionOffset;
			FHairStrandsInterpolationInput* LocalInterpolationInput = InterpolationInput;
			ENQUEUE_RENDER_COMMAND(FHairStrandsTick_OutHairPositionOffsetUpdate)(
				[OutHairPositionOffset, OutHairPreviousPositionOffset, LocalInterpolationInput, LocalResources](FRHICommandListImmediate& RHICmdList)
			{
				for (FHairStrandsInterpolationInput::FHairGroup& HairGroup : LocalInterpolationInput->HairGroups)
				{
					HairGroup.OutHairPositionOffset = OutHairPositionOffset;
					HairGroup.OutHairPreviousPositionOffset = OutHairPreviousPositionOffset;
				}

				// Update deformed (sim/render) hair position offsets. This is used by Niagara.
				for (FHairGroupResource& Res : *LocalResources)
				{
					if (Res.RenderDeformedResources)
						Res.RenderDeformedResources->PositionOffset = OutHairPositionOffset;

					if (Res.SimDeformedResources)
						Res.SimDeformedResources->PositionOffset = OutHairPositionOffset;
				}
			});

			// First frame will be wrong ... 
			SkeletalPreviousPositionOffset = OutHairPositionOffset;
		}

		// When a skeletal object with projection is enabled, activate the refresh of the bounding box to 
		// insure the component/proxy bounding box always lies onto the actual skinned mesh
		MarkRenderTransformDirty();
	}

	FHairGroupResources* LocalResources = &HairGroupResources;
	const FVector DeformedPositionCenter = GHairStrandsMeshUseRelativePosition > 0 ? MeshPositionOffset : FVector::ZeroVector;
	const FTransform SkinLocalToWorld = SkeletalMeshComponent ? SkeletalMeshComponent->GetComponentTransform() : FTransform();
	const FTransform HairLocalToWorld = GetComponentTransform();
	ENQUEUE_RENDER_COMMAND(FHairStrandsTick_TransformUpdate)(
		[Id, WorldType, HairLocalToWorld, SkinLocalToWorld, DeformedPositionCenter, FeatureLevel, LocalResources](FRHICommandListImmediate& RHICmdList)
	{		
		if (ERHIFeatureLevel::Num == FeatureLevel)
			return;

		UpdateHairStrands(Id, WorldType, HairLocalToWorld, SkinLocalToWorld, DeformedPositionCenter);

		// Update deformed (sim/render) triangles position offsets. This is used by Niagara.
		for (FHairGroupResource& Res : *LocalResources)
		{
			if (Res.RenRootResources)
			{
				for (FHairStrandsRootResource::FMeshProjectionLOD& MeshProjectionLOD : Res.RenRootResources->MeshProjectionLODs)
				{
					MeshProjectionLOD.DeformedRootOffset = DeformedPositionCenter;
				}
			}
			if (Res.SimRootResources)
			{
				for (FHairStrandsRootResource::FMeshProjectionLOD& MeshProjectionLOD : Res.SimRootResources->MeshProjectionLODs)
				{
					MeshProjectionLOD.DeformedRootOffset = DeformedPositionCenter;
				}
			}
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
		OutMaterials.Add(GEngine->HairDebugMaterial);
#endif
	OutMaterials.Add(GEngine->HairDefaultMaterial);
}

#if WITH_EDITOR
void UGroomComponent::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	const FName PropertyName = PropertyAboutToChange ? PropertyAboutToChange->GetFName() : NAME_None;
	const bool bAssetAboutToChanged = PropertyName == GET_MEMBER_NAME_CHECKED(UGroomComponent, GroomAsset);
	if (bAssetAboutToChanged)
	{
		// Remove the callback on the GroomAsset about to be replaced
		if (bIsGroomAssetCallbackRegistered && GroomAsset)
		{
			GroomAsset->GetOnGroomAssetChanged().RemoveAll(this);
		}
		bIsGroomAssetCallbackRegistered = false;
	}
}

void UGroomComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) 
{
	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	const FName PropertyName = PropertyThatChanged ? PropertyThatChanged->GetFName() : NAME_None;

	//  Init/release resources when setting the GroomAsset (or undoing)
	const bool bAssetChanged = PropertyName == GET_MEMBER_NAME_CHECKED(UGroomComponent, GroomAsset);
	const bool bRecreateResources =
		(bAssetChanged || PropertyThatChanged == nullptr) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UGroomComponent, bBindGroomToSkeletalMesh);

	if (bRecreateResources)
	{
		// Release the resources before Super::PostEditChangeProperty so that they get
		// re-initialized in OnRegister
		ReleaseResources();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);

	const bool bSupportSkinProjection = GetDefault<URendererSettings>()->bSupportSkinCacheShaders;
	if (!bSupportSkinProjection)
	{
		bBindGroomToSkeletalMesh = false;
	}

#if WITH_EDITOR
	if (bAssetChanged)
	{
		if (GroomAsset)
		{
			// Set the callback on the new GroomAsset being assigned
			GroomAsset->GetOnGroomAssetChanged().AddUObject(this, &UGroomComponent::Invalidate);
			bIsGroomAssetCallbackRegistered = true;
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
		PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, LodBias) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, LodAverageVertexPerPixel))
	{	
		UpdateHairGroupsDesc(GroomAsset, GroomGroupsDesc);

		if (InterpolationInput)
		{
			const int32 GroupCount = InterpolationInput->HairGroups.Num();
			for (int32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
			{
				InterpolationInput->HairGroups[GroupIt].GroupDesc = GroomGroupsDesc[GroupIt];
			}
		}
		MarkRenderStateDirty();
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

		if (FCString::Strcmp(*PropertyName, TEXT("bBindGroomToSkeletalMesh")) == 0)
		{
			return GetDefault<URendererSettings>()->bSupportSkinCacheShaders;
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

		FMaterialResource* Material = nullptr;

		if (OverrideMaterial)
		{
			Material = OverrideMaterial->GetMaterialResource(FeatureLevel);
		}
		else if (MaterialIt < uint32(GroomAsset->HairGroupsInfo.Num()) && GroomAsset->HairGroupsInfo[MaterialIt].Material)
		{
			Material = GroomAsset->HairGroupsInfo[MaterialIt].Material->GetMaterialResource(FeatureLevel);
		}

		if (Material)
		{
			if (!Material->IsUsedWithHairStrands())
			{
				if (bMapCheck)
				{
					FMessageLog("MapCheck").Warning()
						->AddToken(FUObjectToken::Create(GroomAsset))
						->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_HairStrandsMissingUseHairStrands", "Groom's material needs to enable the UseHairStrands option. Groom's material will be replaced with default hair strands shader.")))
						->AddToken(FMapErrorToken::Create(FMapErrors::InvalidHairStrandsMaterial));
				}
				else
				{
					UE_LOG(LogHairStrands, Warning, TEXT("[Groom] %s - Groom's material needs to enable the UseHairStrands option. Groom's material will be replaced with default hair strands shader."), *Name);
				}
			}
			if (!Material->GetShadingModels().HasShadingModel(MSM_Hair))
			{
				if (bMapCheck)
				{
					FMessageLog("MapCheck").Warning()
						->AddToken(FUObjectToken::Create(GroomAsset))
						->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_HairStrandsInvalidShadingModel", "Groom's material needs to have Hair shading model. Groom's material will be replaced with default hair strands shader.")))
						->AddToken(FMapErrorToken::Create(FMapErrors::InvalidHairStrandsMaterial));
				}
				else
				{
					UE_LOG(LogHairStrands, Warning, TEXT("[Groom] %s - Groom's material needs to have Hair shading model. Groom's material will be replaced with default hair strands shader."), *Name);
				}
			}
			if (Material->GetBlendMode() != BLEND_Opaque)
			{
				if (bMapCheck)
				{
					FMessageLog("MapCheck").Warning()
						->AddToken(FUObjectToken::Create(GroomAsset))
						->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_HairStrandsInvalidBlendMode", "Groom's material needs to have Opaque blend mode. Groom's material will be replaced with default hair strands shader.")))
						->AddToken(FMapErrorToken::Create(FMapErrors::InvalidHairStrandsMaterial));
				}
				else
				{
					UE_LOG(LogHairStrands, Warning, TEXT("[Groom] %s - Groom's material needs to have Opaque blend mode. Groom's material will be replaced with default hair strands shader."), *Name);
				}
			}
		}
		else
		{
			if (bMapCheck)
			{
				FMessageLog("MapCheck").Info()
					->AddToken(FUObjectToken::Create(GroomAsset))
					->AddToken(FTextToken::Create(LOCTEXT("MapCheck_Message_HairStrandsMissingMaterial", "Groom's material is not set and will fallback on default hair strands shader.")))
					->AddToken(FMapErrorToken::Create(FMapErrors::InvalidHairStrandsMaterial));
			}
			else
			{
				UE_LOG(LogHairStrands, Warning, TEXT("[Groom] %s - Groom's material is not set and will fallback on default hair strands shader."), *Name);
			}
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
			GroomComponent->CreateRenderState_Concurrent();
		}
	}
}

#undef LOCTEXT_NAMESPACE