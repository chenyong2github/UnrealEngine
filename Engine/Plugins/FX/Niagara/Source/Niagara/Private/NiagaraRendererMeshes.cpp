// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraRendererMeshes.h"
#include "ParticleResources.h"
#include "NiagaraDataSet.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraStats.h"
#include "NiagaraComponent.h"
#include "Async/ParallelFor.h"
#include "Engine/StaticMesh.h"
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

extern int32 GbEnableMinimalGPUBuffers;

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

void FNiagaraRendererMeshes::Initialize(const UNiagaraRendererProperties* InProps, const FNiagaraEmitterInstance* Emitter, const FNiagaraSystemInstanceController& InController)
{
	FNiagaraRenderer::Initialize(InProps, Emitter, InController);

	check(Emitter);
	check(InProps);

	const UNiagaraMeshRendererProperties* Properties = CastChecked<const UNiagaraMeshRendererProperties>(InProps);

	MaxSectionCount = 0;

	// Initialize the valid mesh slots, and prep them with the data for every mesh, LOD, and section we'll be needing over the lifetime of the renderer
	const uint32 MaxMeshes = Properties->Meshes.Num();
	Meshes.Empty(MaxMeshes);
	for (uint32 SourceMeshIndex = 0; SourceMeshIndex < MaxMeshes; ++SourceMeshIndex)
	{
		const auto& MeshProperties = Properties->Meshes[SourceMeshIndex];
		UStaticMesh* Mesh = MeshProperties.ResolveStaticMesh(Emitter);
		
		if (Mesh)
		{
			FMeshData& MeshData = Meshes.AddDefaulted_GetRef();
			MeshData.RenderData = Mesh->GetRenderData();
			MeshData.SourceMeshIndex = SourceMeshIndex;
			MeshData.PivotOffset = MeshProperties.PivotOffset;
			MeshData.PivotOffsetSpace = MeshProperties.PivotOffsetSpace;
			MeshData.Scale = MeshProperties.Scale;
			MeshData.MinimumLOD = Mesh->GetMinLOD().GetValue();

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
			FBox LocalBounds = Mesh->GetExtendedBounds().GetBox();
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

void FNiagaraRendererMeshes::PrepareParticleBuffers(
	FGlobalDynamicReadBuffer& DynamicReadBuffer,
	FNiagaraDataBuffer& SourceParticleData,
	const FNiagaraRendererLayout& RendererLayout,
	bool bDoGPUCulling,
	FParticleGPUBufferData& OutData,
	uint32& OutRendererVisTagOffset,
	uint32& OutMeshIndexOffset) const
{
	OutRendererVisTagOffset = RendererVisTagOffset;
	OutMeshIndexOffset = MeshIndexOffset;

	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		const uint32 NumInstances = SourceParticleData.GetNumInstances();

		// For cpu sims we allocate render buffers from the global pool. GPU sims own their own.
		if (GbEnableMinimalGPUBuffers)
		{
			OutData.FloatDataStride = NumInstances;
			OutData.HalfDataStride = NumInstances;

			FParticleRenderData ParticleFloatData = TransferDataToGPU(DynamicReadBuffer, &RendererLayout, &SourceParticleData);
			OutData.FloatSRV = ParticleFloatData.FloatData.IsValid() ? (FRHIShaderResourceView*)ParticleFloatData.FloatData.SRV : FNiagaraRenderer::GetDummyFloatBuffer();
			OutData.HalfSRV = ParticleFloatData.HalfData.IsValid() ? (FRHIShaderResourceView*)ParticleFloatData.HalfData.SRV : FNiagaraRenderer::GetDummyHalfBuffer();
		}
		else
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderMeshes_AllocateGPUData);

			int32 TotalFloatBytes = SourceParticleData.GetFloatBuffer().Num();
			int32 TotalFloatCount = TotalFloatBytes / sizeof(float);
			if (TotalFloatCount > 0)
			{
				FGlobalDynamicReadBuffer::FAllocation FloatData = DynamicReadBuffer.AllocateFloat(TotalFloatCount);
				FMemory::Memcpy(FloatData.Buffer, SourceParticleData.GetFloatBuffer().GetData(), TotalFloatBytes);
				OutData.FloatSRV = FloatData.SRV;
				OutData.FloatDataStride = SourceParticleData.GetFloatStride() / sizeof(float);
			}
			else
			{
				OutData.FloatSRV = FNiagaraRenderer::GetDummyFloatBuffer();
				OutData.FloatDataStride = 0;
			}

			int32 TotalHalfBytes = SourceParticleData.GetHalfBuffer().Num();
			int32 TotalHalfCount = TotalHalfBytes / sizeof(FFloat16);
			if (TotalHalfCount > 0)
			{
				FGlobalDynamicReadBuffer::FAllocation HalfData = DynamicReadBuffer.AllocateHalf(TotalHalfCount);
				FMemory::Memcpy(HalfData.Buffer, SourceParticleData.GetHalfBuffer().GetData(), TotalHalfBytes);
				OutData.HalfSRV = HalfData.SRV;
				OutData.HalfDataStride = SourceParticleData.GetHalfStride() / sizeof(FFloat16);
			}
			else
			{
				OutData.HalfSRV = FNiagaraRenderer::GetDummyHalfBuffer();
				OutData.HalfDataStride = 0;
			}
		}

		// For CPU sims, we need to copy off any integer attributes needed for culling in the sort shader
		uint32 NumIntParams = 0;
		if (bDoGPUCulling)
		{
			if (RendererVisTagOffset != INDEX_NONE)
			{
				++NumIntParams;
			}
			if (MeshIndexOffset != INDEX_NONE)
			{
				++NumIntParams;
			}
		}

		if (NumIntParams > 0)
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderMeshes_AllocateGPUData);

			FGlobalDynamicReadBuffer::FAllocation ParticleIntData = DynamicReadBuffer.AllocateInt32(NumInstances * NumIntParams);
			int32* Dest = (int32*)ParticleIntData.Buffer;
			const int32* Src = (const int32*)SourceParticleData.GetInt32Buffer().GetData();
			const uint32 SrcIntStride = SourceParticleData.GetInt32Stride() / sizeof(uint32);

			uint32 CurDestOffset = 0;
			if (RendererVisTagOffset != INDEX_NONE)
			{
				for (uint32 InstIdx = 0; InstIdx < NumInstances; ++InstIdx)
				{
					Dest[InstIdx] = Src[RendererVisTagOffset * SrcIntStride + InstIdx];
				}

				OutRendererVisTagOffset = CurDestOffset++;
				Dest += NumInstances;
			}

			if (MeshIndexOffset != INDEX_NONE)
			{
				for (uint32 InstIdx = 0; InstIdx < NumInstances; ++InstIdx)
				{
					Dest[InstIdx] = Src[MeshIndexOffset * SrcIntStride + InstIdx];
				}

				OutMeshIndexOffset = CurDestOffset++;
				Dest += NumInstances;
			}

			OutData.IntSRV = ParticleIntData.SRV;
			OutData.IntDataStride = NumInstances;
		}
		else
		{
			OutData.IntSRV = FNiagaraRenderer::GetDummyIntBuffer();
			OutData.IntDataStride = 0;
		}
	}
	else
	{
		OutData.FloatSRV = SourceParticleData.GetGPUBufferFloat().SRV.IsValid() ? (FRHIShaderResourceView*)SourceParticleData.GetGPUBufferFloat().SRV : FNiagaraRenderer::GetDummyFloatBuffer();
		OutData.HalfSRV = SourceParticleData.GetGPUBufferHalf().SRV.IsValid() ? (FRHIShaderResourceView*)SourceParticleData.GetGPUBufferHalf().SRV : FNiagaraRenderer::GetDummyHalfBuffer();
		OutData.IntSRV = SourceParticleData.GetGPUBufferInt().SRV.IsValid() ? (FRHIShaderResourceView*)SourceParticleData.GetGPUBufferInt().SRV : FNiagaraRenderer::GetDummyIntBuffer();
		OutData.FloatDataStride = SourceParticleData.GetFloatStride() / sizeof(float);
		OutData.HalfDataStride = SourceParticleData.GetHalfStride() / sizeof(FFloat16);
		OutData.IntDataStride = SourceParticleData.GetInt32Stride() / sizeof(int32);
	}
}

FNiagaraMeshUniformBufferRef FNiagaraRendererMeshes::CreatePerViewUniformBuffer(
	const FMeshData& MeshData,
	const FNiagaraSceneProxy& SceneProxy,
	const FNiagaraRendererLayout& RendererLayout,
	const FSceneView& View,
	const FParticleGPUBufferData& BufferData,
	const FNiagaraDynamicDataMesh* DynamicDataMesh,
	FVector& OutWorldSpacePivotOffset,
	FSphere& OutCullingSphere) const
{
	OutWorldSpacePivotOffset = FVector(0, 0, 0);
	OutCullingSphere = MeshData.LocalCullingSphere;

	// Compute the per-view uniform buffers.
	FNiagaraMeshUniformParameters PerViewUniformParameters;
	FMemory::Memzero(&PerViewUniformParameters, sizeof(PerViewUniformParameters)); // Clear unset bytes

	PerViewUniformParameters.bLocalSpace = bLocalSpace;
	PerViewUniformParameters.DeltaSeconds = View.Family->DeltaWorldTime;
	PerViewUniformParameters.MeshScale = MeshData.Scale;

	// Calculate pivot offset
	if (MeshData.PivotOffsetSpace == ENiagaraMeshPivotOffsetSpace::Mesh)
	{
		OutCullingSphere.Center += MeshData.PivotOffset;

		PerViewUniformParameters.PivotOffset = MeshData.PivotOffset;
		PerViewUniformParameters.bPivotOffsetIsWorldSpace = false;
	}
	else
	{
		OutWorldSpacePivotOffset = MeshData.PivotOffset;
		if (MeshData.PivotOffsetSpace == ENiagaraMeshPivotOffsetSpace::Local ||
			(bLocalSpace && MeshData.PivotOffsetSpace == ENiagaraMeshPivotOffsetSpace::Simulation))
		{
			// The offset is in local space, transform it to world
			OutWorldSpacePivotOffset = SceneProxy.GetLocalToWorld().TransformVector(OutWorldSpacePivotOffset);
		}

		PerViewUniformParameters.PivotOffset = OutWorldSpacePivotOffset;
		PerViewUniformParameters.bPivotOffsetIsWorldSpace = true;
	}

	PerViewUniformParameters.MaterialParamValidMask = MaterialParamValidMask;
	PerViewUniformParameters.SubImageSize = FVector4(SubImageSize.X, SubImageSize.Y, 1.0f / SubImageSize.X, 1.0f / SubImageSize.Y);
	PerViewUniformParameters.SubImageBlendMode = bSubImageBlend;
	PerViewUniformParameters.FacingMode = (uint32)FacingMode;
	PerViewUniformParameters.bLockedAxisEnable = bLockedAxisEnable;
	PerViewUniformParameters.LockedAxis = LockedAxis;
	PerViewUniformParameters.LockedAxisSpace = (uint32)LockedAxisSpace;
	PerViewUniformParameters.NiagaraFloatDataStride = BufferData.FloatDataStride;
	PerViewUniformParameters.NiagaraParticleDataFloat = BufferData.FloatSRV;
	PerViewUniformParameters.NiagaraParticleDataHalf = BufferData.HalfSRV;

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

	TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = RendererLayout.GetVFVariables_RenderThread();
	if (SourceMode == ENiagaraRendererSourceDataMode::Particles)
	{
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

	if (bSetAnyBoundVars && DynamicDataMesh)
	{
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

void FNiagaraRendererMeshes::InitializeSortInfo(
	const FNiagaraDataBuffer& SourceParticleData,
	const FNiagaraSceneProxy& SceneProxy,
	const FNiagaraRendererLayout& RendererLayout,
	const FParticleGPUBufferData& BufferData,
	const FSceneView& View,
	int32 ViewIndex,
	bool bHasTranslucentMaterials,
	bool bIsInstancedStereo,
	bool bDoGPUCulling,
	int32 SortVarIdx,
	uint32 VisTagOffset,
	uint32 MeshIdxOffset,
	FNiagaraGPUSortInfo& OutSortInfo) const
{
	NiagaraEmitterInstanceBatcher* Batcher = SceneProxy.GetBatcher();
	check(Batcher);

	TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = RendererLayout.GetVFVariables_RenderThread();

	FShaderResourceViewRHIRef GPUCountBufferSRV = Batcher->GetGPUInstanceCounterManager().GetInstanceCountBuffer().SRV;
	if (!GPUCountBufferSRV.IsValid())
	{
		GPUCountBufferSRV = FNiagaraRenderer::GetDummyUIntBuffer();
	}

	OutSortInfo.ParticleCount = SourceParticleData.GetNumInstances();
	OutSortInfo.SortMode = SortMode;
	OutSortInfo.SetSortFlags(GNiagaraGPUSortingUseMaxPrecision != 0, bHasTranslucentMaterials);
	OutSortInfo.bEnableCulling = bDoGPUCulling;
	OutSortInfo.RendererVisTagAttributeOffset = RendererVisTagOffset;
	OutSortInfo.RendererVisibility = RendererVisibility;
	OutSortInfo.DistanceCullRange = DistanceCullRange;
	OutSortInfo.ParticleDataFloatSRV = BufferData.FloatSRV;
	OutSortInfo.ParticleDataHalfSRV = BufferData.HalfSRV;
	OutSortInfo.ParticleDataIntSRV = BufferData.IntSRV;
	OutSortInfo.FloatDataStride = BufferData.FloatDataStride;
	OutSortInfo.HalfDataStride = BufferData.HalfDataStride;
	OutSortInfo.IntDataStride = BufferData.IntDataStride;
	OutSortInfo.GPUParticleCountSRV = GPUCountBufferSRV;
	OutSortInfo.GPUParticleCountOffset = SourceParticleData.GetGPUInstanceCountBufferOffset();
	OutSortInfo.RendererVisTagAttributeOffset = VisTagOffset;
	OutSortInfo.MeshIndexAttributeOffset = MeshIdxOffset;
	OutSortInfo.SortAttributeOffset = VFVariables[SortVarIdx].GetGPUOffset();

	auto GetViewMatrices = [](const FSceneView& View, FVector& OutViewOrigin) -> const FViewMatrices&
	{
		OutViewOrigin = View.ViewLocation;

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
				FPlane CullPlane;
				GetViewMatrices(*RightEyeView, RightEyePos).GetViewProjectionMatrix().GetFrustumRightPlane(CullPlane);
				OutSortInfo.CullPlanes[5] = CullPlane;	// LWC_TODO: Perf pessimization
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

	if (bDoGPUCulling)
	{
		OutSortInfo.CullPositionAttributeOffset = VFVariables[ENiagaraMeshVFLayout::Position].GetGPUOffset();
		OutSortInfo.CullOrientationAttributeOffset = VFVariables[ENiagaraMeshVFLayout::Rotation].GetGPUOffset();
		OutSortInfo.CullScaleAttributeOffset = VFVariables[ENiagaraMeshVFLayout::Scale].GetGPUOffset();
	}
}

void FNiagaraRendererMeshes::CreateMeshBatchForSection(
	FMeshElementCollector& Collector,
	FVertexFactory& VertexFactory,
	FMaterialRenderProxy& MaterialProxy,
	const FNiagaraSceneProxy& SceneProxy,
	const FStaticMeshLODResources& LODModel,
	const FStaticMeshSection& Section,
	const FSceneView& View,
	int32 ViewIndex,
	uint32 NumInstances,
	uint32 GPUCountBufferOffset,
	bool bIsWireframe,
	bool bIsInstancedStereo,
	bool bDoGPUCulling) const
{
	if (Section.NumTriangles == 0)
	{
		return;
	}

	FMeshBatch& Mesh = Collector.AllocateMesh();
	Mesh.VertexFactory = &VertexFactory;
	Mesh.LCI = NULL;
	Mesh.ReverseCulling = SceneProxy.IsLocalToWorldDeterminantNegative();
	Mesh.CastShadow = SceneProxy.CastsDynamicShadow();
#if RHI_RAYTRACING
	Mesh.CastRayTracedShadow = SceneProxy.CastsDynamicShadow();
#endif
	Mesh.DepthPriorityGroup = (ESceneDepthPriorityGroup)SceneProxy.GetDepthPriorityGroup(&View);

	FMeshBatchElement& BatchElement = Mesh.Elements[0];
	BatchElement.PrimitiveUniformBuffer = IsMotionBlurEnabled() ? SceneProxy.GetUniformBuffer() : SceneProxy.GetUniformBufferNoVelocity();
	BatchElement.FirstIndex = 0;
	BatchElement.MinVertexIndex = 0;
	BatchElement.MaxVertexIndex = 0;
	BatchElement.NumInstances = NumInstances;

	if (bIsWireframe)
	{
		if (LODModel.AdditionalIndexBuffers && LODModel.AdditionalIndexBuffers->WireframeIndexBuffer.IsInitialized())
		{
			Mesh.Type = PT_LineList;
			Mesh.MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
			BatchElement.FirstIndex = 0;
			BatchElement.IndexBuffer = &LODModel.AdditionalIndexBuffers->WireframeIndexBuffer;
			BatchElement.NumPrimitives = LODModel.AdditionalIndexBuffers->WireframeIndexBuffer.GetNumIndices() / 2;
		}
		else
		{
			Mesh.Type = PT_TriangleList;
			Mesh.MaterialRenderProxy = &MaterialProxy;
			Mesh.bWireframe = true;
			BatchElement.FirstIndex = 0;
			BatchElement.IndexBuffer = &LODModel.IndexBuffer;
			BatchElement.NumPrimitives = LODModel.IndexBuffer.GetNumIndices() / 3;
		}
	}
	else
	{
		Mesh.Type = PT_TriangleList;
		Mesh.MaterialRenderProxy = &MaterialProxy;
		BatchElement.IndexBuffer = &LODModel.IndexBuffer;
		BatchElement.FirstIndex = Section.FirstIndex;
		BatchElement.NumPrimitives = Section.NumTriangles;
	}

	if (bDoGPUCulling || (SimTarget == ENiagaraSimTarget::GPUComputeSim && SourceMode == ENiagaraRendererSourceDataMode::Particles))
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

	Mesh.bCanApplyViewModeOverrides = true;
	Mesh.bUseWireframeSelectionColoring = SceneProxy.IsSelected();

	Collector.AddMesh(ViewIndex, Mesh);
}

void FNiagaraRendererMeshes::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy* SceneProxy) const
{
	check(SceneProxy);

	PARTICLE_PERF_STAT_CYCLES_RT(SceneProxy->PerfStatsContext, GetDynamicMeshElements);

	NiagaraEmitterInstanceBatcher* Batcher = SceneProxy->GetBatcher();
	FNiagaraDynamicDataMesh* DynamicDataMesh = (static_cast<FNiagaraDynamicDataMesh*>(DynamicDataRender));
	if (!DynamicDataMesh || !Batcher)
	{
		return;
	}

	FNiagaraDataBuffer* SourceParticleData = DynamicDataMesh->GetParticleDataToRender();
	if (SourceParticleData == nullptr ||
		(SourceMode == ENiagaraRendererSourceDataMode::Particles && SourceParticleData->GetNumInstances() == 0) ||
		Meshes.Num() == 0 ||
		GbEnableNiagaraMeshRendering == 0 ||
		!GSupportsResourceView  // Current shader requires SRV to draw properly in all cases.
		)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderMeshes);

#if STATS
	FScopeCycleCounter EmitterStatsCounter(EmitterStatID);
#endif

	const int32 NumInstances = SourceMode == ENiagaraRendererSourceDataMode::Particles ? SourceParticleData->GetNumInstances() : 1;

	// Grab the material proxies we'll be using for each section and check them for translucency.
	bool bHasTranslucentMaterials = false;
	for (FMaterialRenderProxy* MaterialProxy : DynamicDataMesh->Materials)
	{
		check(MaterialProxy);
		EBlendMode BlendMode = MaterialProxy->GetIncompleteMaterialWithFallback(FeatureLevel).GetBlendMode();
		bHasTranslucentMaterials |= IsTranslucentBlendMode(BlendMode);
	}

	// NOTE: have to run the GPU sort when culling is enabled if supported on this platform
	// TODO: implement culling and renderer visibility on the CPU for other platforms
	const bool bGPUSortEnabled = FNiagaraUtilities::AllowComputeShaders(Batcher->GetShaderPlatform());
	const bool bDoGPUCulling = SourceMode == ENiagaraRendererSourceDataMode::Particles && bEnableCulling && GNiagaraGPUCulling && FNiagaraUtilities::AllowComputeShaders(Batcher->GetShaderPlatform());
	const bool bShouldSort = SourceMode == ENiagaraRendererSourceDataMode::Particles && SortMode != ENiagaraSortMode::None && (bHasTranslucentMaterials || !bSortOnlyWhenTranslucent);
	const bool bCustomSorting = SortMode == ENiagaraSortMode::CustomAscending || SortMode == ENiagaraSortMode::CustomDecending;

	FGlobalDynamicReadBuffer& DynamicReadBuffer = Collector.GetDynamicReadBuffer();
	const FNiagaraRendererLayout* RendererLayout = bCustomSorting ? RendererLayoutWithCustomSorting : RendererLayoutWithoutCustomSorting;
	FParticleGPUBufferData BufferData;
	uint32 ActualRendererVisTagOffset;
	uint32 ActualMeshIndexOffset;
	PrepareParticleBuffers(DynamicReadBuffer, *SourceParticleData, *RendererLayout, bDoGPUCulling, BufferData, ActualRendererVisTagOffset, ActualMeshIndexOffset);

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
			int32 SortVarIdx = INDEX_NONE;
			if (bShouldSort || bDoGPUCulling)
			{
				SortVarIdx = bCustomSorting ? ENiagaraMeshVFLayout::CustomSorting : ENiagaraMeshVFLayout::Position;
				InitializeSortInfo(*SourceParticleData, *SceneProxy, *RendererLayout, BufferData, *View, ViewIndex, bHasTranslucentMaterials,
					bIsInstancedStereo, bDoGPUCulling, SortVarIdx, ActualRendererVisTagOffset, ActualMeshIndexOffset, SortInfo);
			}

			for (int32 MeshIndex = 0; MeshIndex < Meshes.Num(); ++MeshIndex)
			{
				if (MeshIndex > 0 && (MeshIndexOffset == INDEX_NONE || !bDoGPUCulling))
				{
					// We have no binding for the mesh index, or we can't run GPU culling. In either case, only render the first mesh in
					// the array for all particles, always
					break;
				}

				const FMeshData& MeshData = Meshes[MeshIndex];
				FVector WorldSpacePivotOffset;
				FSphere CullingSphere;
				FNiagaraMeshUniformBufferRef PerViewUniformBuffer = CreatePerViewUniformBuffer(MeshData, *SceneProxy, *RendererLayout, *View, BufferData, DynamicDataMesh,
					WorldSpacePivotOffset, CullingSphere);

				// @TODO : support multiple LOD
				const int32 LODIndex = GetLODIndex(MeshIndex);
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

				VertexFactory.SetUniformBuffer(PerViewUniformBuffer);
				CollectorResources->UniformBuffer = PerViewUniformBuffer;

				// Sort/Cull particles if needed.
				VertexFactory.SetSortedIndices(nullptr, 0xFFFFFFFF);
				if ((bShouldSort || bDoGPUCulling) && SortInfo.SortAttributeOffset != INDEX_NONE)
				{
					// Set up mesh-specific sorting parameters
					SortInfo.CulledGPUParticleCountOffset = bDoGPUCulling ? Batcher->GetGPUInstanceCounterManager().AcquireCulledEntry() : INDEX_NONE;
					SortInfo.LocalBSphere = CullingSphere;
					SortInfo.CullingWorldSpaceOffset = WorldSpacePivotOffset;
					SortInfo.MeshIndex = MeshData.SourceMeshIndex;

					const int32 CPUThreshold = GNiagaraGPUSortingCPUToGPUThreshold;
					if (SimTarget == ENiagaraSimTarget::GPUComputeSim ||
						(bGPUSortEnabled && CPUThreshold >= 0 && NumInstances > CPUThreshold) ||
						bDoGPUCulling)
					{
						// We need to run the sort shader on the GPU
						if (Batcher->AddSortedGPUSimulation(SortInfo))
						{
							VertexFactory.SetSortedIndices(SortInfo.AllocationInfo.BufferSRV, SortInfo.AllocationInfo.BufferOffset);
						}
					}
					else
					{
						// We want to sort on CPU
						TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = RendererLayout->GetVFVariables_RenderThread();
						FGlobalDynamicReadBuffer::FAllocation SortedIndices;
						SortedIndices = DynamicReadBuffer.AllocateInt32(NumInstances);
						SortIndices(SortInfo, VFVariables[SortVarIdx], *SourceParticleData, SortedIndices);
						VertexFactory.SetSortedIndices(SortedIndices.SRV, 0);
					}
				}

				// Increment stats
				INC_DWORD_STAT_BY(STAT_NiagaraNumMeshVerts, NumInstances * LODModel.GetNumVertices());
				INC_DWORD_STAT_BY(STAT_NiagaraNumMeshes, NumInstances);

				const bool bIsWireframe = AllowDebugViewmodes() && View && ViewFamily.EngineShowFlags.Wireframe;
				for (int32 SectionIndex = 0; SectionIndex < SectionCount; SectionIndex++)
				{
					const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
					if (Section.NumTriangles == 0)
					{
						continue;
					}

					const uint32 RemappedMaterialIndex = MeshData.MaterialRemapTable[Section.MaterialIndex];
					if (!DynamicDataMesh->Materials.IsValidIndex(RemappedMaterialIndex))
					{
						// This should never occur. Otherwise, the section data changed since initialization
						continue;
					}

					FMaterialRenderProxy* MaterialProxy = DynamicDataMesh->Materials[RemappedMaterialIndex];
					if (MaterialProxy == nullptr)
					{
						//@todo. This should never occur, but it does occasionally.
						continue;
					}

					const uint32 GPUCountBufferOffset = bDoGPUCulling ? SortInfo.CulledGPUParticleCountOffset : SourceParticleData->GetGPUInstanceCountBufferOffset();
					CreateMeshBatchForSection(Collector, VertexFactory, *MaterialProxy, *SceneProxy, LODModel, Section, *View, ViewIndex, NumInstances,
						GPUCountBufferOffset, bIsWireframe, bIsInstancedStereo, bDoGPUCulling);
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

	NiagaraEmitterInstanceBatcher* Batcher = SceneProxy->GetBatcher();
	FNiagaraDynamicDataMesh* DynamicDataMesh = (static_cast<FNiagaraDynamicDataMesh*>(DynamicDataRender));
	if (!DynamicDataMesh || !Batcher)
	{
		return;
	}

	FNiagaraDataBuffer* SourceParticleData = DynamicDataMesh->GetParticleDataToRender();
	if (SourceParticleData == nullptr ||
		(SourceMode == ENiagaraRendererSourceDataMode::Particles && SourceParticleData->GetNumInstancesAllocated() == 0) ||
		(SourceMode == ENiagaraRendererSourceDataMode::Particles && SourceParticleData->GetNumInstances() == 0) ||
		Meshes.Num() == 0 ||
		GbEnableNiagaraMeshRendering == 0 ||
		!GSupportsResourceView  // Current shader requires SRV to draw properly in all cases.
		)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderMeshes);

	for (int32 MeshIndex = 0; MeshIndex < Meshes.Num(); ++MeshIndex)
	{
		const FMeshData& MeshData = Meshes[MeshIndex];
		const int32 LODIndex = GetLODIndex(MeshIndex);

		FVertexFactory* VertexFactory = &MeshData.RenderData->LODVertexFactories[LODIndex].VertexFactory;
		if (!VertexFactory->GetType()->SupportsRayTracingDynamicGeometry())
		{
			continue;
		}

		const FStaticMeshLODResources& LODModel = MeshData.RenderData->LODResources[LODIndex];
		FRayTracingGeometry& Geometry = MeshData.RenderData->LODResources[LODIndex].RayTracingGeometry;
		FRayTracingInstance RayTracingInstance;
		RayTracingInstance.Geometry = &Geometry;

		for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
		{
			const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
			if (Section.NumTriangles == 0)
			{
				continue;
			}

			uint32 RemappedMaterialIndex = MeshData.MaterialRemapTable[Section.MaterialIndex];
			if (!DynamicDataMesh->Materials.IsValidIndex(RemappedMaterialIndex))
			{
				// This should never occur. Otherwise, the section data changed since initialization
				continue;
			}

			FMaterialRenderProxy* MaterialProxy = DynamicDataMesh->Materials[RemappedMaterialIndex];
			if (MaterialProxy == nullptr)
			{
				continue;
			}

			FMeshBatch MeshBatch;
			const FStaticMeshLODResources& LOD = MeshData.RenderData->LODResources[LODIndex];
			const FStaticMeshVertexFactories& VFs = MeshData.RenderData->LODVertexFactories[LODIndex];

			MeshBatch.VertexFactory = VertexFactory;
			MeshBatch.MaterialRenderProxy = MaterialProxy;
			MeshBatch.SegmentIndex = SectionIndex;
			MeshBatch.LODIndex = LODIndex;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			MeshBatch.VisualizeLODIndex = LODIndex;
#endif
			MeshBatch.CastShadow = SceneProxy->CastsDynamicShadow();
			MeshBatch.CastRayTracedShadow = SceneProxy->CastsDynamicShadow();

			FMeshBatchElement& MeshBatchElement = MeshBatch.Elements[0];
			MeshBatchElement.VertexFactoryUserData = VFs.VertexFactory.GetUniformBuffer();
			MeshBatchElement.MinVertexIndex = Section.MinVertexIndex;
			MeshBatchElement.MaxVertexIndex = Section.MaxVertexIndex;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			MeshBatchElement.VisualizeElementIndex = SectionIndex;
#endif
			RayTracingInstance.Materials.Add(MeshBatch);
		}

		if (RayTracingInstance.Materials.Num() == 0 || LODModel.Sections.Num() != RayTracingInstance.Materials.Num())
		{
			continue;
		}

		const FNiagaraRendererLayout* RendererLayout = RendererLayoutWithCustomSorting;
		TConstArrayView<FNiagaraRendererVariableInfo> VFVariables = RendererLayout->GetVFVariables_RenderThread();
		const int32 NumInstances = SourceMode == ENiagaraRendererSourceDataMode::Particles ? SourceParticleData->GetNumInstances() : 1;
		const int32 TotalFloatSize = RendererLayout->GetTotalFloatComponents_RenderThread() * SourceParticleData->GetNumInstances();
		const int32 ComponentStrideDest = SourceParticleData->GetNumInstances() * sizeof(float);

		//ENiagaraMeshVFLayout::Transform just contains a Quat, not the whole transform
		const FNiagaraRendererVariableInfo& VarPositionInfo = VFVariables[ENiagaraMeshVFLayout::Position];
		const FNiagaraRendererVariableInfo& VarScaleInfo = VFVariables[ENiagaraMeshVFLayout::Scale];
		const FNiagaraRendererVariableInfo& VarTransformInfo = VFVariables[ENiagaraMeshVFLayout::Rotation];

		int32 PositionBaseCompOffset = VarPositionInfo.DatasetOffset;
		int32 ScaleBaseCompOffset = VarScaleInfo.DatasetOffset;
		int32 TransformBaseCompOffset = VarTransformInfo.DatasetOffset;

		float* RESTRICT PositionX = (float*)SourceParticleData->GetComponentPtrFloat(PositionBaseCompOffset);
		float* RESTRICT PositionY = (float*)SourceParticleData->GetComponentPtrFloat(PositionBaseCompOffset + 1);
		float* RESTRICT PositionZ = (float*)SourceParticleData->GetComponentPtrFloat(PositionBaseCompOffset + 2);

		float* RESTRICT ScaleX = (float*)SourceParticleData->GetComponentPtrFloat(ScaleBaseCompOffset);
		float* RESTRICT ScaleY = (float*)SourceParticleData->GetComponentPtrFloat(ScaleBaseCompOffset + 1);
		float* RESTRICT ScaleZ = (float*)SourceParticleData->GetComponentPtrFloat(ScaleBaseCompOffset + 2);

		float* RESTRICT QuatArrayX = (float*)SourceParticleData->GetComponentPtrFloat(TransformBaseCompOffset);
		float* RESTRICT QuatArrayY = (float*)SourceParticleData->GetComponentPtrFloat(TransformBaseCompOffset + 1);
		float* RESTRICT QuatArrayZ = (float*)SourceParticleData->GetComponentPtrFloat(TransformBaseCompOffset + 2);
		float* RESTRICT QuatArrayW = (float*)SourceParticleData->GetComponentPtrFloat(TransformBaseCompOffset + 3);

		FMatrix LocalTransform(SceneProxy->GetLocalToWorld());

		if (SourceMode == ENiagaraRendererSourceDataMode::Emitter)
		{
			FVector Pos = bLocalSpace ? FVector() : LocalTransform.GetOrigin();
			FVector Scale { 1.0f, 1.0f, 1.0f };
			FQuat Rot = FQuat::Identity;

			if (bSetAnyBoundVars)
			{
				const uint8* ParameterBoundData = DynamicDataMesh->ParameterDataBound.GetData();
				if (VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Position] != INDEX_NONE
					&& DynamicDataMesh->ParameterDataBound.IsValidIndex(VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Position]))
				{
					FMemory::Memcpy(&Pos, ParameterBoundData + VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Position], sizeof(FVector));
				}
				if (VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Scale] != INDEX_NONE
					&& DynamicDataMesh->ParameterDataBound.IsValidIndex(VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Scale]))
				{
					FMemory::Memcpy(&Scale, ParameterBoundData + VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Scale], sizeof(FVector));
				}
				if (VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Rotation] != INDEX_NONE
					&& DynamicDataMesh->ParameterDataBound.IsValidIndex(VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Rotation]))
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
		auto GetInstancePosition = [&PositionX, &PositionY, &PositionZ](int32 Idx)
		{
			return FVector4(PositionX[Idx], PositionY[Idx], PositionZ[Idx], 1);
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
		bool bHasPosition = PositionBaseCompOffset > 0;
		bool bHasRotation = TransformBaseCompOffset > 0;
		bool bHasScale = ScaleBaseCompOffset > 0;

		for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; InstanceIndex++)
		{
			FMatrix InstanceTransform(FMatrix::Identity);

			if (SimTarget == ENiagaraSimTarget::CPUSim)
			{
				FVector4 InstancePos = bHasPosition ? GetInstancePosition(InstanceIndex) : FVector4(0, 0, 0, 0);

				FVector4 Transform1 = FVector4(1.0f, 0.0f, 0.0f, InstancePos.X);
				FVector4 Transform2 = FVector4(0.0f, 1.0f, 0.0f, InstancePos.Y);
				FVector4 Transform3 = FVector4(0.0f, 0.0f, 1.0f, InstancePos.Z);

				if (bHasRotation)
				{
					FQuat InstanceQuat = GetInstanceQuat(InstanceIndex);
					FTransform RotationTransform(InstanceQuat.GetNormalized());
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
				}

				FMatrix ScaleMatrix(FMatrix::Identity);
				if (bHasScale)
				{
						FVector InstanceScale(GetInstanceScale(InstanceIndex));
						ScaleMatrix.M[0][0] *= InstanceScale.X;
						ScaleMatrix.M[1][1] *= InstanceScale.Y;
						ScaleMatrix.M[2][2] *= InstanceScale.Z;
				}

				InstanceTransform = FMatrix(FPlane(Transform1), FPlane(Transform2), FPlane(Transform3), FPlane(0.0, 0.0, 0.0, 1.0));
				InstanceTransform = InstanceTransform * ScaleMatrix;
				InstanceTransform = InstanceTransform.GetTransposed();

				if (bLocalSpace)
				{
					InstanceTransform = InstanceTransform * LocalTransform;
				}
			}
			else
			{
				// Indirect instancing dispatching: transforms are not available at this point but computed in GPU instead
				// Set invalid transforms so ray tracing ignores them. Valid transforms will be set later directly in the GPU
				FMatrix ScaleTransform = FMatrix::Identity;
				ScaleTransform.M[0][0] = 0.0;
				ScaleTransform.M[1][1] = 0.0;
				ScaleTransform.M[2][2] = 0.0;

				InstanceTransform = ScaleTransform * InstanceTransform;
			}

			RayTracingInstance.InstanceTransforms.Add(InstanceTransform);
		}

		// Set indirect transforms for GPU instances
		if (SimTarget == ENiagaraSimTarget::GPUComputeSim
			&& FNiagaraUtilities::AllowComputeShaders(GShaderPlatformForFeatureLevel[FeatureLevel])
			&& FDataDrivenShaderPlatformInfo::GetSupportsRayTracingIndirectInstanceData(GShaderPlatformForFeatureLevel[FeatureLevel])
			)
		{
			FRHICommandListImmediate& RHICmdList = Context.RHICmdList;

			uint32 CPUInstancesCount = SourceParticleData->GetNumInstances();

			RayTracingInstance.NumTransforms = CPUInstancesCount;

			FRWBufferStructured InstanceGPUTransformsBuffer;
			//InstanceGPUTransformsBuffer.Initialize(sizeof(FMatrix), CPUInstancesCount, BUF_Static);
				InstanceGPUTransformsBuffer.Initialize(TEXT("InstanceGPUTransformsBuffer"), 3 * 4 * sizeof(float), CPUInstancesCount, BUF_Static);
			RayTracingInstance.InstanceGPUTransformsSRV = InstanceGPUTransformsBuffer.SRV;

			FNiagaraGPURayTracingTransformsCS::FPermutationDomain PermutationVector;

			TShaderMapRef<FNiagaraGPURayTracingTransformsCS> GPURayTracingTransformsCS(GetGlobalShaderMap(FeatureLevel), PermutationVector);
			RHICmdList.SetComputeShader(GPURayTracingTransformsCS.GetComputeShader());

			const FUintVector4 NiagaraOffsets(
				VFVariables[ENiagaraMeshVFLayout::Position].GetGPUOffset(),
				VFVariables[ENiagaraMeshVFLayout::Rotation].GetGPUOffset(),
				VFVariables[ENiagaraMeshVFLayout::Scale].GetGPUOffset(),
				bLocalSpace ? 1 : 0);

			uint32 FloatDataOffset = 0;
			uint32 FloatDataStride = SourceParticleData->GetFloatStride() / sizeof(float);

			GPURayTracingTransformsCS->SetParameters(
				RHICmdList,
				CPUInstancesCount,
				SourceParticleData->GetGPUBufferFloat().SRV,
				FloatDataOffset,
				FloatDataStride,
				SourceParticleData->GetGPUInstanceCountBufferOffset(),
				Batcher->GetGPUInstanceCounterManager().GetInstanceCountBuffer().SRV,
				NiagaraOffsets,
				LocalTransform,
				InstanceGPUTransformsBuffer.UAV);

			uint32 NGroups = FMath::DivideAndRoundUp(CPUInstancesCount, FNiagaraGPURayTracingTransformsCS::ThreadGroupSize);
			DispatchComputeShader(RHICmdList, GPURayTracingTransformsCS, NGroups, 1, 1);
			GPURayTracingTransformsCS->UnbindBuffers(RHICmdList);

			RHICmdList.Transition(FRHITransitionInfo(InstanceGPUTransformsBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute));
		}

		RayTracingInstance.BuildInstanceMaskAndFlags(FeatureLevel);
		OutRayTracingInstances.Add(RayTracingInstance);
	}
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
		if (!Properties->Meshes.IsValidIndex(MeshData.SourceMeshIndex) || 
			!Properties->Meshes[MeshData.SourceMeshIndex].HasValidMeshProperties())
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

//////////////////////////////////////////////////////////////////////////