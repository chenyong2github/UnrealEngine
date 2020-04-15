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
#include "UObject/ConstructorHelpers.h"

static float GHairClipLength = -1;
static FAutoConsoleVariableRef CVarHairClipLength(TEXT("r.HairStrands.DebugClipLength"), GHairClipLength, TEXT("Clip hair strands which have a lenth larger than this value. (default is -1, no effect)"));
float GetHairClipLength() { return GHairClipLength > 0 ? GHairClipLength : 100000;  }

#define LOCTEXT_NAMESPACE "GroomComponent"

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

static EHairMaterialCompatibility IsHairMaterialCompatible(UMaterialInterface* Material, ERHIFeatureLevel::Type FeatureLevel)
{
	if (Material)
	{
		FMaterialResource* MaterialResources = Material->GetMaterialResource(FeatureLevel);
		if (!MaterialResources)
		{
			return EHairMaterialCompatibility::Invalid_IsNull;
		}
		if (!MaterialResources->IsUsedWithHairStrands())
		{
			return EHairMaterialCompatibility::Invalid_UsedWithHairStrands;
		}
		if (!MaterialResources->GetShadingModels().HasShadingModel(MSM_Hair))
		{
			return EHairMaterialCompatibility::Invalid_ShadingModel;
		}
		if (MaterialResources->GetBlendMode() != BLEND_Opaque)
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
		, VertexFactory(GetScene().GetFeatureLevel(), "FStrandHairSceneProxy")
		, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))	
	{
		check(Component);
		check(Component->GroomAsset);
		check(Component->GroomAsset->GetNumHairGroups() > 0);
		check(Component->InterpolationOutput);
		ComponentId = Component->ComponentId.PrimIDValue;
		HairDebugMaterial = Component->HairDebugMaterial;

		FHairStrandsVertexFactory::FDataType VFData;
		VFData.InterpolationOutput = Component->InterpolationOutput;

		check(Component->HairGroupResources);
		check(Component->HairGroupResources->HairGroups.Num());

		const uint32 GroupCount = Component->GroomAsset->GetNumHairGroups();
		check(Component->GroomAsset->HairGroupsData.Num() == Component->HairGroupResources->HairGroups.Num());
		for (uint32 GroupIt=0;GroupIt<GroupCount; GroupIt++)
		{		
			const FHairGroupData& InGroupData = Component->GroomAsset->HairGroupsData[GroupIt];
			const FHairGroupDesc& InGroupDesc = Component->GroomGroupsDesc[GroupIt];

			const UGroomComponent::FHairGroupResource& GroupResources = Component->HairGroupResources->HairGroups[GroupIt];

			UMaterialInterface* Material = Component->GetMaterial(GroupIt);
			if (Material == nullptr || !Material->GetMaterialResource(GetScene().GetFeatureLevel())->IsUsedWithHairStrands())
			{
				Material =  UMaterial::GetDefaultMaterial(MD_Surface);
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
		{
			return;
		}

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
		{
			return;
		}

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
			auto DebugMaterial = new FHairDebugModeMaterialRenderProxy(HairDebugMaterial ? HairDebugMaterial->GetRenderProxy() : nullptr, DebugModeScalar, 0, HairMaxRadius, HairClipLength);
			Collector.RegisterOneFrameMaterialProxy(DebugMaterial);
			MaterialProxy = DebugMaterial;
		}
		
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
					const uint32 HairVertexCount = VertexFactory.GetData().InterpolationOutput->HairGroups[GroupIt].VFInput.VertexCount;

					FMaterialRenderProxy* MaterialRenderProxy = MaterialProxy == nullptr ? GroupData.Material->GetRenderProxy() : MaterialProxy;
					if (MaterialRenderProxy == nullptr)
					{
						continue;
					}

					// Draw the mesh.
					FMeshBatch& Mesh = Collector.AllocateMesh();
					FMeshBatchElement& BatchElement = Mesh.Elements[0];
					BatchElement.IndexBuffer = nullptr;
					Mesh.bWireframe = false;
					Mesh.VertexFactory = &VertexFactory;
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
			Result.bShadowRelevance = IsShadowCast(View);
			Result.bDynamicRelevance = true;
			MaterialRelevance.SetPrimitiveViewRelevance(Result);
			return Result;
		}

		FPrimitiveViewRelevance Result;
		Result.bHairStrands = bIsViewModeValid && IsShown(View);

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

	uint32 ComponentId = 0;
	FHairStrandsVertexFactory VertexFactory;
	FMaterialRelevance MaterialRelevance;
	UMaterialInterface* HairDebugMaterial = nullptr;
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

	SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HairDebugMaterialRef(TEXT("/HairStrands/Materials/HairDebugMaterial.HairDebugMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HairDefaultMaterialRef(TEXT("/HairStrands/Materials/HairDefaultMaterial.HairDefaultMaterial"));

	HairDebugMaterial = HairDebugMaterialRef.Object;
	HairDefaultMaterial = HairDefaultMaterialRef.Object;
}

void UGroomComponent::UpdateHairGroupsDesc()
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
		const FHairGroupInfo& GroupInfo = GroomAsset->HairGroupsInfo[GroupIt];
		const FHairGroupData& GroupData = GroomAsset->HairGroupsData[GroupIt];

		FHairGroupDesc& Desc = GroomGroupsDesc[GroupIt];
		if (bReinitOverride)
		{
			Desc.ReInit();
		}
		Desc.GuideCount = GroupInfo.NumGuides;
		Desc.HairCount  = GroupInfo.NumCurves;
		Desc.HairLength = GroupData.HairRenderData.StrandsCurves.MaxLength;

		if (bReinitOverride || Desc.HairWidth == 0)
		{
			Desc.HairWidth = GroupData.HairRenderData.StrandsCurves.MaxRadius * 2.0f;
		}
		if (bReinitOverride || Desc.HairShadowDensity == 0)
		{
			Desc.HairShadowDensity = GroupData.HairRenderData.HairDensity;
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
	static UNiagaraSystem* CosseratRodsSystem = LoadObject<UNiagaraSystem>(nullptr, TEXT("/HairStrands/Emitters/GroomRodsSystem.GroomRodsSystem"));
	static UNiagaraSystem* AngularSpringsSystem = LoadObject<UNiagaraSystem>(nullptr, TEXT("/HairStrands/Emitters/GroomSpringsSystem.GroomSpringsSystem"));

	const int32 NumGroups = GroomAsset ? GroomAsset->HairGroupsPhysics.Num() : 0;
	const int32 NumComponents = FMath::Max(NumGroups, NiagaraComponents.Num());

	TArray<bool> ValidComponents;
	ValidComponents.Init(false, NumComponents);

	if (GroomAsset)
	{
		for (int32 i = 0; i < NumGroups; ++i)
		{
			ValidComponents[i] = GroomAsset->HairGroupsPhysics[i].SolverSettings.EnableSimulation;
		}
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
				if (GetWorld())
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
			NiagaraComponent->Activate(true);
			if (NiagaraComponent->GetSystemInstance())
			{
				NiagaraComponent->GetSystemInstance()->Reset(FNiagaraSystemInstance::EResetMode::ReInit);
				NiagaraComponent->GetSystemInstance()->Reset(FNiagaraSystemInstance::EResetMode::ResetAll);
			}
		}
		else if (NiagaraComponent && !NiagaraComponent->IsBeingDestroyed())
		{
			if (GetWorld())
			{
				NiagaraComponent->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
				NiagaraComponent->UnregisterComponent();
			}
			NiagaraComponent->DestroyComponent();
			NiagaraComponent = nullptr;
		}
	}
	NiagaraComponents.SetNum(NumGroups);
	UpdateSimulatedGroups();
}

void UGroomComponent::SetGroomAsset(UGroomAsset* Asset)
{
	ReleaseResources();
	if (Asset && Asset->IsValid())
	{
		GroomAsset = Asset;
	}
	else
	{
		GroomAsset = nullptr;
	}

	if (!UGroomBindingAsset::IsBindingAssetValid(BindingAsset) || !UGroomBindingAsset::IsCompatible(GroomAsset, BindingAsset))
	{
		BindingAsset = nullptr;
	}

	UpdateHairGroupsDesc();
	UpdateHairSimulation();
	if (!GroomAsset)
		return;
	InitResources();
}

void UGroomComponent::SetGroomAsset(UGroomAsset* Asset, UGroomBindingAsset* InBinding)
{
	ReleaseResources();
	if (Asset && Asset->IsValid())
	{
		GroomAsset = Asset;
	}
	else
	{
		GroomAsset = nullptr;
	}
	BindingAsset = InBinding;
	if (!UGroomBindingAsset::IsBindingAssetValid(BindingAsset) || !UGroomBindingAsset::IsCompatible(GroomAsset, BindingAsset))
	{
		BindingAsset = nullptr;
	}

	if (BindingAsset)
	{
		bBindGroomToSkeletalMesh = true;
	}

	UpdateHairGroupsDesc();
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
		HairDesc.HairClipLength = Scale;
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
		const bool bIsValid = InBinding != nullptr ? UGroomBindingAsset::IsBindingAssetValid(BindingAsset) : true;
		if (bIsValid && UGroomBindingAsset::IsCompatible(GroomAsset, InBinding))
		{
			BindingAsset = InBinding;
			bBindGroomToSkeletalMesh = InBinding != nullptr;
			InitResources();
		}
	}
}

void UGroomComponent::UpdateHairGroupsDescAndInvalidateRenderState()
{
	UpdateHairGroupsDesc();

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

FPrimitiveSceneProxy* UGroomComponent::CreateSceneProxy()
{
	if (!GroomAsset || GroomAsset->GetNumHairGroups() == 0 || GroomAsset->HairGroupsData[0].HairRenderData.GetNumCurves() == 0 || !InterpolationOutput || !InterpolationInput)
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
				LocalBounds += GroupData.HairRenderData.BoundingBox;
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
		if (UMaterialInterface* Material = GroomAsset->HairGroupsInfo[ElementIndex].Material)
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
		OverrideMaterial = HairDefaultMaterial;
	}

	return OverrideMaterial;
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
	if (!HairGroupResources || GroupIndex >= uint32(HairGroupResources->HairGroups.Num()))
	{
		return nullptr;
	}

	return HairGroupResources->HairGroups[GroupIndex].SimDeformedResources;
}

FHairStrandsRestRootResource* UGroomComponent::GetGuideStrandsRestRootResource(uint32 GroupIndex)
{
	if (!HairGroupResources || GroupIndex >= uint32(HairGroupResources->HairGroups.Num()))
	{
		return nullptr;
	}

	return HairGroupResources->HairGroups[GroupIndex].SimRestRootResources;
}

FHairStrandsDeformedRootResource* UGroomComponent::GetGuideStrandsDeformedRootResource(uint32 GroupIndex)
{
	if (!HairGroupResources || GroupIndex >= uint32(HairGroupResources->HairGroups.Num()))
	{
		return nullptr;
	}

	return HairGroupResources->HairGroups[GroupIndex].SimDeformedRootResources;
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

EWorldType::Type UGroomComponent::GetWorldType() const
{
	EWorldType::Type WorldType = GetWorld() ? EWorldType::Type(GetWorld()->WorldType) : EWorldType::None;
	return WorldType == EWorldType::Inactive ? EWorldType::Editor : WorldType;
}

void UGroomComponent::UpdateSimulatedGroups()
{
	if (InterpolationInput)
	{
		const uint32 Id = ComponentId.PrimIDValue;
		const EWorldType::Type WorldType = GetWorldType();

		FHairStrandsInterpolationInput* LocalInterpolationInput = InterpolationInput;
		UGroomAsset* LocalGroomAsset = GroomAsset;
		ENQUEUE_RENDER_COMMAND(FHairStrandsTick_UEnableSimulatedGroups)(
			[LocalInterpolationInput, LocalGroomAsset, Id, WorldType](FRHICommandListImmediate& RHICmdList)
		{
			int32 GroupIt = 0;
			for (FHairStrandsInterpolationInput::FHairGroup& HairGroup : LocalInterpolationInput->HairGroups)
			{
				const bool bIsSimulationEnable = (LocalGroomAsset && GroupIt < LocalGroomAsset->HairGroupsPhysics.Num()) ? 
					LocalGroomAsset->HairGroupsPhysics[GroupIt].SolverSettings.EnableSimulation : false;
				HairGroup.bIsSimulationEnable = bIsSimulationEnable;
				UpdateHairStrandsDebugInfo(Id, WorldType, GroupIt, bIsSimulationEnable);
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

	UpdateHairGroupsDesc();

	if (!GroomAsset || GroomAsset->GetNumHairGroups() == 0)
		return;

	InitializedResources = GroomAsset;

	const FPrimitiveComponentId LocalComponentId = ComponentId;
	const EWorldType::Type WorldType = GetWorldType();

	// Insure the ticking of the Groom component always happens after the skeletalMeshComponent.
	USkeletalMeshComponent* SkeletalMeshComponent = bBindGroomToSkeletalMesh && GetAttachParent() ? Cast<USkeletalMeshComponent>(GetAttachParent()) : nullptr;
	const bool bHasValidSketalMesh = SkeletalMeshComponent && SkeletalMeshComponent->GetSkeletalMeshRenderData();

	uint32 SkeletalComponentId = ~0;
	if (bHasValidSketalMesh)
	{
		RegisteredSkeletalMeshComponent = SkeletalMeshComponent;
		SkeletalComponentId = SkeletalMeshComponent->ComponentId.PrimIDValue;
		AddTickPrerequisiteComponent(SkeletalMeshComponent);
	}

	const bool bIsBindingCompatible = 
		UGroomBindingAsset::IsCompatible(SkeletalMeshComponent ? SkeletalMeshComponent->SkeletalMesh : nullptr, BindingAsset) &&
		UGroomBindingAsset::IsCompatible(GroomAsset, BindingAsset) && 
		UGroomBindingAsset::IsBindingAssetValid(BindingAsset, bIsBindingReloading);

	FTransform HairLocalToWorld = GetComponentTransform();
	FTransform SkinLocalToWorld = bBindGroomToSkeletalMesh && SkeletalMeshComponent ? SkeletalMeshComponent->GetComponentTransform() : FTransform::Identity;
	
	check(HairGroupResources == nullptr);
	HairGroupResources = new FHairGroupResources();
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

		FHairGroupResource& Res = HairGroupResources->HairGroups.AddDefaulted_GetRef();
		Res.InterpolationResource = GroupData.HairInterpolationResource;

		#if RHI_RAYTRACING
		if (IsHairRayTracingEnabled())
		{
			Res.RaytracingResources = new FHairStrandsRaytracingResource(GroupData.HairRenderData);
			BeginInitResource(Res.RaytracingResources);
		}
		#endif

		if (bHasValidSketalMesh)
		{
			if (BindingAsset && bIsBindingCompatible)
			{
				check(GroupIt < BindingAsset->HairGroupResources.Num());
				check(SkeletalMeshComponent->GetNumLODs() == BindingAsset->HairGroupResources[GroupIt].RenRootResources->RootData.MeshProjectionLODs.Num());
				check(SkeletalMeshComponent->GetNumLODs() == BindingAsset->HairGroupResources[GroupIt].SimRootResources->RootData.MeshProjectionLODs.Num());

				Res.bOwnRootResourceAllocation = false;
				Res.RenRestRootResources = BindingAsset->HairGroupResources[GroupIt].RenRootResources;
				Res.SimRestRootResources = BindingAsset->HairGroupResources[GroupIt].SimRootResources;

				DebugHairGroup.bHasBinding = true;
			}
			else
			{
				const uint32 LODCount = SkeletalMeshComponent->GetNumLODs();
				if (LODCount > 0)
				{
					Res.bOwnRootResourceAllocation = true;
					TArray<uint32> NumSamples; NumSamples.Init(0, LODCount);
					Res.RenRestRootResources = new FHairStrandsRestRootResource(&GroupData.HairRenderData, LODCount, NumSamples);
					Res.SimRestRootResources = new FHairStrandsRestRootResource(&GroupData.HairSimulationData, LODCount, NumSamples);
					BeginInitResource(Res.RenRestRootResources);
					BeginInitResource(Res.SimRestRootResources);
				}
			}

			Res.RenDeformedRootResources = new FHairStrandsDeformedRootResource(Res.RenRestRootResources);
			Res.SimDeformedRootResources = new FHairStrandsDeformedRootResource(Res.SimRestRootResources);

			BeginInitResource(Res.RenDeformedRootResources);
			BeginInitResource(Res.SimDeformedRootResources);
		}
		
		Res.RenderRestResources = GroupData.HairStrandsRestResource;
		Res.SimRestResources = GroupData.HairSimulationRestResource;

		Res.RenderDeformedResources = new FHairStrandsDeformedResource(GroupData.HairRenderData.RenderData, false);
		Res.SimDeformedResources = new FHairStrandsDeformedResource(GroupData.HairSimulationData.RenderData, true);
		Res.ClusterCullingResources = GroupData.ClusterCullingResource;

		const uint32 HairVertexCount = GroupData.HairRenderData.GetNumPoints();
		const uint32 GroupInstanceVertexCount = HairVertexCount * 6; // 6 vertex per point for a quad
		Res.HairGroupPublicDatas = new FHairGroupPublicData(GroupIt, GroupInstanceVertexCount, Res.ClusterCullingResources->ClusterCount, HairVertexCount);

		BeginInitResource(Res.RenderDeformedResources);
		BeginInitResource(Res.SimDeformedResources);
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

		const bool bIsSimulationEnable = (GroomAsset && GroupIt < GroomAsset->HairGroupsPhysics.Num()) ?
			GroomAsset->HairGroupsPhysics[GroupIt].SolverSettings.EnableSimulation : false;

		// For skinned groom, these value will be updated during TickComponent() call
		// Deformed sim & render are expressed within the referential (unlike rest pose)
		InterpolationInputGroup.OutHairPositionOffset = RenderRestHairPositionOffset;
		InterpolationInputGroup.OutHairPreviousPositionOffset = RenderRestHairPositionOffset;
		InterpolationInputGroup.bIsSimulationEnable = bIsSimulationEnable;
		DebugHairGroup.bHasSimulation = InterpolationInputGroup.bIsSimulationEnable;

		GroupIt++;
	}

	FHairStrandsInterpolationData Interpolation;
	Interpolation.Input  = InterpolationInput;
	Interpolation.Output = InterpolationOutput;
	Interpolation.Function = ComputeHairStrandsInterpolation;
	Interpolation.ResetFunction = ResetHairStrandsInterpolation;

	// Does not run projection code when running with null RHI as this is not needed, and will crash as the skeletal GPU resources are not created
	if (GUsingNullRHI)
	{
		return;
	}

	UGroomAsset* LocalGroomAsset = GroomAsset;
	USkeletalMesh* InSourceSkeletalMesh = SourceSkeletalMesh;
	const bool bRunMeshProjection = bHasValidSketalMesh && (!BindingAsset || !bIsBindingCompatible);
	FHairGroupResources* LocalResources = HairGroupResources;
	const uint32 Id = LocalComponentId.PrimIDValue;
	ENQUEUE_RENDER_COMMAND(FHairStrandsBuffers)(
		[
			Id,
			SkeletalComponentId,
			Interpolation,
			LocalResources,
			HairLocalToWorld, SkinLocalToWorld,
			WorldType,
			DebugGroupInfo,
			bRunMeshProjection,
			SkeletalMeshComponent,
			InSourceSkeletalMesh,
			LocalGroomAsset
		]
		(FRHICommandListImmediate& RHICmdList)
	{
		FHairStrandsProjectionHairData RenProjectionDatas;
		FHairStrandsProjectionHairData SimProjectionDatas;
		FHairStrandsPrimitiveResources PrimitiveResources;
		const uint32 GroupCount = LocalResources->HairGroups.Num();
		for (uint32 GroupIt=0;GroupIt<GroupCount; ++GroupIt)
		{
			FHairGroupResource& Res = LocalResources->HairGroups[GroupIt];

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
			OutputGroup.CurrentIndex					= &Res.SimDeformedResources->CurrentIndex;
			
			OutputGroup.RenderClusterAABBBuffer			= &Res.HairGroupPublicDatas->GetClusterAABBBuffer();
			OutputGroup.RenderGroupAABBBuffer			= &Res.HairGroupPublicDatas->GetGroupAABBBuffer();
			OutputGroup.ClusterInfoBuffer				= &Res.ClusterCullingResources->ClusterInfoBuffer;

			RenProjectionDatas.HairGroups.Add(ToProjectionHairData(Res.RenRestRootResources, Res.RenDeformedRootResources));
			SimProjectionDatas.HairGroups.Add(ToProjectionHairData(Res.SimRestRootResources, Res.SimDeformedRootResources));

			FHairStrandsPrimitiveResources::FHairGroup& Group = PrimitiveResources.Groups.AddDefaulted_GetRef();
			Group.ClusterAABBBuffer = &Res.HairGroupPublicDatas->GetClusterAABBBuffer();
			Group.GroupAABBBuffer	= &Res.HairGroupPublicDatas->GetGroupAABBBuffer();
			Group.ClusterCount		= Res.ClusterCullingResources->ClusterCount;
		}

		FHairStrandsProjectionDebugInfo HairProjectionDebugInfo;
		#if WITH_EDITOR
		HairProjectionDebugInfo.GroomAssetName = LocalGroomAsset->GetName();
		if (SkeletalMeshComponent)
		{
			HairProjectionDebugInfo.SkeletalComponentName = SkeletalMeshComponent->GetPathName();
		}
		#endif

		if (bRunMeshProjection)
		{				
			FSkeletalMeshRenderData* TargetRenderData = SkeletalMeshComponent ? SkeletalMeshComponent->GetSkeletalMeshRenderData() : nullptr;
			FHairStrandsProjectionMeshData TargetMeshData = ExtractMeshData(TargetRenderData);

			// Create mapping between the source & target using their UV
			// The lifetime of 'TransferredPositions' needs to encompass RunProjection
			TArray<FRWBuffer> TransferredPositions;
			
			if (FSkeletalMeshRenderData* SourceRenderData = InSourceSkeletalMesh ? InSourceSkeletalMesh->GetResourceForRendering() : nullptr)
			{
				FHairStrandsProjectionMeshData SourceMeshData = ExtractMeshData(SourceRenderData);
				RunMeshTransfer(
					RHICmdList,
					SourceMeshData,
					TargetMeshData,
					TransferredPositions);

				const uint32 LODCount = TargetMeshData.LODs.Num();
				for (uint32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
				{
					for (FHairStrandsProjectionMeshData::Section& Section : TargetMeshData.LODs[LODIndex].Sections)
					{
						Section.PositionBuffer = TransferredPositions[LODIndex].SRV;
					}
				}
			#if WITH_EDITOR
				HairProjectionDebugInfo.SourceMeshData = SourceMeshData;
				HairProjectionDebugInfo.TargetMeshData = TargetMeshData;
				HairProjectionDebugInfo.TransferredPositions = TransferredPositions;
			#endif
			}

			// The offset is based on the center of the skeletal mesh (which is computed based on the physics capsules/boxes/...)
			RunProjection(
				RHICmdList,
				HairLocalToWorld,
				TargetMeshData,
				RenProjectionDatas,
				SimProjectionDatas);

		}

		RegisterHairStrands(
			Id,
			SkeletalComponentId,
			WorldType,
			Interpolation,
			RenProjectionDatas,
			SimProjectionDatas,
			PrimitiveResources,
			DebugGroupInfo,
			HairProjectionDebugInfo);
	});
}

void UGroomComponent::DeleteHairGroupResources(FHairGroupResources*& InHairGroupResources)
{
	if (!InHairGroupResources)
	{
		return;
	}

	FHairGroupResources* Local = InHairGroupResources;
	ENQUEUE_RENDER_COMMAND(StaticMeshVertexBuffersLegacyInit)(
		[Local](FRHICommandListImmediate& RHICmdList)
	{
		for (FHairGroupResource& Res : Local->HairGroups)
		{
			// Release the root resources only if they have been created internally (vs. being created by external asset)
			if (Res.bOwnRootResourceAllocation)
			{
				SafeRelease(Res.RenRestRootResources);
				SafeRelease(Res.SimRestRootResources);
			}
			SafeRelease(Res.RenDeformedRootResources);
			SafeRelease(Res.SimDeformedRootResources);

			SafeRelease(Res.RenderDeformedResources);
			SafeRelease(Res.HairGroupPublicDatas);
			SafeRelease(Res.SimDeformedResources);
			#if RHI_RAYTRACING
			SafeRelease(Res.RaytracingResources);
			#endif
		}
		delete Local;
	});

	InHairGroupResources = nullptr;
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

	DeleteHairGroupResources(HairGroupResources);

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
		bIsGroomAssetCallbackRegistered = true;
	}

	if (BindingAsset && !bIsGroomBindingAssetCallbackRegistered)
	{
		BindingAsset->GetOnGroomBindingAssetChanged().AddUObject(this, &UGroomComponent::InvalidateAndRecreate);
		bIsGroomBindingAssetCallbackRegistered = true;
	}
	ValidateMaterials(false);
#endif
}

#if WITH_EDITOR
void UGroomComponent::Invalidate()
{
	UpdateHairSimulation();
	MarkRenderStateDirty();
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
	

	UpdateHairSimulation();
	Super::OnRegister();

	if (!InitializedResources)
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
	const uint32 Id = ComponentId.PrimIDValue;

	ENQUEUE_RENDER_COMMAND(FHairStrandsRegister)(
		[Id, WorldType](FRHICommandListImmediate& RHICmdList)
	{
		UpdateHairStrands(Id, WorldType);
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

	FVector MeshPositionOffset = FVector::ZeroVector;
	USkeletalMeshComponent* SkeletalMeshComponent = RegisteredSkeletalMeshComponent;

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

	if (!HairGroupResources)
	{
		return;
	}

	if (SkeletalMeshComponent)
	{
		// The offset is based on the center of the skeletal mesh (which is computed based on the physics capsules/boxes/...)
		// For skinned mesh update the relative center of hair positions after deformation
		MeshPositionOffset = SkeletalMeshComponent->CalcBounds(FTransform::Identity).GetBox().GetCenter();
		{
			FHairGroupResources* LocalResources = HairGroupResources;

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
				for (FHairGroupResource& Res : LocalResources->HairGroups)
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

	FHairGroupResources* LocalResources = HairGroupResources;
	const FTransform SkinLocalToWorld = SkeletalMeshComponent ? SkeletalMeshComponent->GetComponentTransform() : FTransform();
	const FTransform HairLocalToWorld = GetComponentTransform();
	ENQUEUE_RENDER_COMMAND(FHairStrandsTick_TransformUpdate)(
		[Id, WorldType, HairLocalToWorld, SkinLocalToWorld, FeatureLevel, LocalResources](FRHICommandListImmediate& RHICmdList)
	{		
		if (ERHIFeatureLevel::Num == FeatureLevel)
			return;

		UpdateHairStrands(Id, WorldType, HairLocalToWorld, SkinLocalToWorld);
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
		OutMaterials.Add(HairDebugMaterial);
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

	if (bRegisterDefaultMaterial)
	{
		OutMaterials.Add(HairDefaultMaterial);		
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
	const bool bIsBindingCompatible = UGroomBindingAsset::IsCompatible(GroomAsset, BindingAsset);
	if (!bIsBindingCompatible || !UGroomBindingAsset::IsBindingAssetValid(BindingAsset))
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
		PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, LodBias) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(FHairGroupDesc, LodAverageVertexPerPixel) ||
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
		if (!OverrideMaterial && MaterialIt < uint32(GroomAsset->HairGroupsInfo.Num()) && GroomAsset->HairGroupsInfo[MaterialIt].Material)
		{
			OverrideMaterial = GroomAsset->HairGroupsInfo[MaterialIt].Material;
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
