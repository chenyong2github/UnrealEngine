// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved. 

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
#include "Engine/RendererSettings.h"
#include "Animation/AnimationSettings.h"

static float GHairClipLength = -1;
static FAutoConsoleVariableRef CVarHairClipLength(TEXT("r.HairStrands.DebugClipLength"), GHairClipLength, TEXT("Clip hair strands which have a lenth larger than this value. (default is -1, no effect)"));
float GetHairClipLength() { return GHairClipLength > 0 ? GHairClipLength : 100000;  }

static int32 GHairStrandsMeshProjectionForceRefPoseEnable = 0;
static int32 GHairStrandsMeshProjectionForceLOD = -1;
static FAutoConsoleVariableRef CVarHairStrandsMeshProjectionForceRefPoseEnable(TEXT("r.HairStrands.MeshProjection.RefPose"), GHairStrandsMeshProjectionForceRefPoseEnable, TEXT("Enable/Disable reference pose"));
static FAutoConsoleVariableRef CVarHairStrandsMeshProjectionForceLOD(TEXT("r.HairStrands.MeshProjection.LOD"), GHairStrandsMeshProjectionForceLOD, TEXT("Force a specific LOD"));

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

		const FHairGroupData& GroupData = Component->GroomAsset->HairGroupsData[0];
		const uint32 VertexCount = GroupData.HairRenderData.GetNumPoints();
		const float MinHairRadius = 0; // Todo: Component->GroomAsset->HairRenderData.StrandsCurves.MinRadius;
		const float MaxHairRadius = GroupData.HairRenderData.StrandsCurves.MaxRadius;
		const float MaxHairLength = GroupData.HairRenderData.StrandsCurves.MaxLength;
		const float HairDensity = GroupData.HairRenderData.HairDensity;
		const FVector& HairWorldOffset = GroupData.HairRenderData.BoundingBox.GetCenter();

		check(Component->InterpolationOutput);
		FHairStrandsInterpolationOutput* LocalOutput = Component->InterpolationOutput;
		FHairStrandsVertexFactory* LocaVertexFactor = &VertexFactory;
		ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyInit)(
		[LocalOutput, LocaVertexFactor, HairWorldOffset, MinHairRadius, MaxHairRadius, MaxHairLength, HairDensity](FRHICommandListImmediate& RHICmdList)
		{
			const uint32 Offset = 0;
			FHairStrandsVertexFactory::FDataType Data;
			Data.MinStrandRadius = MinHairRadius;
			Data.MaxStrandRadius = MaxHairRadius;
			Data.MaxStrandLength = MaxHairLength;
			Data.HairDensity = HairDensity;
			Data.HairWorldOffset = HairWorldOffset;
			Data.InterpolationOutput = LocalOutput;

			LocaVertexFactor->SetData(Data);
			LocaVertexFactor->InitResource();
		});


		Material = Component->GetMaterial(0);
		if (Material == nullptr || !Material->GetMaterialResource(GetScene().GetFeatureLevel())->IsUsedWithHairStrands())
		{
			Material = GEngine->HairDefaultMaterial;
		}

		#if RHI_RAYTRACING
		if (IsRayTracingEnabled())
			RayTracingGeometry = Component->RaytracingResources ? &Component->RaytracingResources->RayTracingGeometry : nullptr;
		#endif
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
		if (!IsRayTracingEnabled() || !RayTracingGeometry || !RayTracingGeometry->RayTracingGeometryRHI.IsValid())
			return;

		check(RayTracingGeometry->Initializer.PositionVertexBuffer.IsValid());
		AddOpaqueRaytracingInstance(GetLocalToWorld(), RayTracingGeometry, RaytracingInstanceMask_ThinShadow, OutRayTracingInstances);
	}
#endif

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		const uint32 HairVertexCount = VertexFactory.GetData().InterpolationOutput ? VertexFactory.GetData().InterpolationOutput->VFInput.VertexCount : 0;
		if (HairVertexCount == 0) return;

		QUICK_SCOPE_CYCLE_COUNTER(STAT_HairStrandsSceneProxy_GetDynamicMeshElements);

		FMaterialRenderProxy* MaterialProxy = nullptr;
		{
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
				};

				const float HairMinRadius = VertexFactory.GetMinStrandRadius();
				const float HairMaxRadius = VertexFactory.GetMaxStrandRadius();
				const float HairClipLength = GetHairClipLength();
				auto DebugMaterial = new FHairDebugModeMaterialRenderProxy(
					GEngine->HairDebugMaterial ? GEngine->HairDebugMaterial->GetRenderProxy() : nullptr,
					DebugModeScalar, HairMinRadius, HairMaxRadius, HairClipLength);
				Collector.RegisterOneFrameMaterialProxy(DebugMaterial);
				MaterialProxy = DebugMaterial;
			}
			else
			{
				MaterialProxy = Material->GetRenderProxy();
			}
		}

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];
				// Draw the mesh.
				FMeshBatch& Mesh = Collector.AllocateMesh();
				FMeshBatchElement& BatchElement = Mesh.Elements[0];
				BatchElement.IndexBuffer = nullptr;
				Mesh.bWireframe = false;
				Mesh.VertexFactory = &VertexFactory;
				Mesh.MaterialRenderProxy = MaterialProxy;
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
				BatchElement.NumPrimitives = HairVertexCount * 2;
				BatchElement.MinVertexIndex = 0;
				BatchElement.MaxVertexIndex = HairVertexCount * 6;
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

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
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
		Result.bHairStrandsRelevance = true;

		// Special pass for hair strands geometry (not part of the base pass, and shadowing is handlded in a custom fashion)
		Result.bDrawRelevance = false;		
		Result.bShadowRelevance = false;
		Result.bRenderInMainPass = false;
		Result.bDynamicRelevance = true;

		// Selection only
		#if WITH_EDITOR
		{
			const bool bIsSelected = (IsSelected() || IsHovered());
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
	UMaterialInterface* Material = nullptr;
	FHairStrandsVertexFactory VertexFactory;
	FMaterialRelevance MaterialRelevance;
#if RHI_RAYTRACING
	FRayTracingGeometry* RayTracingGeometry = nullptr;
#endif
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
	HairDensity = 1;
	bSkinGroom = false;
	InitializedResources = nullptr;
	RenRootResources = nullptr;
	SimRootResources = nullptr;
	Mobility = EComponentMobility::Movable;
	MeshProjectionTickDelay = 0;
	MeshProjectionLODIndex = -1;
	MeshProjectionState = EMeshProjectionState::Invalid;
	if (GEngine)
	{
		SetMaterial(0, GEngine->HairDefaultMaterial);
	}

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
			// Transform the bounding box with an extra offset coming from the skin animation
			const FBoxSphereBounds MeshLocalBound = RegisteredSkeletalMeshComponent->CalcBounds(FTransform::Identity);
			const FVector HairTranslation = GroomAsset->HairGroupsData[0].HairRenderData.BoundingBox.GetCenter();
			const FVector MeshTranslation = MeshLocalBound.GetSphere().Center;
			const FVector LocalTranslation = MeshTranslation - HairTranslation;

			FTransform LocalToWorld = InLocalToWorld;
			LocalToWorld.SetLocation(LocalTranslation + InLocalToWorld.GetLocation());
			return FBoxSphereBounds(GroomAsset->HairGroupsData[0].HairRenderData.BoundingBox.TransformBy(LocalToWorld));
		}
		else
		{
			return FBoxSphereBounds(GroomAsset->HairGroupsData[0].HairRenderData.BoundingBox.TransformBy(InLocalToWorld));
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

static FHairStrandsProjectionHairData ToProjectionHairData(FHairStrandsRootResource* In)
{
	FHairStrandsProjectionHairData Out = {};
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
		
		LODData.RestRootCenter					= MeshLODData.RestRootCenter;
		LODData.RestRootTrianglePosition0Buffer = &MeshLODData.RestRootTrianglePosition0Buffer;
		LODData.RestRootTrianglePosition1Buffer = &MeshLODData.RestRootTrianglePosition1Buffer;
		LODData.RestRootTrianglePosition2Buffer = &MeshLODData.RestRootTrianglePosition2Buffer;

		LODData.DeformedRootCenter					= MeshLODData.DeformedRootCenter;
		LODData.DeformedRootTrianglePosition0Buffer = &MeshLODData.DeformedRootTrianglePosition0Buffer;
		LODData.DeformedRootTrianglePosition1Buffer = &MeshLODData.DeformedRootTrianglePosition1Buffer;
		LODData.DeformedRootTrianglePosition2Buffer = &MeshLODData.DeformedRootTrianglePosition2Buffer;
		LODData.bIsValid = 
			MeshLODData.Status == FHairStrandsRootResource::FMeshProjectionLOD::EStatus::Projected ||
			MeshLODData.Status == FHairStrandsRootResource::FMeshProjectionLOD::EStatus::Initialized;
	}

	return Out;
}

FHairStrandsDatas* UGroomComponent::GetGuideStrandsDatas()
{
	if (!GroomAsset || GroomAsset->GetNumHairGroups() == 0)
	{
		return nullptr;
	}

	return &GroomAsset->HairGroupsData[0].HairSimulationData;
}

FHairStrandsResource* UGroomComponent::GetGuideStrandsResource()
{
	if (!GroomAsset || GroomAsset->GetNumHairGroups() == 0)
	{
		return nullptr;
	}

	return GroomAsset->HairGroupsData[0].HairSimulationResource;
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

void UGroomComponent::InitResources()
{
	ReleaseResources();

	if (GroomAsset && GroomAsset->GetNumHairGroups() > 0 && GroomAsset->HairGroupsData[0].HairStrandsResource)
	{
		check(GroomAsset->HairGroupsData[0].HairStrandsResource);

		InitializedResources = GroomAsset;

		FHairStrandsDebugInfo::FGroupInfos DebugGroupInfos;
		for (const FHairGroupData GroupData : GroomAsset->HairGroupsData)
		{
			FHairStrandsDebugInfo::FGroupInfo& DebugGroupInfo = DebugGroupInfos.AddDefaulted_GetRef();
			DebugGroupInfo.MaxLength	= GroupData.HairRenderData.StrandsCurves.MaxLength;
			DebugGroupInfo.MaxRadius	= GroupData.HairRenderData.StrandsCurves.MaxRadius;
			DebugGroupInfo.VertexCount	= GroupData.HairRenderData.GetNumPoints();
			DebugGroupInfo.CurveCount	= GroupData.HairRenderData.GetNumCurves();
		}

		const FHairGroupData& GroupData = GroomAsset->HairGroupsData[0];

		InterpolationResource = new FHairStrandsInterpolationResource(GroupData.HairInterpolationData.RenderData, GroupData.HairSimulationData);
		BeginInitResource(InterpolationResource);

		#if RHI_RAYTRACING
		FHairStrandsRaytracingResource* LocalRaytracingResources = nullptr;
		if (IsRayTracingEnabled())
		{
			RaytracingResources = new FHairStrandsRaytracingResource(GroupData.HairRenderData);
			BeginInitResource(RaytracingResources);
			LocalRaytracingResources = RaytracingResources;
		}
		#endif


		const FPrimitiveComponentId LocalComponentId = ComponentId;
		EWorldType::Type WorldType = GetWorld() ? EWorldType::Type(GetWorld()->WorldType) : EWorldType::None;
		WorldType = WorldType == EWorldType::Inactive ? EWorldType::Editor : WorldType;

		// Insure the ticking of the Groom component always happens after the skeletalMeshComponent.
		USkeletalMeshComponent* SkeletalMeshComponent = bSkinGroom && GetAttachParent() ? Cast<USkeletalMeshComponent>(GetAttachParent()) : nullptr;
		if (SkeletalMeshComponent)
		{
			AddTickPrerequisiteComponent(SkeletalMeshComponent);
			RegisteredSkeletalMeshComponent = SkeletalMeshComponent;

			FSkeletalMeshObjectCallbackData CallbackData;
			CallbackData.Run = CallbackMeshObjectCallback;
			CallbackData.UserData = (uint64(LocalComponentId.PrimIDValue) & 0xFFFFFFFF) | (uint64(WorldType) << 32);
			SkeletalMeshComponent->MeshObjectCallbackData = CallbackData;

			const uint32 LODCount = SkeletalMeshComponent->GetNumLODs();
			if (LODCount > 0)
			{
				RenRootResources = new FHairStrandsRootResource(&GroupData.HairRenderData, LODCount);
				SimRootResources = new FHairStrandsRootResource(&GroupData.HairSimulationData, LODCount);
				BeginInitResource(RenRootResources);
				BeginInitResource(SimRootResources);
			}
		}
		
		InterpolationOutput = new FHairStrandsInterpolationOutput();
		InterpolationInput = new FHairStrandsInterpolationInput();

		FHairStrandsInterpolationInput* Input = InterpolationInput;
		FHairStrandsInterpolationOutput* Output = InterpolationOutput;
		FHairStrandsResource* RenderResources = GroupData.HairStrandsResource;
		FHairStrandsResource* SimResources = GroupData.HairSimulationResource;
		FHairStrandsInterpolationResource* LocalInterpolationResource = InterpolationResource;

		check(Input);
		Input->HairRadius = GroupData.HairRenderData.StrandsCurves.MaxRadius;
		Input->HairWorldOffset = GroupData.HairRenderData.BoundingBox.GetCenter();


		FHairStrandsRootResource* LocalRenRootResources = RenRootResources;
		FHairStrandsRootResource* LocalSimRootResources = SimRootResources;
		FTransform HairLocalToWorld = GetComponentTransform();
		FTransform SkinLocalToWorld = bSkinGroom && SkeletalMeshComponent ? SkeletalMeshComponent->GetComponentTransform() : FTransform::Identity;
		
		const uint64 Id = LocalComponentId.PrimIDValue;
		ENQUEUE_RENDER_COMMAND(FHairStrandsBuffers)(
			[Id, Input, Output, 
			RenderResources, SimResources, LocalRenRootResources, LocalSimRootResources, LocalInterpolationResource,
			HairLocalToWorld, SkinLocalToWorld,
			WorldType,
			DebugGroupInfos
			#if RHI_RAYTRACING
			, LocalRaytracingResources
			#endif
			](FRHICommandListImmediate& RHICmdList)
		{
			Input->RenderRestPosePositionBuffer = &RenderResources->RestPositionBuffer;
			Input->RenderAttributeBuffer		= &RenderResources->AttributeBuffer;
			Input->RenderVertexCount			= RenderResources->RenderData.RenderingPositions.Num() / FHairStrandsPositionFormat::ComponentCount;

			Input->SimRestPosePositionBuffer	= &SimResources->RestPositionBuffer;
			Input->SimAttributeBuffer			= &SimResources->AttributeBuffer;
			Input->SimVertexCount				= SimResources->RenderData.RenderingPositions.Num() / FHairStrandsPositionFormat::ComponentCount;
			Input->SimRootPointIndexBuffer		= &LocalInterpolationResource->SimRootPointIndexBuffer;

			Input->Interpolation0Buffer			= &LocalInterpolationResource->Interpolation0Buffer;
			Input->Interpolation1Buffer			= &LocalInterpolationResource->Interpolation1Buffer;

			#if RHI_RAYTRACING
			if (IsRayTracingEnabled())
			{
				Input->RaytracingGeometry		= &LocalRaytracingResources->RayTracingGeometry;
				Input->RaytracingPositionBuffer	= &LocalRaytracingResources->PositionBuffer;
				Input->RaytracingVertexCount	= LocalRaytracingResources->VertexCount;
			}
			#endif

			Output->SimDeformedPositionBuffer[0]	= &SimResources->DeformedPositionBuffer[0];
			Output->SimDeformedPositionBuffer[1]	= &SimResources->DeformedPositionBuffer[1];
			Output->RenderDeformedPositionBuffer[0] = &RenderResources->DeformedPositionBuffer[0];
			Output->RenderDeformedPositionBuffer[1] = &RenderResources->DeformedPositionBuffer[1];
			Output->RenderAttributeBuffer			= &RenderResources->AttributeBuffer;
			Output->RenderTangentBuffer				= &RenderResources->TangentBuffer;
			Output->SimTangentBuffer				= &SimResources->TangentBuffer;

			FHairStrandsProjectionHairData RenProjectionData = ToProjectionHairData(LocalRenRootResources);
			FHairStrandsProjectionHairData SimProjectionData = ToProjectionHairData(LocalSimRootResources);

			FHairStrandsInterpolationData Interpolation;
			Interpolation.Input = Input;
			Interpolation.Output = Output;
			Interpolation.Function = ComputeHairStrandsInterpolation;

			RegisterHairStrands(
				Id,
				WorldType,
				Interpolation,
				RenProjectionData, 
				SimProjectionData,
				DebugGroupInfos);
		});

	}
}

void UGroomComponent::ReleaseResources()
{
	// Unregister component interpolation resources
	const FPrimitiveComponentId LocalComponentId = ComponentId;
	const EWorldType::Type WorldType = GetWorld() ? EWorldType::Type(GetWorld()->WorldType) : EWorldType::None;
	const uint64 Id = LocalComponentId.PrimIDValue;
	ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyInit)(
		[Id, WorldType](FRHICommandListImmediate& RHICmdList)
	{
		UnregisterHairStrands(Id, WorldType);
	});

	SafeRelease(InterpolationResource);
	SafeRelease(RenRootResources);
	SafeRelease(SimRootResources);

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

	MeshProjectionLODIndex = -1;
	MeshProjectionTickDelay = 0;
	MeshProjectionState = EMeshProjectionState::Invalid;

#if RHI_RAYTRACING
	SafeRelease(RaytracingResources);
#endif

	// Insure the ticking of the Groom component always happens after the skeletalMeshComponent.
	if (RegisteredSkeletalMeshComponent)
	{
		RemoveTickPrerequisiteComponent(RegisteredSkeletalMeshComponent);
		RegisteredSkeletalMeshComponent = nullptr;
	}
}

void UGroomComponent::PostLoad()
{
	Super::PostLoad();

	if (GroomAsset)
	{
		// Make sure that the asset initialized its resources first since the component needs them to initialize its own resources
		GroomAsset->ConditionalPostLoad();
	}

	if (!IsTemplate())
	{
		InitResources();
	}
}

void UGroomComponent::OnRegister()
{
	Super::OnRegister();

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

void UGroomComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);	
	
	const EWorldType::Type WorldType = GetWorld() ? EWorldType::Type(GetWorld()->WorldType) : EWorldType::None;
	const uint64 Id = ComponentId.PrimIDValue;
	const ERHIFeatureLevel::Type FeatureLevel = GetWorld() ? ERHIFeatureLevel::Type(GetWorld()->FeatureLevel) : ERHIFeatureLevel::Num;

	USkeletalMeshComponent* SkeletalMeshComponent = RegisteredSkeletalMeshComponent;	
	if (SkeletalMeshComponent)
	{
		const uint32 SkeletalLODCount = SkeletalMeshComponent->GetNumLODs();
		const uint32 ResourceLODCount = RenRootResources ? RenRootResources->MeshProjectionLODs.Num() : 0;
		if (SkeletalLODCount != ResourceLODCount)
		{

			FHairStrandsRootResource* LocalRenRootResources = SkeletalLODCount > 0 ? new FHairStrandsRootResource((GroomAsset && GroomAsset->GetNumHairGroups() > 0) ? &GroomAsset->HairGroupsData[0].HairRenderData : nullptr, SkeletalLODCount) : nullptr;
			FHairStrandsRootResource* LocalSimRootResources = SkeletalLODCount > 0 ? new FHairStrandsRootResource((GroomAsset && GroomAsset->GetNumHairGroups() > 0) ? &GroomAsset->HairGroupsData[0].HairSimulationData : nullptr, SkeletalLODCount) : nullptr;

			auto InitRootResource = [] (FHairStrandsRootResource*& PersistentResources, FHairStrandsRootResource* LocalResources)
			{
				SafeRelease(PersistentResources);
				PersistentResources = LocalResources;
				if (PersistentResources)
				{
					BeginInitResource(PersistentResources);
				}
			};
			InitRootResource(RenRootResources, LocalRenRootResources);
			InitRootResource(SimRootResources, LocalSimRootResources);

			FTransform HairLocalToWorld = GetComponentTransform();
			ENQUEUE_RENDER_COMMAND(FHairStrandsTick_LODUpdate)(
				[Id, WorldType, LocalRenRootResources, LocalSimRootResources, HairLocalToWorld](FRHICommandListImmediate& RHICmdList)
			{
				FHairStrandsProjectionHairData RenProjectionData = ToProjectionHairData(LocalRenRootResources);
				FHairStrandsProjectionHairData SimProjectionData = ToProjectionHairData(LocalSimRootResources);
				UpdateHairStrands(Id, WorldType, HairLocalToWorld, RenProjectionData, SimProjectionData);
			});
		}

		if (MeshProjectionState != EMeshProjectionState::Completed && MeshProjectionTickDelay == 0)
		{

			if (MeshProjectionState == EMeshProjectionState::Invalid)
			{
				MeshProjectionLODIndex = 0;
				MeshProjectionState = EMeshProjectionState::WaitForData;
			}

			if (MeshProjectionLODIndex == SkeletalMeshComponent->GetNumLODs())
			{
				SkeletalMeshComponent->SetForceRefPose(false);
				SkeletalMeshComponent->SetForcedLOD(0);
				MeshProjectionState = EMeshProjectionState::Completed;
			}

			if (MeshProjectionLODIndex < SkeletalMeshComponent->GetNumLODs())
			{
				SkeletalMeshComponent->SetForceRefPose(true);
				SkeletalMeshComponent->SetForcedLOD(MeshProjectionLODIndex + 1);

				const FVector RestRootCenter = SkeletalMeshComponent->Bounds.GetSphere().Center;
				const uint32 LODIndex = MeshProjectionLODIndex;
				ENQUEUE_RENDER_COMMAND(FHairStrandsTick_Projection)(
					[Id, WorldType, FeatureLevel, LODIndex, RestRootCenter](FRHICommandListImmediate& RHICmdList)
				{
					AddHairStrandsProjectionQuery(RHICmdList, Id, WorldType, LODIndex, RestRootCenter);
				});
				MeshProjectionTickDelay += 5;
				++MeshProjectionLODIndex;
			}
		}
		else
		{
			// For debug purpose
			const int32 ForceLOD = GHairStrandsMeshProjectionForceLOD >= 0 ? FMath::Clamp(GHairStrandsMeshProjectionForceLOD, 0, SkeletalMeshComponent->GetNumLODs() - 1) : -1;
			SkeletalMeshComponent->SetForcedLOD(ForceLOD + 1);
			SkeletalMeshComponent->SetForceRefPose(GHairStrandsMeshProjectionForceRefPoseEnable > 0);
		}

		// When a skeletal object with projection is enabled, activate the refresh of the bounding box to 
		// insure the component/proxy bounding box alwaws lies onto the actual skinned mesh
		MarkRenderTransformDirty();
	}

	if (MeshProjectionTickDelay > 0)
	{
		MeshProjectionTickDelay--;
	}

	const FVector WorldsBoundsCenter  = SkeletalMeshComponent ? SkeletalMeshComponent->Bounds.GetSphere().Center : FVector::ZeroVector;
	const FTransform SkinLocalToWorld = SkeletalMeshComponent ? SkeletalMeshComponent->GetComponentTransform() : FTransform();
	const FTransform HairLocalToWorld = GetComponentTransform();
	ENQUEUE_RENDER_COMMAND(FHairStrandsTick_TransformUpdate)(
		[Id, WorldType, HairLocalToWorld, SkinLocalToWorld, WorldsBoundsCenter, FeatureLevel](FRHICommandListImmediate& RHICmdList)
	{		
		if (ERHIFeatureLevel::Num == FeatureLevel)
			return;

		UpdateHairStrands(Id, WorldType, HairLocalToWorld, SkinLocalToWorld, WorldsBoundsCenter);
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
void UGroomComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) 
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	UProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	const FName PropertyName = PropertyThatChanged ? PropertyThatChanged->GetFName() : NAME_None;

	//  Init/release resources when setting the GroomAsset (or undoing)
	const bool bRecreateResources =
		(PropertyName == GET_MEMBER_NAME_CHECKED(UGroomComponent, GroomAsset) || PropertyThatChanged == nullptr) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UGroomComponent, bSkinGroom);

	const bool bSupportSkinProjection = GetDefault<URendererSettings>()->bSupportSkinCacheShaders && !UAnimationSettings::Get()->bTickAnimationOnSkeletalMeshInit;
	if (!bSupportSkinProjection)
	{
		bSkinGroom = false;
	}

	if (bRecreateResources)
	{
		if (GroomAsset != InitializedResources)
		{
			if (GroomAsset)
			{
				InitResources();
			}
			else
			{
				ReleaseResources();
			}
		}
	}
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
