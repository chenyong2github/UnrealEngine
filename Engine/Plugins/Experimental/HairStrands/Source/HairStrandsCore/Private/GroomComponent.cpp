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

static float GHairClipLength = -1;
static FAutoConsoleVariableRef CVarHairClipLength(TEXT("r.HairStrands.DebugClipLength"), GHairClipLength, TEXT("Clip hair strands which have a lenth larger than this value. (default is -1, no effect)"));
float GetHairClipLength() { return GHairClipLength > 0 ? GHairClipLength : 100000;  }

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
		
		const uint32 VertexCount = Component->GroomAsset->HairRenderData.GetNumPoints();
		const float MinHairRadius = 0; // Todo: Component->GroomAsset->HairRenderData.StrandsCurves.MinRadius;
		const float MaxHairRadius = Component->GroomAsset->HairRenderData.StrandsCurves.MaxRadius;
		const float MaxHairLength = Component->GroomAsset->HairRenderData.StrandsCurves.MaxLength;
		const float HairDensity = Component->GroomAsset->HairRenderData.HairDensity;
		const FVector& HairWorldOffset = Component->GroomAsset->HairRenderData.BoundingBox.GetCenter();

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
			const EHairStrandsDebugMode DebugMode = GetHairStrandsDebugMode();
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
	HairDensity = 1;
	MergeThreshold = 0.1f;
	InitializedResources = nullptr;
	Mobility = EComponentMobility::Movable;
	if (GEngine)
	{
		SetMaterial(0, GEngine->HairDefaultMaterial);
	}

	SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
}

FPrimitiveSceneProxy* UGroomComponent::CreateSceneProxy()
{
	if (!GroomAsset || GroomAsset->HairRenderData.GetNumCurves() == 0 || !InterpolationOutput || !InterpolationInput)
		return nullptr;

	return new FHairStrandsSceneProxy(this);
}


FBoxSphereBounds UGroomComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox HairBox(ForceInit);
	if (GroomAsset)
	{
		return FBoxSphereBounds(GroomAsset->HairRenderData.BoundingBox.TransformBy(LocalToWorld));
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
		int32 NumGroups = GroomAsset->RenderHairGroups.Num();
		return NumGroups > 0 ? NumGroups : 1;
	}
	return 1;
}

UMaterialInterface* UGroomComponent::GetMaterial(int32 ElementIndex) const
{
	UMaterialInterface* OverrideMaterial = Super::GetMaterial(ElementIndex);

	const ERHIFeatureLevel::Type FeatureLevel = GetScene() ? GetScene()->GetFeatureLevel() : ERHIFeatureLevel::Num;
	if (FeatureLevel == ERHIFeatureLevel::Num)
	{ 
		return GEngine->HairDefaultMaterial;
	}

	if (!OverrideMaterial && GroomAsset && ElementIndex < GroomAsset->RenderHairGroups.Num())
	{
		if (UMaterialInterface* Material = GroomAsset->RenderHairGroups[ElementIndex].Material)
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

void UGroomComponent::InitResources()
{
	ReleaseResources();

	if (GroomAsset && GroomAsset->HairStrandsResource)
	{
		check(GroomAsset->HairStrandsResource);

		InitializedResources = GroomAsset;

		FHairStrandsDatas* SimStrandDatas = &GroomAsset->HairSimulationData;

		FHairStrandsInterpolationDatas InterpolationDatas;
		InterpolationDatas.BuildInterpolationDatas(*SimStrandDatas, GroomAsset->HairRenderData);

		InterpolationResource = new FHairStrandsInterpolationResource(InterpolationDatas, *SimStrandDatas);
		BeginInitResource(InterpolationResource);

		#if RHI_RAYTRACING
		FHairStrandsRaytracingResource* LocalRaytracingResources = nullptr;
		if (IsRayTracingEnabled())
		{
			RaytracingResources = new FHairStrandsRaytracingResource(&GroomAsset->HairRenderData);
			BeginInitResource(RaytracingResources);
			LocalRaytracingResources = RaytracingResources;
		}
		#endif

		InterpolationOutput = new FHairStrandsInterpolationOutput();
		InterpolationInput = new FHairStrandsInterpolationInput();

		FHairStrandsInterpolationInput* Input = InterpolationInput;
		FHairStrandsInterpolationOutput* Output = InterpolationOutput;
		FHairStrandsResource* RenderResources = GroomAsset->HairStrandsResource;
		FHairStrandsResource* SimResources = GroomAsset->HairSimulationResource;
		FHairStrandsInterpolationResource* LocalInterpolationResource = InterpolationResource;

		check(Input);
		Input->HairRadius = GroomAsset->HairRenderData.StrandsCurves.MaxRadius;
		Input->HairWorldOffset = GroomAsset->HairRenderData.BoundingBox.GetCenter();

		const uint64 Id = (uint64)this;
		ENQUEUE_RENDER_COMMAND(FHairStrandsBuffers)(
			[Id, Input, Output, RenderResources, SimResources, LocalInterpolationResource
			#if RHI_RAYTRACING
			, LocalRaytracingResources
			#endif
			](FRHICommandListImmediate& RHICmdList)
		{
			Input->RenderRestPosePositionBuffer = &RenderResources->RestPositionBuffer;
			Input->RenderAttributeBuffer		= &RenderResources->AttributeBuffer;
			Input->RenderVertexCount			= RenderResources->StrandsDatas->GetNumPoints();

			Input->SimRestPosePositionBuffer	= &SimResources->RestPositionBuffer;
			Input->SimAttributeBuffer			= &SimResources->AttributeBuffer;
			Input->SimVertexCount				= SimResources->StrandsDatas->GetNumPoints();
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

			FHairStrandsInterpolation Interpolation;
			Interpolation.Input = Input;
			Interpolation.Output = Output;
			Interpolation.Function = ComputeHairStrandsInterpolation;
			RegisterHairStrands(Id, Interpolation);
		});

	}
}

void UGroomComponent::ReleaseResources()
{
	// Unregister component interpolation resources
	const uint64 Id = (uint64)this;
	ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyInit)(
		[Id](FRHICommandListImmediate& RHICmdList)
	{
		UnregisterHairStrands(Id);
	});

	SafeRelease(InterpolationResource);

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

#if RHI_RAYTRACING
	SafeRelease(RaytracingResources);
#endif
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
}

void UGroomComponent::OnUnregister()
{
	Super::OnUnregister();
}

void UGroomComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);	
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
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UGroomComponent, GroomAsset) ||
		PropertyThatChanged == nullptr)
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
