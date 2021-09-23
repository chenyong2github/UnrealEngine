// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraRendererMeshes.h"
#include "ParticleResources.h"
#include "NiagaraDataSet.h"
#include "NiagaraStats.h"
#include "Async/ParallelFor.h"
#include "Engine/StaticMesh.h"
#include "Math/ScaleMatrix.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraSortingGPU.h"
#include "NiagaraGPURayTracingTransformsShader.h"
#include "RayTracingDefinitions.h"
#include "RayTracingDynamicGeometryCollection.h"
#include "RayTracingInstance.h"
#include "Renderer/Private/ScenePrivate.h"
#include "IXRTrackingSystem.h"

#ifdef HMD_MODULE_INCLUDED
	#include "IXRTrackingSystem.h"
#endif

DECLARE_CYCLE_STAT(TEXT("Generate Mesh Vertex Data [GT]"), STAT_NiagaraGenMeshVertexData, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Render Meshes [RT]"), STAT_NiagaraRenderMeshes, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Render Meshes - Allocate GPU Data [RT]"), STAT_NiagaraRenderMeshes_AllocateGPUData, STATGROUP_Niagara);


DECLARE_DWORD_COUNTER_STAT(TEXT("NumMeshesRenderer"), STAT_NiagaraNumMeshes, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumMesheVerts"), STAT_NiagaraNumMeshVerts, STATGROUP_Niagara);

static int32 GbEnableNiagaraMeshRendering = 1;
static FAutoConsoleVariableRef CVarEnableNiagaraMeshRendering(
	TEXT("fx.EnableNiagaraMeshRendering"),
	GbEnableNiagaraMeshRendering,
	TEXT("If == 0, Niagara Mesh Renderers are disabled. \n"),
	ECVF_Default
);

#if RHI_RAYTRACING
static TAutoConsoleVariable<int32> CVarRayTracingNiagaraMeshes(
	TEXT("r.RayTracing.Geometry.NiagaraMeshes"),
	1,
	TEXT("Include Niagara meshes in ray tracing effects (default = 1 (Niagara meshes enabled in ray tracing))"));
#endif

struct FNiagaraDynamicDataMesh : public FNiagaraDynamicDataBase
{
	FNiagaraDynamicDataMesh(const FNiagaraEmitterInstance* InEmitter)
		: FNiagaraDynamicDataBase(InEmitter)
	{
	}

	TArray<FMaterialRenderProxy*, TInlineAllocator<8>> Materials;
	TArray<UNiagaraDataInterface*> DataInterfacesBound;
	TArray<UObject*> ObjectsBound;
	TArray<uint8> ParameterDataBound;
};

//////////////////////////////////////////////////////////////////////////

FNiagaraRendererMeshes::FNiagaraRendererMeshes(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties* Props, const FNiagaraEmitterInstance* Emitter)
	: FNiagaraRenderer(FeatureLevel, Props, Emitter)
	, MaterialParamValidMask(0)
{
	check(Emitter);
	check(Props);

	const UNiagaraMeshRendererProperties* Properties = CastChecked<const UNiagaraMeshRendererProperties>(Props);
	SourceMode = Properties->SourceMode;
	FacingMode = Properties->FacingMode;
	bLockedAxisEnable = Properties->bLockedAxisEnable;
	LockedAxis = Properties->LockedAxis;
	LockedAxisSpace = Properties->LockedAxisSpace;
	SortMode = Properties->SortMode;
	bSortOnlyWhenTranslucent = Properties->bSortOnlyWhenTranslucent;
	bGpuLowLatencyTranslucency = false;//-TEMP: Need to add IndirectArgs update point.  Properties->bGpuLowLatencyTranslucency && (SortMode == ENiagaraSortMode::None);
	bOverrideMaterials = Properties->bOverrideMaterials;
	SubImageSize = Properties->SubImageSize;
	bSubImageBlend = Properties->bSubImageBlend;
	bEnableFrustumCulling = Properties->bEnableFrustumCulling;
	bEnableCulling = bEnableFrustumCulling;
	DistanceCullRange = FVector2D(0, FLT_MAX);
	RendererVisibility = Properties->RendererVisibility;
	bAccurateMotionVectors = Properties->NeedsPreciseMotionVectors();
	MaxSectionCount = 0;

	if (Properties->bEnableCameraDistanceCulling)
	{
		DistanceCullRange = FVector2D(Properties->MinCameraDistance, Properties->MaxCameraDistance);
		bEnableCulling = true;
	}

	// Ensure valid value for the locked axis
	if (!LockedAxis.Normalize())
	{
		LockedAxis.Set(0.0f, 0.0f, 1.0f);
	}

	const FNiagaraDataSet& Data = Emitter->GetData();

	RendererVisTagOffset = INDEX_NONE;
	int32 FloatOffset;
	int32 HalfOffset;
	if (Data.GetVariableComponentOffsets(Properties->RendererVisibilityTagBinding.GetDataSetBindableVariable(), FloatOffset, RendererVisTagOffset, HalfOffset))
	{
		// If the renderer visibility tag is bound, we have to do it in the culling pass
		bEnableCulling = true;
	}

	MeshIndexOffset = INDEX_NONE;
	if (Data.GetVariableComponentOffsets(Properties->MeshIndexBinding.GetDataSetBindableVariable(), FloatOffset, MeshIndexOffset, HalfOffset))
	{
		// If the mesh index is bound, we have to do it in the culling pass
		bEnableCulling = true;
	}

	MaterialParamValidMask = Properties->MaterialParamValidMask;

	RendererLayoutWithCustomSorting = &Properties->RendererLayoutWithCustomSorting;
	RendererLayoutWithoutCustomSorting = &Properties->RendererLayoutWithoutCustomSorting;

	bSetAnyBoundVars = false;
	if (Emitter->GetRendererBoundVariables().IsEmpty() == false)
	{
		const TArray< const FNiagaraVariableAttributeBinding*>& VFBindings = Properties->GetAttributeBindings();
		const int32 NumBindings = bAccurateMotionVectors ? ENiagaraMeshVFLayout::Type::Num_Max : ENiagaraMeshVFLayout::Type::Num_Default;
		check(VFBindings.Num() >= ENiagaraMeshVFLayout::Type::Num_Max);
		for (int32 i = 0; i < ENiagaraMeshVFLayout::Type::Num_Max; i++)
		{
			VFBoundOffsetsInParamStore[i] = INDEX_NONE;
			if (i < NumBindings && VFBindings[i] && VFBindings[i]->CanBindToHostParameterMap())
			{
				VFBoundOffsetsInParamStore[i] = Emitter->GetRendererBoundVariables().IndexOf(VFBindings[i]->GetParamMapBindableVariable());
				if (VFBoundOffsetsInParamStore[i] != INDEX_NONE)
					bSetAnyBoundVars = true;
			}
		}
	}
	else
	{
		for (int32 i = 0; i < ENiagaraMeshVFLayout::Type::Num_Max; i++)
		{
			VFBoundOffsetsInParamStore[i] = INDEX_NONE;
		}
	}
}

FNiagaraRendererMeshes::~FNiagaraRendererMeshes()
{
}

void FNiagaraRendererMeshes::Initialize(const UNiagaraRendererProperties* InProps, const FNiagaraEmitterInstance* Emitter, const UNiagaraComponent* InComponent)
{
	FNiagaraRenderer::Initialize(InProps, Emitter, InComponent);

	check(Emitter);
	check(InProps);

	const UNiagaraMeshRendererProperties* Properties = CastChecked<const UNiagaraMeshRendererProperties>(InProps);

	MaxSectionCount = 0;

	// Initialize the valid mesh slots, and prep them with the data for every mesh, LOD, and section we'll be needing over the lifetime of the renderer
	const uint32 MaxMeshes = Properties->Meshes.Num();
	Meshes.Reserve(MaxMeshes);
	for (uint32 SourceMeshIndex = 0; SourceMeshIndex < MaxMeshes; ++SourceMeshIndex)
	{
		const auto& MeshProperties = Properties->Meshes[SourceMeshIndex];
		if (MeshProperties.Mesh && MeshProperties.Mesh->GetRenderData())
		{
			FMeshData& MeshData = Meshes.AddDefaulted_GetRef();
			MeshData.RenderData = MeshProperties.Mesh->GetRenderData();
			MeshData.SourceMeshIndex = SourceMeshIndex;
			MeshData.PivotOffset = MeshProperties.PivotOffset;
			MeshData.PivotOffsetSpace = MeshProperties.PivotOffsetSpace;
			MeshData.Scale = MeshProperties.Scale;
			MeshData.MinimumLOD = MeshProperties.Mesh->GetMinLOD().GetValue();

			// Create an index remap from mesh material index to it's index in the master material list
			TArray<UMaterialInterface*> MeshMaterials;
			Properties->GetUsedMeshMaterials(SourceMeshIndex, Emitter, MeshMaterials);
			for (auto MeshMaterial : MeshMaterials)
			{
				MeshData.MaterialRemapTable.Add(BaseMaterials_GT.IndexOfByPredicate([&](UMaterialInterface* LookMat)
				{
					if (LookMat == MeshMaterial)
					{
						return true;
					}
					if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(LookMat))
					{
						return MeshMaterial == MID->Parent;
					}
					return false;
				}));
			}

			// Extend the local bounds by this mesh's bounds
			FBox LocalBounds = MeshProperties.Mesh->GetExtendedBounds().GetBox();
			LocalBounds.Min *= MeshProperties.Scale;
			LocalBounds.Max *= MeshProperties.Scale;
			MeshData.LocalCullingSphere.Center = LocalBounds.GetCenter();
			MeshData.LocalCullingSphere.W = LocalBounds.GetExtent().Size();

			// Determine the max section count for all LODs of this mesh and accumulate it on the max for all meshes
			uint32 MaxSectionCountThisMesh = 0;
			for (const auto& LODModel : MeshData.RenderData->LODResources)
			{
				MaxSectionCountThisMesh = FMath::Max<uint32>(MaxSectionCountThisMesh, LODModel.Sections.Num());
			}
			MaxSectionCount += MaxSectionCountThisMesh;
		}
	}

	checkf(Meshes.Num() > 0, TEXT("At least one valid mesh is required to instantiate a mesh renderer"));
}

void FNiagaraRendererMeshes::ReleaseRenderThreadResources()
{
}

void FNiagaraRendererMeshes::SetupVertexFactory(FNiagaraMeshVertexFactory& InVertexFactory, const FStaticMeshLODResources& LODResources) const
{
	FStaticMeshDataType Data;

	LODResources.VertexBuffers.PositionVertexBuffer.BindPositionVertexBuffer(&InVertexFactory, Data);
	LODResources.VertexBuffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(&InVertexFactory, Data);
	LODResources.VertexBuffers.StaticMeshVertexBuffer.BindTexCoordVertexBuffer(&InVertexFactory, Data, MAX_TEXCOORDS);
	LODResources.VertexBuffers.ColorVertexBuffer.BindColorVertexBuffer(&InVertexFactory, Data);
	InVertexFactory.SetData(Data);
}

int32 FNiagaraRendererMeshes::GetLODIndex(int32 MeshIndex) const
{
	check(IsInRenderingThread());
	check(Meshes.IsValidIndex(MeshIndex));

	const FMeshData& MeshData = Meshes[MeshIndex];
	const int32 LODIndex = MeshData.RenderData->GetCurrentFirstLODIdx(MeshData.MinimumLOD);

	return MeshData.RenderData->LODResources.IsValidIndex(LODIndex) ? LODIndex : INDEX_NONE;
}

void FNiagaraRendererMeshes::PrepareParticleMeshRenderData(FParticleMeshRenderData& ParticleMeshRenderData, FNiagaraDynamicDataBase* InDynamicData, const FNiagaraSceneProxy* SceneProxy) const
{

	// Anything to render?
	ParticleMeshRenderData.DynamicDataMesh = static_cast<FNiagaraDynamicDataMesh*>(InDynamicData);
	if (!ParticleMeshRenderData.DynamicDataMesh || !SceneProxy->GetBatcher())
	{
		return;
	}

	// If all materials are translucent we can pickup the low latency data
	bool bAllTranslucentMaterials = true;
	ParticleMeshRenderData.bHasTranslucentMaterials = false;
	for (FMaterialRenderProxy* MaterialProxy : ParticleMeshRenderData.DynamicDataMesh->Materials)
	{
		check(MaterialProxy);
		const EBlendMode BlendMode = MaterialProxy->GetIncompleteMaterialWithFallback(FeatureLevel).GetBlendMode();
		const bool bTranslucent = IsTranslucentBlendMode(BlendMode);
		ParticleMeshRenderData.bHasTranslucentMaterials |= bTranslucent;
		bAllTranslucentMaterials &= bTranslucent;
	}
	bAllTranslucentMaterials &= ParticleMeshRenderData.bHasTranslucentMaterials;

	// Anything to render?
	ParticleMeshRenderData.SourceParticleData = ParticleMeshRenderData.DynamicDataMesh->GetParticleDataToRender(bAllTranslucentMaterials && bGpuLowLatencyTranslucency);
	if ((ParticleMeshRenderData.SourceParticleData == nullptr) ||
		(SourceMode == ENiagaraRendererSourceDataMode::Particles && ParticleMeshRenderData.SourceParticleData->GetNumInstances() == 0) ||
		(Meshes.Num() == 0) ||
		!GSupportsResourceView
		)
	{
		ParticleMeshRenderData.SourceParticleData = nullptr;
		return;
	}

	// Particle source mode
	if (SourceMode == ENiagaraRendererSourceDataMode::Particles)
	{
		const EShaderPlatform ShaderPlatform = SceneProxy->GetBatcher()->GetShaderPlatform();

		// Determine if we need sorting
		ParticleMeshRenderData.bNeedsSort = SortMode != ENiagaraSortMode::None && (ParticleMeshRenderData.bHasTranslucentMaterials || !bSortOnlyWhenTranslucent);
		const bool bNeedCustomSort = ParticleMeshRenderData.bNeedsSort && (SortMode == ENiagaraSortMode::CustomAscending || SortMode == ENiagaraSortMode::CustomDecending);
		ParticleMeshRenderData.RendererLayout = bNeedCustomSort ? RendererLayoutWithCustomSorting : RendererLayoutWithoutCustomSorting;
		ParticleMeshRenderData.SortVariable = bNeedCustomSort ? ENiagaraMeshVFLayout::CustomSorting : ENiagaraMeshVFLayout::Position;
		if (ParticleMeshRenderData.bNeedsSort)
		{
			TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = ParticleMeshRenderData.RendererLayout->GetVFVariables_RenderThread();
			const FNiagaraRendererVariableInfo& SortVariable = VFVariables[ParticleMeshRenderData.SortVariable];
			ParticleMeshRenderData.bNeedsSort = SortVariable.GetGPUOffset() != INDEX_NONE;
		}

		// Do we need culling?
		ParticleMeshRenderData.bNeedsCull = bEnableCulling;
		ParticleMeshRenderData.bSortCullOnGpu = (ParticleMeshRenderData.bNeedsSort && FNiagaraUtilities::AllowGPUSorting(ShaderPlatform)) || (ParticleMeshRenderData.bNeedsCull && FNiagaraUtilities::AllowGPUCulling(ShaderPlatform));

		// Validate what we setup
		if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			if (!ensureMsgf(!ParticleMeshRenderData.bNeedsCull || ParticleMeshRenderData.bSortCullOnGpu, TEXT("Culling is requested on GPU but we don't support sorting, this will result in incorrect rendering.")))
			{
				ParticleMeshRenderData.bNeedsCull = false;
			}
			ParticleMeshRenderData.bNeedsSort &= ParticleMeshRenderData.bSortCullOnGpu;
		}
		else
		{
			// For CPU sims, decide if we should sort / cull on the GPU or not
			if ( ParticleMeshRenderData.bSortCullOnGpu )
			{
				const int32 NumInstances = ParticleMeshRenderData.SourceParticleData->GetNumInstances();

				const int32 SortThreshold = GNiagaraGPUSortingCPUToGPUThreshold;
				const bool bSortMoveToGpu = (SortThreshold >= 0) && (NumInstances >= SortThreshold);

				const int32 CullThreshold = GNiagaraGPUCullingCPUToGPUThreshold;
				const bool bCullMoveToGpu = (CullThreshold >= 0) && (NumInstances >= CullThreshold);

				ParticleMeshRenderData.bSortCullOnGpu = bSortMoveToGpu || bCullMoveToGpu;
			}
		}

		// Update layout as it could have changed
		ParticleMeshRenderData.RendererLayout = bNeedCustomSort ? RendererLayoutWithCustomSorting : RendererLayoutWithoutCustomSorting;
	}
}

void FNiagaraRendererMeshes::PrepareParticleRenderBuffers(FParticleMeshRenderData& ParticleMeshRenderData, FGlobalDynamicReadBuffer& DynamicReadBuffer) const
{
	if ( SourceMode == ENiagaraRendererSourceDataMode::Particles )
	{
		if ( SimTarget == ENiagaraSimTarget::CPUSim )
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderMeshes_AllocateGPUData);


			// For CPU simulations we do not gather int parameters inside TransferDataToGPU currently so we need to copy off
			// integrate attributes if we are culling on the GPU.
			TArray<uint32, TInlineAllocator<2>> IntParamsToCopy;
			if (ParticleMeshRenderData.bNeedsCull)
			{
				if (ParticleMeshRenderData.bSortCullOnGpu)
				{
					if (RendererVisTagOffset != INDEX_NONE)
					{
						ParticleMeshRenderData.RendererVisTagOffset = IntParamsToCopy.Add(RendererVisTagOffset);
					}
					if (MeshIndexOffset != INDEX_NONE)
					{
						ParticleMeshRenderData.MeshIndexOffset = IntParamsToCopy.Add(MeshIndexOffset);
					}
				}
				else
				{
					ParticleMeshRenderData.RendererVisTagOffset = RendererVisTagOffset;
					ParticleMeshRenderData.MeshIndexOffset = MeshIndexOffset;
				}
			}

			FParticleRenderData ParticleRenderData = TransferDataToGPU(DynamicReadBuffer, ParticleMeshRenderData.RendererLayout, IntParamsToCopy, ParticleMeshRenderData.SourceParticleData);
			const uint32 NumInstances = ParticleMeshRenderData.SourceParticleData->GetNumInstances();

			ParticleMeshRenderData.ParticleFloatSRV = GetSrvOrDefaultFloat(ParticleRenderData.FloatData);
			ParticleMeshRenderData.ParticleHalfSRV = GetSrvOrDefaultHalf(ParticleRenderData.HalfData);
			ParticleMeshRenderData.ParticleIntSRV = GetSrvOrDefaultInt(ParticleRenderData.IntData);
			ParticleMeshRenderData.ParticleFloatDataStride = ParticleRenderData.FloatStride / sizeof(float);
			ParticleMeshRenderData.ParticleHalfDataStride = ParticleRenderData.HalfStride / sizeof(FFloat16);
			ParticleMeshRenderData.ParticleIntDataStride = ParticleRenderData.IntStride / sizeof(int32);
		}
		else
		{
			ParticleMeshRenderData.ParticleFloatSRV = GetSrvOrDefaultFloat(ParticleMeshRenderData.SourceParticleData->GetGPUBufferFloat());
			ParticleMeshRenderData.ParticleHalfSRV = GetSrvOrDefaultHalf(ParticleMeshRenderData.SourceParticleData->GetGPUBufferHalf());
			ParticleMeshRenderData.ParticleIntSRV = GetSrvOrDefaultInt(ParticleMeshRenderData.SourceParticleData->GetGPUBufferInt());
			ParticleMeshRenderData.ParticleFloatDataStride = ParticleMeshRenderData.SourceParticleData->GetFloatStride() / sizeof(float);
			ParticleMeshRenderData.ParticleHalfDataStride = ParticleMeshRenderData.SourceParticleData->GetHalfStride() / sizeof(FFloat16);
			ParticleMeshRenderData.ParticleIntDataStride = ParticleMeshRenderData.SourceParticleData->GetInt32Stride() / sizeof(int32);

			ParticleMeshRenderData.RendererVisTagOffset = RendererVisTagOffset;
			ParticleMeshRenderData.MeshIndexOffset = MeshIndexOffset;
		}
	}
	else
	{
		ParticleMeshRenderData.ParticleFloatSRV = FNiagaraRenderer::GetDummyFloatBuffer();
		ParticleMeshRenderData.ParticleHalfSRV = FNiagaraRenderer::GetDummyHalfBuffer();
		ParticleMeshRenderData.ParticleIntSRV = FNiagaraRenderer::GetDummyIntBuffer();
		ParticleMeshRenderData.ParticleFloatDataStride = 0;
		ParticleMeshRenderData.ParticleHalfDataStride = 0;
		ParticleMeshRenderData.ParticleIntDataStride = 0;
	}
}

void FNiagaraRendererMeshes::InitializeSortInfo(FParticleMeshRenderData& ParticleMeshRenderData, const FNiagaraSceneProxy& SceneProxy, const FSceneView& View, int32 ViewIndex, bool bIsInstancedStereo, FNiagaraGPUSortInfo& OutSortInfo) const
{
	TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = ParticleMeshRenderData.RendererLayout->GetVFVariables_RenderThread();

	OutSortInfo.ParticleCount = ParticleMeshRenderData.SourceParticleData->GetNumInstances();
	OutSortInfo.SortMode = SortMode;
	OutSortInfo.SetSortFlags(GNiagaraGPUSortingUseMaxPrecision != 0, ParticleMeshRenderData.bHasTranslucentMaterials);
	OutSortInfo.bEnableCulling = ParticleMeshRenderData.bNeedsCull;
	OutSortInfo.RendererVisTagAttributeOffset = ParticleMeshRenderData.RendererVisTagOffset;
	OutSortInfo.MeshIndexAttributeOffset = ParticleMeshRenderData.MeshIndexOffset;
	OutSortInfo.RendererVisibility = RendererVisibility;
	OutSortInfo.DistanceCullRange = DistanceCullRange;

	if (ParticleMeshRenderData.bSortCullOnGpu)
	{
		NiagaraEmitterInstanceBatcher* Batcher = SceneProxy.GetBatcher();

		OutSortInfo.ParticleDataFloatSRV = ParticleMeshRenderData.ParticleFloatSRV;
		OutSortInfo.ParticleDataHalfSRV = ParticleMeshRenderData.ParticleHalfSRV;
		OutSortInfo.ParticleDataIntSRV = ParticleMeshRenderData.ParticleIntSRV;
		OutSortInfo.FloatDataStride = ParticleMeshRenderData.ParticleFloatDataStride;
		OutSortInfo.HalfDataStride = ParticleMeshRenderData.ParticleHalfDataStride;
		OutSortInfo.IntDataStride = ParticleMeshRenderData.ParticleIntDataStride;
		OutSortInfo.GPUParticleCountSRV = GetSrvOrDefaultUInt(Batcher->GetGPUInstanceCounterManager().GetInstanceCountBuffer());
		OutSortInfo.GPUParticleCountOffset = ParticleMeshRenderData.SourceParticleData->GetGPUInstanceCountBufferOffset();
	}

	if (ParticleMeshRenderData.SortVariable != INDEX_NONE)
	{
		const FNiagaraRendererVariableInfo& SortVariable = VFVariables[ParticleMeshRenderData.SortVariable];
		OutSortInfo.SortAttributeOffset = ParticleMeshRenderData.bSortCullOnGpu ? SortVariable.GetGPUOffset() : SortVariable.GetEncodedDatasetOffset();
	}

	auto GetViewMatrices = [](const FSceneView& View, FVector& OutViewOrigin) -> const FViewMatrices&
	{
		OutViewOrigin = View.ViewMatrices.GetViewOrigin();

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		const FSceneViewState* ViewState = View.State != nullptr ? View.State->GetConcreteViewState() : nullptr;
		if (ViewState && ViewState->bIsFrozen && ViewState->bIsFrozenViewMatricesCached)
		{
			// Use the frozen view for culling so we can test that it's working
			OutViewOrigin = ViewState->CachedViewMatrices.GetViewOrigin();

			// Don't retrieve the cached matrices for shadow views
			bool bIsShadow = View.GetDynamicMeshElementsShadowCullFrustum() != nullptr;
			if (!bIsShadow)
			{
				return ViewState->CachedViewMatrices;
			}
		}
#endif

		return View.ViewMatrices;
	};

	const TArray<const FSceneView*>& AllViewsInFamily = View.Family->Views;
	const FViewMatrices& ViewMatrices = GetViewMatrices(View, OutSortInfo.ViewOrigin);
	OutSortInfo.ViewDirection = ViewMatrices.GetViewMatrix().GetColumn(2);

#ifdef HMD_MODULE_INCLUDED
	if (View.StereoPass != eSSP_FULL && GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetHMDDevice() != nullptr))
#else
	if (View.StereoPass != eSSP_FULL && AllViewsInFamily.Num() > 1)
#endif	
	{
		// For VR, do distance culling and sorting from a central eye position to prevent differences between views
		const uint32 PairedViewIdx = (ViewIndex & 1) ? (ViewIndex - 1) : (ViewIndex + 1);
		if (AllViewsInFamily.IsValidIndex(PairedViewIdx))
		{
			const FSceneView* PairedView = AllViewsInFamily[PairedViewIdx];
			check(PairedView);

			FVector PairedViewOrigin;
			GetViewMatrices(*PairedView, PairedViewOrigin);
			OutSortInfo.ViewOrigin = 0.5f * (OutSortInfo.ViewOrigin + PairedViewOrigin);
		}
	}

	if (bEnableFrustumCulling)
	{
		if (const FConvexVolume* ShadowFrustum = View.GetDynamicMeshElementsShadowCullFrustum())
		{
			// Ensure we don't break the maximum number of planes here
			// (For an accurate shadow frustum, a tight hull is formed from the silhouette and back-facing planes of the view frustum)
			check(ShadowFrustum->Planes.Num() <= FNiagaraGPUSortInfo::MaxCullPlanes);
			OutSortInfo.CullPlanes = ShadowFrustum->Planes;

			// Remove pre-shadow translation to get the planes in world space
			const FVector PreShadowTranslation = View.GetPreShadowTranslation();
			for (FPlane& Plane : OutSortInfo.CullPlanes)
			{
				Plane.W -= FVector::DotProduct(FVector(Plane), PreShadowTranslation);
			}
		}
		else
		{
			OutSortInfo.CullPlanes.SetNumZeroed(6);

			// Gather the culling planes from the view projection matrix
			const FMatrix& ViewProj = ViewMatrices.GetViewProjectionMatrix();
			ViewProj.GetFrustumNearPlane(OutSortInfo.CullPlanes[0]);
			ViewProj.GetFrustumFarPlane(OutSortInfo.CullPlanes[1]);
			ViewProj.GetFrustumTopPlane(OutSortInfo.CullPlanes[2]);
			ViewProj.GetFrustumBottomPlane(OutSortInfo.CullPlanes[3]);

			ViewProj.GetFrustumLeftPlane(OutSortInfo.CullPlanes[4]);
			if (bIsInstancedStereo)
			{
				// For Instanced Stereo, cull using an extended frustum that encompasses both eyes
				ensure(View.StereoPass == eSSP_LEFT_EYE); // Sanity check that the primary eye is the left
				const FSceneView* RightEyeView = AllViewsInFamily[ViewIndex + 1];
				check(RightEyeView);
				FVector RightEyePos;
				GetViewMatrices(*RightEyeView, RightEyePos).GetViewProjectionMatrix().GetFrustumRightPlane(OutSortInfo.CullPlanes[5]);
			}
			else
			{
				ViewProj.GetFrustumRightPlane(OutSortInfo.CullPlanes[5]);
			}
		}
	}

	if (bLocalSpace)
	{
		OutSortInfo.ViewOrigin = SceneProxy.GetLocalToWorldInverse().TransformPosition(OutSortInfo.ViewOrigin);
		OutSortInfo.ViewDirection = SceneProxy.GetLocalToWorld().GetTransposed().TransformVector(OutSortInfo.ViewDirection);
		if (bEnableFrustumCulling)
		{
			for (FPlane& Plane : OutSortInfo.CullPlanes)
			{
				Plane = Plane.TransformBy(SceneProxy.GetLocalToWorldInverse());
			}
		}
	}

	if (ParticleMeshRenderData.bNeedsCull)
	{
		if ( ParticleMeshRenderData.bSortCullOnGpu )
		{
			OutSortInfo.CullPositionAttributeOffset = VFVariables[ENiagaraMeshVFLayout::Position].GetGPUOffset();
			OutSortInfo.CullOrientationAttributeOffset = VFVariables[ENiagaraMeshVFLayout::Rotation].GetGPUOffset();
			OutSortInfo.CullScaleAttributeOffset = VFVariables[ENiagaraMeshVFLayout::Scale].GetGPUOffset();
		}
		else
		{
			OutSortInfo.CullPositionAttributeOffset = VFVariables[ENiagaraMeshVFLayout::Position].GetEncodedDatasetOffset();
			OutSortInfo.CullOrientationAttributeOffset = VFVariables[ENiagaraMeshVFLayout::Rotation].GetEncodedDatasetOffset();
			OutSortInfo.CullScaleAttributeOffset = VFVariables[ENiagaraMeshVFLayout::Scale].GetEncodedDatasetOffset();
		}
	}
}

void FNiagaraRendererMeshes::PreparePerMeshData(FParticleMeshRenderData& ParticleMeshRenderData, const FNiagaraSceneProxy& SceneProxy, const FMeshData& MeshData) const
{
	// Calculate pivot offset / culling sphere
	ParticleMeshRenderData.CullingSphere = MeshData.LocalCullingSphere;
	if (MeshData.PivotOffsetSpace == ENiagaraMeshPivotOffsetSpace::Mesh)
	{
		ParticleMeshRenderData.WorldSpacePivotOffset = FVector::ZeroVector;
		ParticleMeshRenderData.CullingSphere.Center += MeshData.PivotOffset;
	}
	else
	{
		ParticleMeshRenderData.WorldSpacePivotOffset = MeshData.PivotOffset;
		if (MeshData.PivotOffsetSpace == ENiagaraMeshPivotOffsetSpace::Local ||
			(bLocalSpace && MeshData.PivotOffsetSpace == ENiagaraMeshPivotOffsetSpace::Simulation))
		{
			// The offset is in local space, transform it to world
			ParticleMeshRenderData.WorldSpacePivotOffset = SceneProxy.GetLocalToWorld().TransformVector(ParticleMeshRenderData.WorldSpacePivotOffset);
		}
	}
}

uint32 FNiagaraRendererMeshes::PerformSortAndCull(FParticleMeshRenderData& ParticleMeshRenderData, FGlobalDynamicReadBuffer& ReadBuffer, FNiagaraGPUSortInfo& SortInfo, NiagaraEmitterInstanceBatcher* Batcher, int32 MeshIndex) const
{
	// Emitter mode culls earlier on
	if (SourceMode == ENiagaraRendererSourceDataMode::Emitter)
	{
		ParticleMeshRenderData.ParticleSortedIndicesSRV = GFNiagaraNullSortedIndicesVertexBuffer.VertexBufferSRV.GetReference();
		ParticleMeshRenderData.ParticleSortedIndicesOffset = 0xffffffff;
		return 1;
	}

	uint32 NumInstances = ParticleMeshRenderData.SourceParticleData->GetNumInstances();
	if (ParticleMeshRenderData.bNeedsCull || ParticleMeshRenderData.bNeedsSort)
	{
		SortInfo.LocalBSphere = ParticleMeshRenderData.CullingSphere;
		SortInfo.CullingWorldSpaceOffset = ParticleMeshRenderData.WorldSpacePivotOffset;
		SortInfo.MeshIndex = MeshIndex;
		if (ParticleMeshRenderData.bSortCullOnGpu)
		{
			SortInfo.CulledGPUParticleCountOffset = ParticleMeshRenderData.bNeedsCull ? Batcher->GetGPUInstanceCounterManager().AcquireCulledEntry() : INDEX_NONE;
			if (Batcher->AddSortedGPUSimulation(SortInfo))
			{
				ParticleMeshRenderData.ParticleSortedIndicesSRV = SortInfo.AllocationInfo.BufferSRV;
				ParticleMeshRenderData.ParticleSortedIndicesOffset = SortInfo.AllocationInfo.BufferOffset;
			}
		}
		else
		{
			FGlobalDynamicReadBuffer::FAllocation SortedIndices;
			SortedIndices = ReadBuffer.AllocateInt32(NumInstances);
			NumInstances = SortAndCullIndices(SortInfo, *ParticleMeshRenderData.SourceParticleData, SortedIndices);
			ParticleMeshRenderData.ParticleSortedIndicesSRV = SortedIndices.SRV;
			ParticleMeshRenderData.ParticleSortedIndicesOffset = 0;
		}
	}
	else
	{
		ParticleMeshRenderData.ParticleSortedIndicesSRV = GFNiagaraNullSortedIndicesVertexBuffer.VertexBufferSRV.GetReference();
		ParticleMeshRenderData.ParticleSortedIndicesOffset = 0xffffffff;
	}
	return NumInstances;
}

FNiagaraMeshUniformBufferRef FNiagaraRendererMeshes::CreatePerViewUniformBuffer(FParticleMeshRenderData& ParticleMeshRenderData, const FSceneView& View, const FMeshData& MeshData, const FNiagaraSceneProxy& SceneProxy) const
{
	// Compute the per-view uniform buffers.
	FNiagaraMeshUniformParameters PerViewUniformParameters;
	FMemory::Memzero(&PerViewUniformParameters, sizeof(PerViewUniformParameters)); // Clear unset bytes

	PerViewUniformParameters.bLocalSpace = bLocalSpace;
	PerViewUniformParameters.DeltaSeconds = View.Family->DeltaWorldTime;
	PerViewUniformParameters.MeshScale = MeshData.Scale;

	if (MeshData.PivotOffsetSpace == ENiagaraMeshPivotOffsetSpace::Mesh)
	{
		PerViewUniformParameters.PivotOffset = MeshData.PivotOffset;
		PerViewUniformParameters.bPivotOffsetIsWorldSpace = false;
	}
	else
	{
		PerViewUniformParameters.PivotOffset = ParticleMeshRenderData.WorldSpacePivotOffset;
		PerViewUniformParameters.bPivotOffsetIsWorldSpace = true;
	}

	PerViewUniformParameters.MaterialParamValidMask = MaterialParamValidMask;
	PerViewUniformParameters.SubImageSize = FVector4(SubImageSize.X, SubImageSize.Y, 1.0f / SubImageSize.X, 1.0f / SubImageSize.Y);
	PerViewUniformParameters.SubImageBlendMode = bSubImageBlend;
	PerViewUniformParameters.FacingMode = (uint32)FacingMode;
	PerViewUniformParameters.bLockedAxisEnable = bLockedAxisEnable;
	PerViewUniformParameters.LockedAxis = LockedAxis;
	PerViewUniformParameters.LockedAxisSpace = (uint32)LockedAxisSpace;
	PerViewUniformParameters.NiagaraFloatDataStride = ParticleMeshRenderData.ParticleFloatDataStride;
	PerViewUniformParameters.NiagaraParticleDataFloat = ParticleMeshRenderData.ParticleFloatSRV;
	PerViewUniformParameters.NiagaraParticleDataHalf = ParticleMeshRenderData.ParticleHalfSRV;

	PerViewUniformParameters.SortedIndices = ParticleMeshRenderData.ParticleSortedIndicesSRV;
	PerViewUniformParameters.SortedIndicesOffset = ParticleMeshRenderData.ParticleSortedIndicesOffset;

	PerViewUniformParameters.DefaultPos = bLocalSpace ? FVector4(0.0f, 0.0f, 0.0f, 1.0f) : FVector4(SceneProxy.GetLocalToWorld().GetOrigin());
	PerViewUniformParameters.DefaultPrevPos = PerViewUniformParameters.DefaultPos;
	PerViewUniformParameters.DefaultVelocity = FVector(0.f, 0.0f, 0.0f);
	PerViewUniformParameters.DefaultPrevVelocity = PerViewUniformParameters.DefaultVelocity;
	PerViewUniformParameters.DefaultColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	PerViewUniformParameters.DefaultScale = FVector(1.0f, 1.0f, 1.0f);
	PerViewUniformParameters.DefaultPrevScale = PerViewUniformParameters.DefaultScale;
	PerViewUniformParameters.DefaultRotation = FVector4(0.0f, 0.0f, 0.0f, 1.0f);
	PerViewUniformParameters.DefaultPrevRotation = PerViewUniformParameters.DefaultRotation;
	PerViewUniformParameters.DefaultMatRandom = 0.0f;
	PerViewUniformParameters.DefaultNormAge = 0.0f;

	PerViewUniformParameters.DefaultSubImage = 0.0f;
	PerViewUniformParameters.DefaultDynamicMaterialParameter0 = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	PerViewUniformParameters.DefaultDynamicMaterialParameter1 = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	PerViewUniformParameters.DefaultDynamicMaterialParameter2 = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	PerViewUniformParameters.DefaultDynamicMaterialParameter3 = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	PerViewUniformParameters.DefaultCamOffset = 0.0f;
	PerViewUniformParameters.DefaultPrevCamOffset = PerViewUniformParameters.DefaultCamOffset;

	PerViewUniformParameters.PrevPositionDataOffset = INDEX_NONE;
	PerViewUniformParameters.PrevScaleDataOffset = INDEX_NONE;
	PerViewUniformParameters.PrevRotationDataOffset = INDEX_NONE;
	PerViewUniformParameters.PrevCameraOffsetDataOffset = INDEX_NONE;
	PerViewUniformParameters.PrevVelocityDataOffset = INDEX_NONE;

	if (SourceMode == ENiagaraRendererSourceDataMode::Particles)
	{
		TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = ParticleMeshRenderData.RendererLayout->GetVFVariables_RenderThread();
		PerViewUniformParameters.PositionDataOffset = VFVariables[ENiagaraMeshVFLayout::Position].GetGPUOffset();
		PerViewUniformParameters.VelocityDataOffset = VFVariables[ENiagaraMeshVFLayout::Velocity].GetGPUOffset();
		PerViewUniformParameters.ColorDataOffset = VFVariables[ENiagaraMeshVFLayout::Color].GetGPUOffset();
		PerViewUniformParameters.ScaleDataOffset = VFVariables[ENiagaraMeshVFLayout::Scale].GetGPUOffset();
		PerViewUniformParameters.RotationDataOffset = VFVariables[ENiagaraMeshVFLayout::Rotation].GetGPUOffset();
		PerViewUniformParameters.MaterialRandomDataOffset = VFVariables[ENiagaraMeshVFLayout::MaterialRandom].GetGPUOffset();
		PerViewUniformParameters.NormalizedAgeDataOffset = VFVariables[ENiagaraMeshVFLayout::NormalizedAge].GetGPUOffset();

		PerViewUniformParameters.SubImageDataOffset = VFVariables[ENiagaraMeshVFLayout::SubImage].GetGPUOffset();
		PerViewUniformParameters.MaterialParamDataOffset = VFVariables[ENiagaraMeshVFLayout::DynamicParam0].GetGPUOffset();
		PerViewUniformParameters.MaterialParam1DataOffset = VFVariables[ENiagaraMeshVFLayout::DynamicParam1].GetGPUOffset();
		PerViewUniformParameters.MaterialParam2DataOffset = VFVariables[ENiagaraMeshVFLayout::DynamicParam2].GetGPUOffset();
		PerViewUniformParameters.MaterialParam3DataOffset = VFVariables[ENiagaraMeshVFLayout::DynamicParam3].GetGPUOffset();
		PerViewUniformParameters.CameraOffsetDataOffset = VFVariables[ENiagaraMeshVFLayout::CameraOffset].GetGPUOffset();
		
		if (bAccurateMotionVectors)
		{
			PerViewUniformParameters.PrevPositionDataOffset = VFVariables[ENiagaraMeshVFLayout::PrevPosition].GetGPUOffset();
			PerViewUniformParameters.PrevScaleDataOffset = VFVariables[ENiagaraMeshVFLayout::PrevScale].GetGPUOffset();
			PerViewUniformParameters.PrevRotationDataOffset = VFVariables[ENiagaraMeshVFLayout::PrevRotation].GetGPUOffset();
			PerViewUniformParameters.PrevCameraOffsetDataOffset = VFVariables[ENiagaraMeshVFLayout::PrevCameraOffset].GetGPUOffset();
			PerViewUniformParameters.PrevVelocityDataOffset = VFVariables[ENiagaraMeshVFLayout::PrevVelocity].GetGPUOffset();
		}
	}
	else if (SourceMode == ENiagaraRendererSourceDataMode::Emitter) // Clear all these out because we will be using the defaults to specify them
	{
		PerViewUniformParameters.PositionDataOffset = INDEX_NONE;
		PerViewUniformParameters.VelocityDataOffset = INDEX_NONE;
		PerViewUniformParameters.ColorDataOffset = INDEX_NONE;
		PerViewUniformParameters.ScaleDataOffset = INDEX_NONE;
		PerViewUniformParameters.RotationDataOffset = INDEX_NONE;
		PerViewUniformParameters.MaterialRandomDataOffset = INDEX_NONE;
		PerViewUniformParameters.NormalizedAgeDataOffset = INDEX_NONE;

		PerViewUniformParameters.SubImageDataOffset = INDEX_NONE;
		PerViewUniformParameters.MaterialParamDataOffset = INDEX_NONE;
		PerViewUniformParameters.MaterialParam1DataOffset = INDEX_NONE;
		PerViewUniformParameters.MaterialParam2DataOffset = INDEX_NONE;
		PerViewUniformParameters.MaterialParam3DataOffset = INDEX_NONE;
		PerViewUniformParameters.CameraOffsetDataOffset = INDEX_NONE;
	}
	else
	{
		// Unsupported source data mode detected
		check(SourceMode <= ENiagaraRendererSourceDataMode::Emitter);
	}

	if (bSetAnyBoundVars)
	{
		const FNiagaraDynamicDataMesh* DynamicDataMesh = ParticleMeshRenderData.DynamicDataMesh;
		const uint8* ParameterBoundData = DynamicDataMesh->ParameterDataBound.GetData();

		const int32 NumVFOffsets = bAccurateMotionVectors ? ENiagaraMeshVFLayout::Type::Num_Max : ENiagaraMeshVFLayout::Type::Num_Default;
		for (int32 i = 0; i < NumVFOffsets; i++)
		{
			if (VFBoundOffsetsInParamStore[i] != INDEX_NONE && DynamicDataMesh->ParameterDataBound.IsValidIndex(VFBoundOffsetsInParamStore[i]))
			{
				switch (i)
				{
				case ENiagaraMeshVFLayout::Type::Position:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultPos, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(FVector));
					break;
				case ENiagaraMeshVFLayout::Type::Velocity:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultVelocity, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(FVector));
					break;
				case ENiagaraMeshVFLayout::Type::Color:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultColor, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(FLinearColor));
					break;
				case ENiagaraMeshVFLayout::Type::Scale:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultScale, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(FVector));
					break;
				case ENiagaraMeshVFLayout::Type::Rotation:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultRotation, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(FVector4));
					break;
				case ENiagaraMeshVFLayout::Type::MaterialRandom:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultMatRandom, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(float));
					break;
				case ENiagaraMeshVFLayout::Type::NormalizedAge:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultNormAge, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(float));
					break;
				case ENiagaraMeshVFLayout::Type::CustomSorting:
					// unsupported for now...
					break;
				case ENiagaraMeshVFLayout::Type::SubImage:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultSubImage, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(float));
					break;
				case ENiagaraMeshVFLayout::Type::DynamicParam0:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultDynamicMaterialParameter0, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(FVector4));
					break;
				case ENiagaraMeshVFLayout::Type::DynamicParam1:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultDynamicMaterialParameter1, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(FVector4));
					break;
				case ENiagaraMeshVFLayout::Type::DynamicParam2:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultDynamicMaterialParameter2, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(FVector4));
					break;
				case ENiagaraMeshVFLayout::Type::DynamicParam3:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultDynamicMaterialParameter3, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(FVector4));
					break;
				case ENiagaraMeshVFLayout::Type::CameraOffset:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultCamOffset, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(float));
					break;
				case ENiagaraMeshVFLayout::Type::PrevPosition:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultPrevPos, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(FVector));
					break;
				case ENiagaraMeshVFLayout::Type::PrevScale:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultPrevScale, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(FVector));
					break;
				case ENiagaraMeshVFLayout::Type::PrevRotation:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultPrevRotation, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(FVector4));
					break;
				case ENiagaraMeshVFLayout::Type::PrevCameraOffset:
					FMemory::Memcpy(&PerViewUniformParameters.DefaultPrevCamOffset, ParameterBoundData + VFBoundOffsetsInParamStore[i], sizeof(float));
					break;
				}
			}
			else
			{
				// If these prev values aren't bound to the host parameters, but their current values are, copy them
				switch (i)
				{
				case ENiagaraMeshVFLayout::Type::PrevPosition:
					PerViewUniformParameters.DefaultPrevPos = PerViewUniformParameters.DefaultPos;
					break;
				case ENiagaraMeshVFLayout::Type::PrevScale:
					PerViewUniformParameters.DefaultPrevScale = PerViewUniformParameters.DefaultScale;
					break;
				case ENiagaraMeshVFLayout::Type::PrevRotation:
					PerViewUniformParameters.DefaultPrevRotation = PerViewUniformParameters.DefaultRotation;
					break;
				case ENiagaraMeshVFLayout::Type::PrevCameraOffset:
					PerViewUniformParameters.DefaultPrevCamOffset = PerViewUniformParameters.DefaultCamOffset;
					break;
				default:
					break;
				}
			}
		}
	}

	return FNiagaraMeshUniformBufferRef::CreateUniformBufferImmediate(PerViewUniformParameters, UniformBuffer_SingleFrame);
}

void FNiagaraRendererMeshes::CreateMeshBatchForSection(
	FMeshBatch& MeshBatch,
	FVertexFactory& VertexFactory,
	FMaterialRenderProxy& MaterialProxy,
	const FNiagaraSceneProxy& SceneProxy,
	const FMeshData& MeshData,
	const FStaticMeshLODResources& LODModel,
	const FStaticMeshSection& Section,
	const FSceneView& View,
	int32 ViewIndex,
	uint32 NumInstances,
	uint32 GPUCountBufferOffset,
	bool bIsWireframe,
	bool bIsInstancedStereo,
	bool bDoGPUCulling
) const
{
	MeshBatch.VertexFactory = &VertexFactory;
	MeshBatch.LCI = NULL;
	MeshBatch.ReverseCulling = SceneProxy.IsLocalToWorldDeterminantNegative();
	MeshBatch.CastShadow = SceneProxy.CastsDynamicShadow();
#if RHI_RAYTRACING
	MeshBatch.CastRayTracedShadow = SceneProxy.CastsDynamicShadow();
#endif
	MeshBatch.DepthPriorityGroup = (ESceneDepthPriorityGroup)SceneProxy.GetDepthPriorityGroup(&View);

	FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
	BatchElement.PrimitiveUniformBuffer = IsMotionBlurEnabled() ? SceneProxy.GetUniformBuffer() : SceneProxy.GetUniformBufferNoVelocity();
	BatchElement.FirstIndex = 0;
	BatchElement.MinVertexIndex = 0;
	BatchElement.MaxVertexIndex = 0;
	BatchElement.NumInstances = NumInstances;

	if (bIsWireframe)
	{
		if (LODModel.AdditionalIndexBuffers && LODModel.AdditionalIndexBuffers->WireframeIndexBuffer.IsInitialized())
		{
			MeshBatch.Type = PT_LineList;
			MeshBatch.MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
			BatchElement.FirstIndex = 0;
			BatchElement.IndexBuffer = &LODModel.AdditionalIndexBuffers->WireframeIndexBuffer;
			BatchElement.NumPrimitives = LODModel.AdditionalIndexBuffers->WireframeIndexBuffer.GetNumIndices() / 2;
		}
		else
		{
			MeshBatch.Type = PT_TriangleList;
			MeshBatch.MaterialRenderProxy = &MaterialProxy;
			MeshBatch.bWireframe = true;
			BatchElement.FirstIndex = 0;
			BatchElement.IndexBuffer = &LODModel.IndexBuffer;
			BatchElement.NumPrimitives = LODModel.IndexBuffer.GetNumIndices() / 3;
		}
	}
	else
	{
		MeshBatch.Type = PT_TriangleList;
		MeshBatch.MaterialRenderProxy = &MaterialProxy;
		BatchElement.IndexBuffer = &LODModel.IndexBuffer;
		BatchElement.FirstIndex = Section.FirstIndex;
		BatchElement.NumPrimitives = Section.NumTriangles;
	}

	if ((SourceMode == ENiagaraRendererSourceDataMode::Particles) && (GPUCountBufferOffset != INDEX_NONE))
	{
		// We need to use indirect draw args, because the number of actual instances is coming from the GPU
		auto Batcher = SceneProxy.GetBatcher();
		check(Batcher);

		auto& CountManager = Batcher->GetGPUInstanceCounterManager();
		auto IndirectDraw = CountManager.AddDrawIndirect(
			GPUCountBufferOffset,
			Section.NumTriangles * 3,
			Section.FirstIndex,
			bIsInstancedStereo,
			bDoGPUCulling);

		BatchElement.NumPrimitives = 0;
		BatchElement.IndirectArgsBuffer = IndirectDraw.Buffer;
		BatchElement.IndirectArgsOffset = IndirectDraw.Offset;
	}
	else
	{
		check(BatchElement.NumPrimitives > 0);
	}

	MeshBatch.bCanApplyViewModeOverrides = true;
	MeshBatch.bUseWireframeSelectionColoring = SceneProxy.IsSelected();
}

void FNiagaraRendererMeshes::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy* SceneProxy) const
{
	check(SceneProxy);
	PARTICLE_PERF_STAT_CYCLES_RT(SceneProxy->PerfStatsContext, GetDynamicMeshElements);

	// Prepare our particle render data
	// This will also determine if we have anything to render
	FParticleMeshRenderData ParticleMeshRenderData;
	PrepareParticleMeshRenderData(ParticleMeshRenderData, DynamicDataRender, SceneProxy);

	if (ParticleMeshRenderData.SourceParticleData == nullptr )
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderMeshes);
#if STATS
	FScopeCycleCounter EmitterStatsCounter(EmitterStatID);
#endif

	PrepareParticleRenderBuffers(ParticleMeshRenderData, Collector.GetDynamicReadBuffer());

	// Generate mesh batches per view
	const int32 NumViews = Views.Num();
	for (int32 ViewIndex = 0; ViewIndex < NumViews; ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];

			const bool bIsInstancedStereo = View->bIsInstancedStereoEnabled && IStereoRendering::IsStereoEyeView(*View);
			if (bIsInstancedStereo && !IStereoRendering::IsAPrimaryView(*View))
			{
				// One eye renders everything, so we can skip non-primaries
				continue;
			}				

			if (SourceMode == ENiagaraRendererSourceDataMode::Emitter && bEnableCulling)
			{
				FVector ViewOrigin = View->ViewMatrices.GetViewOrigin();
				FVector RefPosition = SceneProxy->GetLocalToWorld().GetOrigin();

#if WITH_NIAGARA_COMPONENT_PREVIEW_DATA
				float DistSquared = SceneProxy->PreviewLODDistance >= 0.0f ? SceneProxy->PreviewLODDistance * SceneProxy->PreviewLODDistance : FVector::DistSquared(RefPosition, ViewOrigin);
#else
				float DistSquared = FVector::DistSquared(RefPosition, ViewOrigin);
#endif
				if (DistSquared < DistanceCullRange.X * DistanceCullRange.X || DistSquared > DistanceCullRange.Y * DistanceCullRange.Y)
				{
					// Distance cull the whole emitter
					continue;
				}
			}

			// Initialize sort parameters that are mesh/section invariant
			FNiagaraGPUSortInfo SortInfo;
			if (ParticleMeshRenderData.bNeedsSort || ParticleMeshRenderData.bNeedsCull)
			{
				InitializeSortInfo(ParticleMeshRenderData, *SceneProxy, *View, ViewIndex, bIsInstancedStereo, SortInfo);
			}

			NiagaraEmitterInstanceBatcher* Batcher = SceneProxy->GetBatcher();
			for (int32 MeshIndex = 0; MeshIndex < Meshes.Num(); ++MeshIndex)
			{
				// No binding for mesh index or we don't allow culling we will only render the first mesh for all particles
				if (MeshIndex > 0 && (ParticleMeshRenderData.MeshIndexOffset == INDEX_NONE || !ParticleMeshRenderData.bNeedsCull))
				{
					break;
				}

				const FMeshData& MeshData = Meshes[MeshIndex];

				// @TODO : support multiple LOD
				const int32 LODIndex = GetLODIndex(MeshIndex);
				if (LODIndex == INDEX_NONE)
				{
					continue;
				}

				const FStaticMeshLODResources& LODModel = MeshData.RenderData->LODResources[LODIndex];
				const int32 SectionCount = LODModel.Sections.Num();

				FMeshCollectorResourcesBase* CollectorResources;
				if (bAccurateMotionVectors)
				{
					CollectorResources = &Collector.AllocateOneFrameResource<FMeshCollectorResourcesEx>();
				}
				else
				{
					CollectorResources = &Collector.AllocateOneFrameResource<FMeshCollectorResources>();
				}				

				// Get the next vertex factory to use
				// TODO: Find a way to safely pool these such that they won't be concurrently accessed by multiple views
				FNiagaraMeshVertexFactory& VertexFactory = CollectorResources->GetVertexFactory();
				VertexFactory.SetParticleFactoryType(NVFT_Mesh);
				VertexFactory.SetMeshIndex(MeshIndex);
				VertexFactory.SetLODIndex(LODIndex);
				VertexFactory.InitResource();
				SetupVertexFactory(VertexFactory, LODModel);

				PreparePerMeshData(ParticleMeshRenderData, *SceneProxy, MeshData);

				// Sort/Cull particles if needed.
				const uint32 NumInstances = PerformSortAndCull(ParticleMeshRenderData, Collector.GetDynamicReadBuffer(), SortInfo, Batcher, MeshData.SourceMeshIndex);
				if (NumInstances > 0)
				{
					// Increment stats
					INC_DWORD_STAT_BY(STAT_NiagaraNumMeshVerts, NumInstances * LODModel.GetNumVertices());
					INC_DWORD_STAT_BY(STAT_NiagaraNumMeshes, NumInstances);

					FNiagaraMeshUniformBufferRef PerViewUniformBuffer = CreatePerViewUniformBuffer(ParticleMeshRenderData, *View, MeshData, *SceneProxy);
					VertexFactory.SetUniformBuffer(PerViewUniformBuffer);
					CollectorResources->UniformBuffer = PerViewUniformBuffer;

					const bool bIsWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;
					for (int32 SectionIndex = 0; SectionIndex < SectionCount; SectionIndex++)
					{
						const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
						const uint32 RemappedMaterialIndex = MeshData.MaterialRemapTable[Section.MaterialIndex];
						if (!ParticleMeshRenderData.DynamicDataMesh->Materials.IsValidIndex(RemappedMaterialIndex))
						{
							// This should never occur. Otherwise, the section data changed since initialization
							continue;
						}

						FMaterialRenderProxy* MaterialProxy = ParticleMeshRenderData.DynamicDataMesh->Materials[RemappedMaterialIndex];
						if (Section.NumTriangles == 0 || MaterialProxy == NULL)
						{
							//@todo. This should never occur, but it does occasionally.
							continue;
						}

						const uint32 GPUCountBufferOffset = SortInfo.CulledGPUParticleCountOffset != INDEX_NONE ? SortInfo.CulledGPUParticleCountOffset : ParticleMeshRenderData.SourceParticleData->GetGPUInstanceCountBufferOffset();
						FMeshBatch& MeshBatch = Collector.AllocateMesh();
						CreateMeshBatchForSection(
							MeshBatch, VertexFactory, *MaterialProxy, *SceneProxy, MeshData, LODModel, Section, *View, ViewIndex, NumInstances,
							GPUCountBufferOffset, bIsWireframe, bIsInstancedStereo,
							ParticleMeshRenderData.bNeedsCull && ParticleMeshRenderData.bSortCullOnGpu
						);

						Collector.AddMesh(ViewIndex, MeshBatch);
					}
				}
			}
		}
	}
}

#if RHI_RAYTRACING

void FNiagaraRendererMeshes::GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* SceneProxy)
{
	if (!CVarRayTracingNiagaraMeshes.GetValueOnRenderThread())
	{
		return;
	}

	check(SceneProxy);

	// Prepare our particle render data
	// This will also determine if we have anything to render
	FParticleMeshRenderData ParticleMeshRenderData;
	PrepareParticleMeshRenderData(ParticleMeshRenderData, DynamicDataRender, SceneProxy);

	if (ParticleMeshRenderData.SourceParticleData == nullptr)
	{
		return;
	}

	// Disable sorting and culling as we manage this ourself
	ParticleMeshRenderData.bNeedsSort = false;
	ParticleMeshRenderData.bNeedsCull = false;
	ParticleMeshRenderData.bSortCullOnGpu = false;

	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderMeshes);

	PrepareParticleRenderBuffers(ParticleMeshRenderData, Context.RayTracingMeshResourceCollector.GetDynamicReadBuffer());

	const int32 ViewIndex = 0;
	const FSceneView* View = Context.ReferenceView;
	const bool bIsInstancedStereo = View->bIsInstancedStereoEnabled && IStereoRendering::IsStereoEyeView(*View);

	// Initialize sort parameters that are mesh/section invariant
	FNiagaraGPUSortInfo SortInfo;
	if (ParticleMeshRenderData.bNeedsSort || ParticleMeshRenderData.bNeedsCull)
	{
		InitializeSortInfo(ParticleMeshRenderData, *SceneProxy, *View, ViewIndex, bIsInstancedStereo, SortInfo);
	}

	for (int32 MeshIndex = 0; MeshIndex < Meshes.Num(); ++MeshIndex)
	{
		// No binding for mesh index we only render the first mesh not all of them
		if ( (MeshIndex > 0) && (MeshIndexOffset == INDEX_NONE) )
		{
			break;
		}

		const FMeshData& MeshData = Meshes[MeshIndex];
		const int32 LODIndex = GetLODIndex(MeshIndex);
		if (LODIndex == INDEX_NONE)
		{
			continue;
		}

		const FStaticMeshLODResources& LODModel = MeshData.RenderData->LODResources[LODIndex];
		const FStaticMeshVertexFactories& VFs = MeshData.RenderData->LODVertexFactories[LODIndex];
		FRayTracingGeometry& Geometry = MeshData.RenderData->LODResources[LODIndex].RayTracingGeometry;
		FRayTracingInstance RayTracingInstance;
		RayTracingInstance.Geometry = &Geometry;

		FMeshCollectorResourcesBase* CollectorResources;
		if (bAccurateMotionVectors)
		{
			CollectorResources = &Context.RayTracingMeshResourceCollector.AllocateOneFrameResource<FMeshCollectorResourcesEx>();
		}
		else
		{
			CollectorResources = &Context.RayTracingMeshResourceCollector.AllocateOneFrameResource<FMeshCollectorResources>();
		}

		// Get the next vertex factory to use
		// TODO: Find a way to safely pool these such that they won't be concurrently accessed by multiple views
		FNiagaraMeshVertexFactory& VertexFactory = CollectorResources->GetVertexFactory();
		VertexFactory.SetParticleFactoryType(NVFT_Mesh);
		VertexFactory.SetMeshIndex(MeshIndex);
		VertexFactory.SetLODIndex(LODIndex);
		VertexFactory.InitResource();
		SetupVertexFactory(VertexFactory, LODModel);

		PreparePerMeshData(ParticleMeshRenderData, *SceneProxy, MeshData);

		// Sort/Cull particles if needed.
		NiagaraEmitterInstanceBatcher* Batcher = SceneProxy->GetBatcher();
		const uint32 NumInstances = PerformSortAndCull(ParticleMeshRenderData, Context.RayTracingMeshResourceCollector.GetDynamicReadBuffer(), SortInfo, Batcher, MeshData.SourceMeshIndex);
		if ( NumInstances == 0 )
		{
			continue;
		}

		FNiagaraMeshUniformBufferRef PerViewUniformBuffer = CreatePerViewUniformBuffer(ParticleMeshRenderData, *View, MeshData, *SceneProxy);
		VertexFactory.SetUniformBuffer(PerViewUniformBuffer);
		CollectorResources->UniformBuffer = PerViewUniformBuffer;

		for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
		{
			const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
			if (Section.NumTriangles == 0)
			{
				continue;
			}

			const uint32 RemappedMaterialIndex = MeshData.MaterialRemapTable[Section.MaterialIndex];
			if (!ParticleMeshRenderData.DynamicDataMesh->Materials.IsValidIndex(RemappedMaterialIndex))
			{
				// This should never occur. Otherwise, the section data changed since initialization
				continue;
			}

			FMaterialRenderProxy* MaterialProxy = ParticleMeshRenderData.DynamicDataMesh->Materials[RemappedMaterialIndex];
			if (MaterialProxy == nullptr)
			{
				continue;
			}

			FMeshBatch MeshBatch;
			CreateMeshBatchForSection(MeshBatch, VertexFactory, *MaterialProxy, *SceneProxy, MeshData, LODModel, Section, *View, ViewIndex, NumInstances, INDEX_NONE, false, bIsInstancedStereo, ParticleMeshRenderData.bNeedsCull && ParticleMeshRenderData.bSortCullOnGpu);
			MeshBatch.SegmentIndex = SectionIndex;
			MeshBatch.LODIndex = LODIndex;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			MeshBatch.VisualizeLODIndex = LODIndex;
#endif

			MeshBatch.bCanApplyViewModeOverrides = false;

			MeshBatch.Elements[0].VertexFactoryUserData = VFs.VertexFactory.GetUniformBuffer();
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			MeshBatch.Elements[0].VisualizeElementIndex = SectionIndex;
#endif

			RayTracingInstance.Materials.Add(MeshBatch);
		}

		if (RayTracingInstance.Materials.Num() == 0 || LODModel.Sections.Num() != RayTracingInstance.Materials.Num())
		{
			continue;
		}

		// Emitter source mode?
		const FMatrix LocalTransform(SceneProxy->GetLocalToWorld());
		if (SourceMode == ENiagaraRendererSourceDataMode::Emitter)
		{
			FVector Pos = bLocalSpace ? FVector() : LocalTransform.GetOrigin();
			FVector Scale{ 1.0f, 1.0f, 1.0f };
			FQuat Rot = FQuat::Identity;

			if (bSetAnyBoundVars)
			{
				const uint8* ParameterBoundData = ParticleMeshRenderData.DynamicDataMesh->ParameterDataBound.GetData();
				if (VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Position] != INDEX_NONE
					&& ParticleMeshRenderData.DynamicDataMesh->ParameterDataBound.IsValidIndex(VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Position]))
				{
					FMemory::Memcpy(&Pos, ParameterBoundData + VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Position], sizeof(FVector));
				}
				if (VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Scale] != INDEX_NONE
					&& ParticleMeshRenderData.DynamicDataMesh->ParameterDataBound.IsValidIndex(VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Scale]))
				{
					FMemory::Memcpy(&Scale, ParameterBoundData + VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Scale], sizeof(FVector));
				}
				if (VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Rotation] != INDEX_NONE
					&& ParticleMeshRenderData.DynamicDataMesh->ParameterDataBound.IsValidIndex(VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Rotation]))
				{
					FMemory::Memcpy(&Rot, ParameterBoundData + VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Rotation], sizeof(FVector4));
				}
			}

			FVector4 Transform1 = FVector4(1.0f, 0.0f, 0.0f, Pos.X);
			FVector4 Transform2 = FVector4(0.0f, 1.0f, 0.0f, Pos.Y);
			FVector4 Transform3 = FVector4(0.0f, 0.0f, 1.0f, Pos.Z);

			FTransform RotationTransform(Rot.GetNormalized());
			FMatrix RotationMatrix = RotationTransform.ToMatrixWithScale();

			Transform1.X = RotationMatrix.M[0][0];
			Transform1.Y = RotationMatrix.M[0][1];
			Transform1.Z = RotationMatrix.M[0][2];

			Transform2.X = RotationMatrix.M[1][0];
			Transform2.Y = RotationMatrix.M[1][1];
			Transform2.Z = RotationMatrix.M[1][2];

			Transform3.X = RotationMatrix.M[2][0];
			Transform3.Y = RotationMatrix.M[2][1];
			Transform3.Z = RotationMatrix.M[2][2];

			FMatrix ScaleMatrix(FMatrix::Identity);
			ScaleMatrix.M[0][0] *= Scale.X;
			ScaleMatrix.M[1][1] *= Scale.Y;
			ScaleMatrix.M[2][2] *= Scale.Z;

			FMatrix InstanceTransform = FMatrix(FPlane(Transform1), FPlane(Transform2), FPlane(Transform3), FPlane(0.0, 0.0, 0.0, 1.0));
			InstanceTransform = InstanceTransform * ScaleMatrix;
			InstanceTransform = InstanceTransform.GetTransposed();

			if (bLocalSpace)
			{
				InstanceTransform = InstanceTransform * LocalTransform;
			}

			RayTracingInstance.InstanceTransforms.Add(InstanceTransform);
		}
		else 
		{
			TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = ParticleMeshRenderData.RendererLayout->GetVFVariables_RenderThread();
			if (SimTarget == ENiagaraSimTarget::CPUSim)
			{
				const int32 TotalFloatSize = ParticleMeshRenderData.RendererLayout->GetTotalFloatComponents_RenderThread() * ParticleMeshRenderData.SourceParticleData->GetNumInstances();
				const int32 ComponentStrideDest = ParticleMeshRenderData.SourceParticleData->GetNumInstances() * sizeof(float);

				//ENiagaraMeshVFLayout::Transform just contains a Quat, not the whole transform
				const FNiagaraRendererVariableInfo& VarPositionInfo = VFVariables[ENiagaraMeshVFLayout::Position];
				const FNiagaraRendererVariableInfo& VarScaleInfo = VFVariables[ENiagaraMeshVFLayout::Scale];
				const FNiagaraRendererVariableInfo& VarTransformInfo = VFVariables[ENiagaraMeshVFLayout::Rotation];

				const int32 PositionBaseCompOffset = VarPositionInfo.DatasetOffset;
				const int32 ScaleBaseCompOffset = VarScaleInfo.DatasetOffset;
				const int32 TransformBaseCompOffset = VarTransformInfo.DatasetOffset;

				const float* RESTRICT PositionX = reinterpret_cast<const float*>(ParticleMeshRenderData.SourceParticleData->GetComponentPtrFloat(PositionBaseCompOffset));
				const float* RESTRICT PositionY = reinterpret_cast<const float*>(ParticleMeshRenderData.SourceParticleData->GetComponentPtrFloat(PositionBaseCompOffset + 1));
				const float* RESTRICT PositionZ = reinterpret_cast<const float*>(ParticleMeshRenderData.SourceParticleData->GetComponentPtrFloat(PositionBaseCompOffset + 2));

				const float* RESTRICT ScaleX = reinterpret_cast<const float*>(ParticleMeshRenderData.SourceParticleData->GetComponentPtrFloat(ScaleBaseCompOffset));
				const float* RESTRICT ScaleY = reinterpret_cast<const float*>(ParticleMeshRenderData.SourceParticleData->GetComponentPtrFloat(ScaleBaseCompOffset + 1));
				const float* RESTRICT ScaleZ = reinterpret_cast<const float*>(ParticleMeshRenderData.SourceParticleData->GetComponentPtrFloat(ScaleBaseCompOffset + 2));

				const float* RESTRICT QuatArrayX = reinterpret_cast<const float*>(ParticleMeshRenderData.SourceParticleData->GetComponentPtrFloat(TransformBaseCompOffset));
				const float* RESTRICT QuatArrayY = reinterpret_cast<const float*>(ParticleMeshRenderData.SourceParticleData->GetComponentPtrFloat(TransformBaseCompOffset + 1));
				const float* RESTRICT QuatArrayZ = reinterpret_cast<const float*>(ParticleMeshRenderData.SourceParticleData->GetComponentPtrFloat(TransformBaseCompOffset + 2));
				const float* RESTRICT QuatArrayW = reinterpret_cast<const float*>(ParticleMeshRenderData.SourceParticleData->GetComponentPtrFloat(TransformBaseCompOffset + 3));

				const int32* RESTRICT RenderVisibilityData = RendererVisTagOffset == INDEX_NONE ? nullptr : reinterpret_cast<const int32*>(ParticleMeshRenderData.SourceParticleData->GetComponentPtrInt32(RendererVisTagOffset));
				const int32* RESTRICT MeshIndexData = MeshIndexOffset == INDEX_NONE ? nullptr : reinterpret_cast<const int32*>(ParticleMeshRenderData.SourceParticleData->GetComponentPtrInt32(MeshIndexOffset));

				auto GetInstancePosition = [&PositionX, &PositionY, &PositionZ](int32 Idx)
				{
					return FVector(PositionX[Idx], PositionY[Idx], PositionZ[Idx]);
				};

				auto GetInstanceScale = [&ScaleX, &ScaleY, &ScaleZ](int32 Idx)
				{
					return FVector(ScaleX[Idx], ScaleY[Idx], ScaleZ[Idx]);
				};

				auto GetInstanceQuat = [&QuatArrayX, &QuatArrayY, &QuatArrayZ, &QuatArrayW](int32 Idx)
				{
					return FQuat(QuatArrayX[Idx], QuatArrayY[Idx], QuatArrayZ[Idx], QuatArrayW[Idx]);
				};

				//#dxr_todo: handle MESH_FACING_VELOCITY, MESH_FACING_CAMERA_POSITION, MESH_FACING_CAMERA_PLANE
				//#dxr_todo: handle half floats
				const bool bHasPosition = PositionBaseCompOffset > 0;
				const bool bHasRotation = TransformBaseCompOffset > 0;
				const bool bHasScale = ScaleBaseCompOffset > 0;

				const FMatrix NullInstanceTransform(FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector);
				for (uint32 InstanceIndex = 0; InstanceIndex < NumInstances; InstanceIndex++)
				{
					if ( RenderVisibilityData && (RenderVisibilityData[InstanceIndex] != RendererVisibility) )
					{
						RayTracingInstance.InstanceTransforms.Add(NullInstanceTransform);
						continue;
					}
					if ( MeshIndexData && (MeshIndexData[InstanceIndex] != MeshData.SourceMeshIndex) )
					{
						RayTracingInstance.InstanceTransforms.Add(NullInstanceTransform);
						continue;
					}

					const FVector InstancePosition = bHasPosition ? GetInstancePosition(InstanceIndex) : FVector::ZeroVector;
					const FQuat InstanceRotation = bHasRotation ? GetInstanceQuat(InstanceIndex).GetNormalized() : FQuat::Identity;
					FMatrix InstanceTransform = FQuatRotationTranslationMatrix::Make(InstanceRotation, InstancePosition);

					const FVector InstanceScale = bHasScale ? GetInstanceScale(InstanceIndex) * MeshData.Scale : MeshData.Scale;
					InstanceTransform = FScaleMatrix(InstanceScale) * InstanceTransform;

					if (bLocalSpace)
					{
						InstanceTransform = InstanceTransform * LocalTransform;
					}

					RayTracingInstance.InstanceTransforms.Add(InstanceTransform);
				}
			}
			// Gpu Target
			else if (FNiagaraUtilities::AllowComputeShaders(GShaderPlatformForFeatureLevel[FeatureLevel]) && FDataDrivenShaderPlatformInfo::GetSupportsRayTracingIndirectInstanceData(GShaderPlatformForFeatureLevel[FeatureLevel]) )
			{
				FRHICommandListImmediate& RHICmdList = Context.RHICmdList;

				RayTracingInstance.NumTransforms = NumInstances;

				FRWBufferStructured InstanceGPUTransformsBuffer;
				//InstanceGPUTransformsBuffer.Initialize(sizeof(FMatrix), NumInstances, BUF_Static);
				InstanceGPUTransformsBuffer.Initialize(3 * 4 * sizeof(float), NumInstances, BUF_Static);
				RayTracingInstance.InstanceGPUTransformsSRV = InstanceGPUTransformsBuffer.SRV;

				FNiagaraGPURayTracingTransformsCS::FParameters PassParameters;
				{
					PassParameters.ParticleDataFloatStride		= ParticleMeshRenderData.ParticleFloatDataStride;
					//PassParameters.ParticleDataHalfStride		= ParticleMeshRenderData.ParticleHalfDataStride;
					PassParameters.ParticleDataIntStride		= ParticleMeshRenderData.ParticleIntDataStride;
					PassParameters.CPUNumInstances				= NumInstances;
					PassParameters.InstanceCountOffset			= ParticleMeshRenderData.SourceParticleData->GetGPUInstanceCountBufferOffset();
					PassParameters.PositionDataOffset			= VFVariables[ENiagaraMeshVFLayout::Position].GetGPUOffset();
					PassParameters.RotationDataOffset			= VFVariables[ENiagaraMeshVFLayout::Rotation].GetGPUOffset();
					PassParameters.ScaleDataOffset				= VFVariables[ENiagaraMeshVFLayout::Scale].GetGPUOffset();
					PassParameters.bLocalSpace					= bLocalSpace ? 1 : 0;
					PassParameters.RenderVisibilityOffset		= RendererVisTagOffset;
					PassParameters.MeshIndexOffset				= MeshIndexOffset;
					PassParameters.RenderVisibilityValue		= RendererVisibility;
					PassParameters.MeshIndexValue				= MeshData.SourceMeshIndex;
					PassParameters.LocalTransform				= LocalTransform;
					PassParameters.DefaultPosition				= FVector::ZeroVector;
					PassParameters.DefaultRotation				= FVector4(0.0f, 0.0f, 0.0f, 1.0f);
					PassParameters.DefaultScale					= FVector(1.0f, 1.0f, 1.0f);
					PassParameters.MeshScale					= MeshData.Scale;
					PassParameters.ParticleDataFloatBuffer		= ParticleMeshRenderData.ParticleFloatSRV;
					//PassParameters.ParticleDataHalfBuffer		= ParticleMeshRenderData.ParticleHalfSRV;
					PassParameters.ParticleDataIntBuffer		= ParticleMeshRenderData.ParticleIntSRV;
					PassParameters.GPUInstanceCountBuffer		= Batcher->GetGPUInstanceCounterManager().GetInstanceCountBuffer().SRV;
					PassParameters.TLASTransforms				= InstanceGPUTransformsBuffer.UAV;
				}

				FNiagaraGPURayTracingTransformsCS::FPermutationDomain PermutationVector;
				TShaderMapRef<FNiagaraGPURayTracingTransformsCS> ComputeShader(GetGlobalShaderMap(FeatureLevel), PermutationVector);
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, PassParameters, FComputeShaderUtils::GetGroupCount(int32(NumInstances), int32(FNiagaraGPURayTracingTransformsCS::ThreadGroupSize)));
			
				RHICmdList.Transition(FRHITransitionInfo(InstanceGPUTransformsBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute));
			}
		}

		RayTracingInstance.BuildInstanceMaskAndFlags();
		OutRayTracingInstances.Add(RayTracingInstance);
	}
}
#endif


FNiagaraDynamicDataBase* FNiagaraRendererMeshes::GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGenMeshVertexData);

	const UNiagaraMeshRendererProperties* Properties = CastChecked<const UNiagaraMeshRendererProperties>(InProperties);

	FNiagaraDataBuffer* DataToRender = Emitter->GetData().GetCurrentData();
	if (!DataToRender || 
		Meshes.Num() == 0 ||
		(SourceMode == ENiagaraRendererSourceDataMode::Particles && DataToRender->GetNumInstances() == 0))
	{
		return nullptr;
	}

	// Bail if we have cached mesh render data for any meshes that are no longer valid
	for (const auto& MeshData : Meshes)
	{
		if (!Properties->Meshes.IsValidIndex(MeshData.SourceMeshIndex) || !Properties->Meshes[MeshData.SourceMeshIndex].Mesh)
		{
			return nullptr;
		}
	}

	FNiagaraDynamicDataMesh* DynamicData = new FNiagaraDynamicDataMesh(Emitter);
	DynamicData->SetMaterialRelevance(BaseMaterialRelevance_GT);

	DynamicData->Materials.Reset(BaseMaterials_GT.Num());
	for (UMaterialInterface* Mat : BaseMaterials_GT)
	{
		//In preparation for a material override feature, we pass our material(s) and relevance in via dynamic data.
		//The renderer ensures we have the correct usage and relevance for materials in BaseMaterials_GT.
		//Any override feature must also do the same for materials that are set.
		check(Mat->CheckMaterialUsage_Concurrent(MATUSAGE_NiagaraMeshParticles));
		DynamicData->Materials.Add(Mat->GetRenderProxy());
	}

	if (DynamicData)
	{
		const FNiagaraParameterStore& ParameterData = Emitter->GetRendererBoundVariables();
		DynamicData->DataInterfacesBound = ParameterData.GetDataInterfaces();
		DynamicData->ObjectsBound = ParameterData.GetUObjects();
		DynamicData->ParameterDataBound = ParameterData.GetParameterDataArray();
	}

	if (DynamicData && Properties->MaterialParameterBindings.Num() != 0)
	{
		ProcessMaterialParameterBindings(MakeArrayView(Properties->MaterialParameterBindings), Emitter, MakeArrayView(BaseMaterials_GT));
	}

	return DynamicData;
}

int FNiagaraRendererMeshes::GetDynamicDataSize()const
{
	uint32 Size = sizeof(FNiagaraDynamicDataMesh);
	return Size;
}

bool FNiagaraRendererMeshes::IsMaterialValid(const UMaterialInterface* Mat)const
{
	return Mat && Mat->CheckMaterialUsage_Concurrent(MATUSAGE_NiagaraMeshParticles);
}


//////////////////////////////////////////////////////////////////////////
// Proposed class for ensuring Niagara/Cascade components who's proxies reference render data of other objects (Materials, Meshes etc) do not have data freed from under them.
// Our components register themselves with the referenced component which then calls InvalidateRenderDependencies() whenever it's render data is changed or when it is destroyed.
// UNTESTED - DO NOT USE.
struct FComponentRenderDependencyHandler
{
	void AddDependency(UPrimitiveComponent* Component)
	{
		DependentComponents.Add(Component);
	}

	void RemoveDependancy(UPrimitiveComponent* Component)
	{
		DependentComponents.RemoveSwap(Component);
	}

	void InvalidateRenderDependencies()
	{
		int32 i = DependentComponents.Num();
		while (--i >= 0)
		{
			if (UPrimitiveComponent* Comp = DependentComponents[i].Get())
			{
				Comp->MarkRenderStateDirty();
			}
			else
			{
				DependentComponents.RemoveAtSwap(i);
			}
		}
	}

	TArray<TWeakObjectPtr<UPrimitiveComponent>> DependentComponents;
};

