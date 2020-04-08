// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraRendererMeshes.h"
#include "ParticleResources.h"
#include "NiagaraMeshVertexFactory.h"
#include "NiagaraDataSet.h"
#include "NiagaraStats.h"
#include "Async/ParallelFor.h"
#include "Engine/StaticMesh.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraSortingGPU.h"
#include "NiagaraGPURayTracingTransformsShader.h"
#include "RayTracingDefinitions.h"
#include "RayTracingDynamicGeometryCollection.h"
#include "RayTracingInstance.h"

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
	TEXT("r.RayTracing.Niagara.Meshes"),
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
};

//////////////////////////////////////////////////////////////////////////

class FNiagaraMeshCollectorResourcesMesh : public FOneFrameResource
{
public:
	FNiagaraMeshVertexFactory* VertexFactory = nullptr;
	FNiagaraMeshUniformBufferRef UniformBuffer;

	virtual ~FNiagaraMeshCollectorResourcesMesh()
	{
		if (VertexFactory)
		{
			VertexFactory->SetSortedIndices(nullptr, 0xFFFFFFFF);
		}
	}
};

//////////////////////////////////////////////////////////////////////////

namespace ENiagaraMeshVFLayout
{
	enum Type
	{
		Position,
		Velocity,
		Color,
		Scale,
		Transform,
		MaterialRandom,
		NormalizedAge,
		CustomSorting,
		SubImage,
		DynamicParam0,
		DynamicParam1,
		DynamicParam2,
		DynamicParam3,
		CameraOffset,

		Num,
	};
};

FNiagaraRendererMeshes::FNiagaraRendererMeshes(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *Props, const FNiagaraEmitterInstance* Emitter)
	: FNiagaraRenderer(FeatureLevel, Props, Emitter)
	, MaterialParamValidMask(0)
{
	check(Emitter);
	check(Props);

	const UNiagaraMeshRendererProperties* Properties = CastChecked<const UNiagaraMeshRendererProperties>(Props);
	check(Properties->ParticleMesh);

	MeshRenderData = Properties->ParticleMesh->RenderData.Get();

	FacingMode = Properties->FacingMode;
	bLockedAxisEnable = Properties->bLockedAxisEnable;
	LockedAxis = Properties->LockedAxis;
	LockedAxisSpace = Properties->LockedAxisSpace;
	SortMode = Properties->SortMode;
	bSortOnlyWhenTranslucent = Properties->bSortOnlyWhenTranslucent;
	bOverrideMaterials = Properties->bOverrideMaterials;
	SubImageSize = Properties->SubImageSize;
	bSubImageBlend = Properties->bSubImageBlend;

	// Ensure valid value for the locked axis
	if (!LockedAxis.Normalize())
	{
		LockedAxis.Set(0.0f, 0.0f, 1.0f);
	}

	const FNiagaraDataSet& Data = Emitter->GetData();

	MaterialParamValidMask = 0;
	TotalVFHalfComponents = 0;
	TotalVFFloatComponents = 0;
	VFVariables.SetNum(ENiagaraMeshVFLayout::Num);
	SetVertexFactoryVariable(Data, Properties->PositionBinding.DataSetVariable, ENiagaraMeshVFLayout::Position);
	SetVertexFactoryVariable(Data, Properties->VelocityBinding.DataSetVariable, ENiagaraMeshVFLayout::Velocity);
	SetVertexFactoryVariable(Data, Properties->ColorBinding.DataSetVariable, ENiagaraMeshVFLayout::Color);
	SetVertexFactoryVariable(Data, Properties->ScaleBinding.DataSetVariable, ENiagaraMeshVFLayout::Scale);
	SetVertexFactoryVariable(Data, Properties->MeshOrientationBinding.DataSetVariable, ENiagaraMeshVFLayout::Transform);
	SetVertexFactoryVariable(Data, Properties->MaterialRandomBinding.DataSetVariable, ENiagaraMeshVFLayout::MaterialRandom);
	SetVertexFactoryVariable(Data, Properties->NormalizedAgeBinding.DataSetVariable, ENiagaraMeshVFLayout::NormalizedAge);
	SetVertexFactoryVariable(Data, Properties->CustomSortingBinding.DataSetVariable, ENiagaraMeshVFLayout::CustomSorting);
	SetVertexFactoryVariable(Data, Properties->SubImageIndexBinding.DataSetVariable, ENiagaraMeshVFLayout::SubImage);
	SetVertexFactoryVariable(Data, Properties->CameraOffsetBinding.DataSetVariable, ENiagaraMeshVFLayout::CameraOffset);
	MaterialParamValidMask |= SetVertexFactoryVariable(Data, Properties->DynamicMaterialBinding.DataSetVariable, ENiagaraMeshVFLayout::DynamicParam0) ? 0x1 : 0;
	MaterialParamValidMask |= SetVertexFactoryVariable(Data, Properties->DynamicMaterial1Binding.DataSetVariable, ENiagaraMeshVFLayout::DynamicParam1) ? 0x2 : 0;
	MaterialParamValidMask |= SetVertexFactoryVariable(Data, Properties->DynamicMaterial2Binding.DataSetVariable, ENiagaraMeshVFLayout::DynamicParam2) ? 0x4 : 0;
	MaterialParamValidMask |= SetVertexFactoryVariable(Data, Properties->DynamicMaterial3Binding.DataSetVariable, ENiagaraMeshVFLayout::DynamicParam3) ? 0x8 : 0;

	MeshMinimumLOD = Properties->ParticleMesh->MinLOD.GetValueForFeatureLevel(FeatureLevel);

	const int32 LODCount = MeshRenderData->LODResources.Num();
	IndexInfoPerSection.SetNum(LODCount);

	for (int32 LODIdx = 0; LODIdx < LODCount; ++LODIdx)
	{
		Properties->GetIndexInfoPerSection(LODIdx, IndexInfoPerSection[LODIdx]);
	}
}

FNiagaraRendererMeshes::~FNiagaraRendererMeshes()
{
	for (FNiagaraMeshVertexFactory* VertexFactory : VertexFactories)
	{
		delete VertexFactory;
	}
	VertexFactories.Empty();
}

void FNiagaraRendererMeshes::ReleaseRenderThreadResources()
{
	FNiagaraRenderer::ReleaseRenderThreadResources();
	for (FNiagaraMeshVertexFactory* VertexFactory : VertexFactories)
	{
		VertexFactory->ReleaseResource();
		delete VertexFactory;
	}
	VertexFactories.Empty();
}

int32 FNiagaraRendererMeshes::GetMaxIndirectArgs() const
{
	// currently the most indirect args we can add would be for a single lod, so search for the LOD with the highest number of sections
	// this value should be constant for the life of the renderer as it is being used for NumRegisteredGPURenderers
	int32 MaxSectionCount = 0;

	for (const auto& IndexInfo : IndexInfoPerSection)
	{
		MaxSectionCount = FMath::Max(MaxSectionCount, IndexInfo.Num());
	}
	return MaxSectionCount;
}

void FNiagaraRendererMeshes::SetupVertexFactory(FNiagaraMeshVertexFactory *InVertexFactory, const FStaticMeshLODResources& LODResources) const
{
	FStaticMeshDataType Data;

	LODResources.VertexBuffers.PositionVertexBuffer.BindPositionVertexBuffer(InVertexFactory, Data);
	LODResources.VertexBuffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(InVertexFactory, Data);
	LODResources.VertexBuffers.StaticMeshVertexBuffer.BindTexCoordVertexBuffer(InVertexFactory, Data, MAX_TEXCOORDS);
	LODResources.VertexBuffers.ColorVertexBuffer.BindColorVertexBuffer(InVertexFactory, Data);
	InVertexFactory->SetData(Data);
}

int32 FNiagaraRendererMeshes::GetLODIndex() const
{
	const int32 LODCount = MeshRenderData->LODResources.Num();

	// Doesn't seem to work for some reason. See comment in FDynamicMeshEmitterData::GetMeshLODIndexFromProxy()
	// const int32 LODIndex = FMath::Max<int32>((int32)MeshRenderData->CurrentFirstLODIdx, MeshMinimumLOD);
	int32 LODIndex = FMath::Clamp<int32>(MeshRenderData->CurrentFirstLODIdx, 0, LODCount - 1);

	while (LODIndex < LODCount && !MeshRenderData->LODResources[LODIndex].GetNumVertices())
	{
		++LODIndex;
	}

	check(MeshRenderData->LODResources[LODIndex].GetNumVertices());

	return LODIndex;
}

void FNiagaraRendererMeshes::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy *SceneProxy) const
{
	check(SceneProxy);

	SCOPE_CYCLE_COUNTER(STAT_NiagaraRender);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderMeshes);
	PARTICLE_PERF_STAT_CYCLES(SceneProxy->PerfAsset, GetDynamicMeshElements);

	NiagaraEmitterInstanceBatcher* Batcher = SceneProxy->GetBatcher();
	FNiagaraDynamicDataMesh *DynamicDataMesh = (static_cast<FNiagaraDynamicDataMesh*>(DynamicDataRender));
	if (!DynamicDataMesh || !Batcher)
	{
		return;
	}

	FNiagaraDataBuffer* SourceParticleData = DynamicDataMesh->GetParticleDataToRender();
	if (SourceParticleData == nullptr ||
		MeshRenderData == nullptr ||
		SourceParticleData->GetNumInstancesAllocated() == 0 ||
		SourceParticleData->GetNumInstances() == 0 ||
		GbEnableNiagaraMeshRendering == 0 ||
		!GSupportsResourceView  // Current shader requires SRV to draw properly in all cases.
		)
	{
		return;
	}

#if STATS
	FScopeCycleCounter EmitterStatsCounter(EmitterStatID);
#endif

	// @TODO : support multiple LOD
	TArray<uint32, TInlineAllocator<8>> IndirectArgsOffsets;
	int32 NumInstances = SourceParticleData->GetNumInstances();

	FGlobalDynamicReadBuffer& DynamicReadBuffer = Collector.GetDynamicReadBuffer();
	FParticleRenderData ParticleData;

	//Grab the material proxies we'll be using for each section and check them for translucency.
	bool bHasTranslucentMaterials = false;
	for (FMaterialRenderProxy* MaterialProxy : DynamicDataMesh->Materials)
	{
		check(MaterialProxy);
		EBlendMode BlendMode = MaterialProxy->GetMaterial(FeatureLevel)->GetBlendMode();
		bHasTranslucentMaterials |= IsTranslucentBlendMode(BlendMode);
	}

	const bool bShouldSort = SortMode != ENiagaraSortMode::None && (bHasTranslucentMaterials || !bSortOnlyWhenTranslucent);
	const bool bCustomSorting = SortMode == ENiagaraSortMode::CustomAscending || SortMode == ENiagaraSortMode::CustomDecending;
	//Disable the upload of sorting data if we're using a material that doesn't need it.
	//TODO: we can probably reinit the GPU layout info entirely to remove custom sorting from the buffer but for now just skip the upload if it's not needed.
	VFVariables[ENiagaraMeshVFLayout::CustomSorting].bUpload &= bCustomSorting;

	//For cpu sims we allocate render buffers from the global pool. GPU sims own their own.
	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		if (GbEnableMinimalGPUBuffers)
		{
			ParticleData = TransferDataToGPU(DynamicReadBuffer, SourceParticleData);
		}
		else
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderMeshes_AllocateGPUData);
			int32 TotalFloatSize = SourceParticleData->GetFloatBuffer().Num() / sizeof(float);
			ParticleData.FloatData = DynamicReadBuffer.AllocateFloat(TotalFloatSize);
			FMemory::Memcpy(ParticleData.FloatData.Buffer, SourceParticleData->GetFloatBuffer().GetData(), SourceParticleData->GetFloatBuffer().Num());
			int32 TotalHalfSize = SourceParticleData->GetHalfBuffer().Num() / sizeof(FFloat16);
			ParticleData.HalfData = DynamicReadBuffer.AllocateHalf(TotalFloatSize);
			FMemory::Memcpy(ParticleData.HalfData.Buffer, SourceParticleData->GetHalfBuffer().GetData(), SourceParticleData->GetHalfBuffer().Num());
		}
	}
	
	const int32 LODIndex = GetLODIndex();
	const FStaticMeshLODResources& LODModel = MeshRenderData->LODResources[LODIndex];

	if (SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		const int32 SectionCount = LODModel.Sections.Num();

		IndirectArgsOffsets.SetNum(SectionCount);
		for (int32 SectionIdx = 0; SectionIdx < SectionCount; ++SectionIdx)
		{
			IndirectArgsOffsets[SectionIdx] = Batcher->GetGPUInstanceCounterManager().AddDrawIndirect(
				SourceParticleData->GetGPUInstanceCountBufferOffset(),
				IndexInfoPerSection[LODIndex][SectionIdx].Key,
				IndexInfoPerSection[LODIndex][SectionIdx].Value);
		}
	}

	{
		int32 VertexFactoryIndex = 0;

		// Compute the per-view uniform buffers.
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];

				// Get the next vertex factory to use
				FNiagaraMeshVertexFactory* VertexFactory = nullptr;
				if (VertexFactories.IsValidIndex(VertexFactoryIndex))
				{
					VertexFactory = VertexFactories[VertexFactoryIndex];
					++VertexFactoryIndex;

					if (VertexFactory->GetLODIndex() != LODIndex)
					{
						SetupVertexFactory(VertexFactory, LODModel);
						VertexFactory->SetLODIndex(LODIndex);
					}
				}
				else
				{
					check(VertexFactoryIndex == VertexFactories.Num());
					VertexFactory = new FNiagaraMeshVertexFactory();
					VertexFactories.Add(VertexFactory);

					VertexFactory->SetParticleFactoryType(NVFT_Mesh);
					VertexFactory->SetLODIndex(LODIndex);
					VertexFactory->InitResource();
					SetupVertexFactory(VertexFactory, LODModel);
				}

				FNiagaraMeshCollectorResourcesMesh& CollectorResources = Collector.AllocateOneFrameResource<FNiagaraMeshCollectorResourcesMesh>();
				CollectorResources.VertexFactory = VertexFactory;

				FNiagaraMeshUniformParameters PerViewUniformParameters;// = UniformParameters;
				FMemory::Memzero(&PerViewUniformParameters, sizeof(PerViewUniformParameters)); // Clear unset bytes

				PerViewUniformParameters.bLocalSpace = bLocalSpace;
				PerViewUniformParameters.PrevTransformAvailable = false;
				PerViewUniformParameters.DeltaSeconds = ViewFamily.DeltaWorldTime;				

				PerViewUniformParameters.PositionDataOffset = VFVariables[ENiagaraMeshVFLayout::Position].GetGPUOffset();
				PerViewUniformParameters.VelocityDataOffset = VFVariables[ENiagaraMeshVFLayout::Velocity].GetGPUOffset();
				PerViewUniformParameters.ColorDataOffset = VFVariables[ENiagaraMeshVFLayout::Color].GetGPUOffset();
				PerViewUniformParameters.ScaleDataOffset = VFVariables[ENiagaraMeshVFLayout::Scale].GetGPUOffset();
				PerViewUniformParameters.TransformDataOffset = VFVariables[ENiagaraMeshVFLayout::Transform].GetGPUOffset();
				PerViewUniformParameters.NormalizedAgeDataOffset = VFVariables[ENiagaraMeshVFLayout::NormalizedAge].GetGPUOffset();
				PerViewUniformParameters.MaterialRandomDataOffset = VFVariables[ENiagaraMeshVFLayout::MaterialRandom].GetGPUOffset();
				PerViewUniformParameters.SubImageDataOffset = VFVariables[ENiagaraMeshVFLayout::SubImage].GetGPUOffset();
				PerViewUniformParameters.MaterialParamDataOffset = VFVariables[ENiagaraMeshVFLayout::DynamicParam0].GetGPUOffset();
				PerViewUniformParameters.MaterialParam1DataOffset = VFVariables[ENiagaraMeshVFLayout::DynamicParam1].GetGPUOffset();
				PerViewUniformParameters.MaterialParam2DataOffset = VFVariables[ENiagaraMeshVFLayout::DynamicParam2].GetGPUOffset();
				PerViewUniformParameters.MaterialParam3DataOffset = VFVariables[ENiagaraMeshVFLayout::DynamicParam3].GetGPUOffset();
				PerViewUniformParameters.CameraOffsetDataOffset = VFVariables[ENiagaraMeshVFLayout::CameraOffset].GetGPUOffset();

				PerViewUniformParameters.MaterialParamValidMask = MaterialParamValidMask;
				PerViewUniformParameters.SizeDataOffset = INDEX_NONE;
				PerViewUniformParameters.DefaultPos = bLocalSpace ? FVector4(0.0f, 0.0f, 0.0f, 1.0f) : FVector4(SceneProxy->GetLocalToWorld().GetOrigin());
				PerViewUniformParameters.SubImageSize = FVector4(SubImageSize.X, SubImageSize.Y, 1.0f / SubImageSize.X, 1.0f / SubImageSize.Y);
				PerViewUniformParameters.SubImageBlendMode = bSubImageBlend;
				PerViewUniformParameters.FacingMode = (uint32)FacingMode;
				PerViewUniformParameters.bLockedAxisEnable = bLockedAxisEnable;
				PerViewUniformParameters.LockedAxis = LockedAxis;
				PerViewUniformParameters.LockedAxisSpace = (uint32)LockedAxisSpace;

				//Sort particles if needed.
				CollectorResources.VertexFactory->SetSortedIndices(nullptr, 0xFFFFFFFF);

				FNiagaraGPUSortInfo SortInfo;
				const int32 SortVarIdx = bCustomSorting ? ENiagaraMeshVFLayout::CustomSorting : ENiagaraMeshVFLayout::Position;
				const bool bSortVarIsHalf = bShouldSort && VFVariables[SortVarIdx].bHalfType;
				if (bShouldSort && VFVariables[SortVarIdx].GetGPUOffset() != INDEX_NONE)
				{
					SortInfo.ParticleCount = NumInstances;
					SortInfo.SortMode = SortMode;
					SortInfo.SortAttributeOffset = (VFVariables[SortVarIdx].GetGPUOffset() & ~(1 << 31));
					SortInfo.SetSortFlags(GNiagaraGPUSortingUseMaxPrecision != 0, bHasTranslucentMaterials); 
					if (!bCustomSorting)
					{
						SortInfo.ViewOrigin = View->ViewMatrices.GetViewOrigin();
						SortInfo.ViewDirection = View->GetViewDirection();
						if (bLocalSpace)
						{
							SortInfo.ViewOrigin = SceneProxy->GetLocalToWorldInverse().TransformPosition(SortInfo.ViewOrigin);
							SortInfo.ViewDirection = SceneProxy->GetLocalToWorld().GetTransposed().TransformVector(SortInfo.ViewDirection);
						}
					}
				};

				if (SimTarget == ENiagaraSimTarget::CPUSim)//TODO: Compute shader for sorting gpu sims and larger cpu sims.
				{
					FRHIShaderResourceView* FloatSRV = ParticleData.FloatData.IsValid() ? ParticleData.FloatData.SRV : (FRHIShaderResourceView*)FNiagaraRenderer::GetDummyFloatBuffer();
					FRHIShaderResourceView* HalfSRV = ParticleData.HalfData.IsValid() ? ParticleData.HalfData.SRV : (FRHIShaderResourceView*)FNiagaraRenderer::GetDummyHalfBuffer();
					const uint32 FloatParticleDataStride = GbEnableMinimalGPUBuffers ? SourceParticleData->GetNumInstances() : (SourceParticleData->GetFloatStride() / sizeof(float));
					check(GbEnableMinimalGPUBuffers || FloatParticleDataStride == SourceParticleData->GetHalfStride() / sizeof(FFloat16));

					if (SortInfo.SortMode != ENiagaraSortMode::None && SortInfo.SortAttributeOffset != INDEX_NONE)
					{
						if (GNiagaraGPUSortingCPUToGPUThreshold >= 0 &&
							SortInfo.ParticleCount >= GNiagaraGPUSortingCPUToGPUThreshold &&
							FNiagaraUtilities::AllowComputeShaders(Batcher->GetShaderPlatform()))
						{
							SortInfo.ParticleCount = NumInstances;
							SortInfo.ParticleDataFloatSRV = bSortVarIsHalf ? HalfSRV : FloatSRV;
							SortInfo.FloatDataStride = FloatParticleDataStride; // Same if Halfs.
							if (Batcher->AddSortedGPUSimulation(SortInfo))
							{
								CollectorResources.VertexFactory->SetSortedIndices(SortInfo.AllocationInfo.BufferSRV, SortInfo.AllocationInfo.BufferOffset);
							}
						}
						else
						{
							FGlobalDynamicReadBuffer::FAllocation SortedIndices;
							SortedIndices = DynamicReadBuffer.AllocateInt32(NumInstances);
							SortIndices(SortInfo, SortVarIdx, *SourceParticleData, SortedIndices);
							CollectorResources.VertexFactory->SetSortedIndices(SortedIndices.SRV, 0);
						}
					}

					PerViewUniformParameters.NiagaraFloatDataStride = FloatParticleDataStride;
					PerViewUniformParameters.NiagaraParticleDataFloat = FloatSRV;
					PerViewUniformParameters.NiagaraParticleDataHalf = HalfSRV;
				}
				else
				{
					FRHIShaderResourceView* FloatSRV = SourceParticleData->GetGPUBufferFloat().SRV.IsValid() ? (FRHIShaderResourceView*)SourceParticleData->GetGPUBufferFloat().SRV : (FRHIShaderResourceView*)FNiagaraRenderer::GetDummyFloatBuffer();
					FRHIShaderResourceView* HalfSRV = SourceParticleData->GetGPUBufferHalf().SRV.IsValid() ? (FRHIShaderResourceView*)SourceParticleData->GetGPUBufferHalf().SRV : (FRHIShaderResourceView*)FNiagaraRenderer::GetDummyHalfBuffer();
					const uint32 FloatParticleDataStride = SourceParticleData->GetFloatStride() / sizeof(float);
					check(FloatParticleDataStride == SourceParticleData->GetHalfStride() / sizeof(FFloat16));

					if (SortInfo.SortMode != ENiagaraSortMode::None && SortInfo.SortAttributeOffset != INDEX_NONE)
					{
						// Here we need to be conservative about the InstanceCount, since the final value is only known on the GPU after the simulation.
						SortInfo.ParticleCount = SourceParticleData->GetNumInstances();
						SortInfo.ParticleDataFloatSRV = bSortVarIsHalf ? HalfSRV : FloatSRV;
						SortInfo.FloatDataStride = FloatParticleDataStride; // Same if Halfs.
						SortInfo.GPUParticleCountSRV = Batcher->GetGPUInstanceCounterManager().GetInstanceCountBuffer().SRV;
						SortInfo.GPUParticleCountOffset = SourceParticleData->GetGPUInstanceCountBufferOffset();
						if (Batcher->AddSortedGPUSimulation(SortInfo))
						{
							CollectorResources.VertexFactory->SetSortedIndices(SortInfo.AllocationInfo.BufferSRV, SortInfo.AllocationInfo.BufferOffset);
						}
					}
					
					PerViewUniformParameters.NiagaraFloatDataStride = FloatParticleDataStride;
					PerViewUniformParameters.NiagaraParticleDataFloat = FloatSRV;
					PerViewUniformParameters.NiagaraParticleDataHalf = HalfSRV;
				}

				// Collector.AllocateOneFrameResource uses default ctor, initialize the vertex factory
				CollectorResources.UniformBuffer = FNiagaraMeshUniformBufferRef::CreateUniformBufferImmediate(PerViewUniformParameters, UniformBuffer_SingleFrame);
				CollectorResources.VertexFactory->SetUniformBuffer(CollectorResources.UniformBuffer);

				// Increment stats
				INC_DWORD_STAT_BY(STAT_NiagaraNumMeshVerts, NumInstances * LODModel.GetNumVertices());
				INC_DWORD_STAT_BY(STAT_NiagaraNumMeshes, NumInstances);

				// GPU mesh rendering currently only supports one mesh section.
				// TODO: Add proper support for multiple mesh sections for GPU mesh particles.
				const int32 SectionCount = LODModel.Sections.Num();
				const bool bIsWireframe = AllowDebugViewmodes() && View && View->Family->EngineShowFlags.Wireframe;
				for (int32 SectionIndex = 0; SectionIndex < SectionCount; SectionIndex++)
				{
					const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
					FMaterialRenderProxy* MaterialProxy = DynamicDataMesh->Materials[SectionIndex];
					if ((Section.NumTriangles == 0) || (MaterialProxy == NULL))
					{
						//@todo. This should never occur, but it does occasionally.
						continue;
					}

					FMeshBatch& Mesh = Collector.AllocateMesh();
					Mesh.VertexFactory = CollectorResources.VertexFactory;
					Mesh.LCI = NULL;
					Mesh.ReverseCulling = SceneProxy->IsLocalToWorldDeterminantNegative();
					Mesh.CastShadow = SceneProxy->CastsDynamicShadow();
#if RHI_RAYTRACING
					Mesh.CastRayTracedShadow = SceneProxy->CastsDynamicShadow();
#endif
					Mesh.DepthPriorityGroup = (ESceneDepthPriorityGroup)SceneProxy->GetDepthPriorityGroup(View);

					FMeshBatchElement& BatchElement = Mesh.Elements[0];
					BatchElement.PrimitiveUniformBuffer = IsMotionBlurEnabled() ? SceneProxy->GetUniformBuffer() : SceneProxy->GetUniformBufferNoVelocity();
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
							Mesh.MaterialRenderProxy = MaterialProxy;
							Mesh.bWireframe = true;
							BatchElement.FirstIndex = 0;
							BatchElement.IndexBuffer = &LODModel.IndexBuffer;
							BatchElement.NumPrimitives = LODModel.IndexBuffer.GetNumIndices() / 3;
						}
					}
					else
					{
						Mesh.Type = PT_TriangleList;
						Mesh.MaterialRenderProxy = MaterialProxy;
						BatchElement.IndexBuffer = &LODModel.IndexBuffer;
						BatchElement.FirstIndex = Section.FirstIndex;
						BatchElement.NumPrimitives = Section.NumTriangles;
					}

					if (IndirectArgsOffsets.IsValidIndex(SectionIndex))
					{
						BatchElement.NumPrimitives = 0;
						BatchElement.IndirectArgsOffset = IndirectArgsOffsets[SectionIndex];
						BatchElement.IndirectArgsBuffer = Batcher->GetGPUInstanceCounterManager().GetDrawIndirectBuffer().Buffer;
					}
					else
					{
						check(BatchElement.NumPrimitives > 0);
					}

					Mesh.bCanApplyViewModeOverrides = true;
					Mesh.bUseWireframeSelectionColoring = SceneProxy->IsSelected();

					Collector.AddMesh(ViewIndex, Mesh);
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

	SCOPE_CYCLE_COUNTER(STAT_NiagaraRender);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderMeshes);
	check(SceneProxy);

	NiagaraEmitterInstanceBatcher* Batcher = SceneProxy->GetBatcher();
	FNiagaraDynamicDataMesh *DynamicDataMesh = (static_cast<FNiagaraDynamicDataMesh*>(DynamicDataRender));
	if (!DynamicDataMesh || !Batcher)
	{
		return;
	}

	FNiagaraDataBuffer* SourceParticleData = DynamicDataMesh->GetParticleDataToRender();
	if (SourceParticleData == nullptr ||
		MeshRenderData == nullptr ||
		SourceParticleData->GetNumInstancesAllocated() == 0 ||
		SourceParticleData->GetNumInstances() == 0 ||
		GbEnableNiagaraMeshRendering == 0 ||
		!GSupportsResourceView  // Current shader requires SRV to draw properly in all cases.
		)
	{
		return;
	}

	int32 LODIndex = (int32)MeshRenderData->CurrentFirstLODIdx;
	while (LODIndex < MeshRenderData->LODResources.Num() - 1 && !MeshRenderData->LODResources[LODIndex].GetNumVertices())
	{
		++LODIndex;
	}

	const FStaticMeshLODResources& LODModel = MeshRenderData->LODResources[LODIndex];
	FRayTracingGeometry& Geometry = MeshRenderData->LODResources[LODIndex].RayTracingGeometry;
	FRayTracingInstance RayTracingInstance;
	RayTracingInstance.Geometry = &Geometry;

	for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
	{
		const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
		FMaterialRenderProxy* MaterialProxy = DynamicDataMesh->Materials[SectionIndex];
		if ((Section.NumTriangles == 0) || (MaterialProxy == NULL))
		{
			continue;
		}

		FMeshBatch MeshBatch;
		const FStaticMeshLODResources& LOD = MeshRenderData->LODResources[LODIndex];
		const FStaticMeshVertexFactories& VFs = MeshRenderData->LODVertexFactories[LODIndex];
		FVertexFactory* VertexFactory = &MeshRenderData->LODVertexFactories[LODIndex].VertexFactory;

		MeshBatch.VertexFactory = VertexFactory;
		MeshBatch.MaterialRenderProxy = MaterialProxy;
		MeshBatch.SegmentIndex = SectionIndex;
		MeshBatch.LODIndex = LODIndex;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		MeshBatch.VisualizeLODIndex = LODIndex;
#endif
		MeshBatch.CastRayTracedShadow = MeshBatch.CastShadow;
		FMeshBatchElement& MeshBatchElement = MeshBatch.Elements[0];
		MeshBatchElement.VertexFactoryUserData = VFs.VertexFactory.GetUniformBuffer();
		MeshBatchElement.MinVertexIndex = Section.MinVertexIndex;
		MeshBatchElement.MaxVertexIndex = Section.MaxVertexIndex;
		
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		MeshBatchElement.VisualizeElementIndex = SectionIndex;
#endif
		RayTracingInstance.Materials.Add(MeshBatch);
	}

	int32 NumInstances = SourceParticleData->GetNumInstances();
	int32 TotalFloatSize = TotalVFFloatComponents * SourceParticleData->GetNumInstances();
	int32 ComponentStrideDest = SourceParticleData->GetNumInstances() * sizeof(float);

	//ENiagaraMeshVFLayout::Transform just contains a Quat, not the whole transform
	FNiagaraRendererVariableInfo& VarPositionInfo	= VFVariables[ENiagaraMeshVFLayout::Position];
	FNiagaraRendererVariableInfo& VarScaleInfo		= VFVariables[ENiagaraMeshVFLayout::Scale];
	FNiagaraRendererVariableInfo& VarTransformInfo	= VFVariables[ENiagaraMeshVFLayout::Transform];
	
	int32 PositionBaseCompOffset	= VarPositionInfo.DatasetOffset;
	int32 ScaleBaseCompOffset		= VarScaleInfo.DatasetOffset;
	int32 TransformBaseCompOffset	= VarTransformInfo.DatasetOffset;
	
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

	FMatrix LocalTransform(SceneProxy->GetLocalToWorld());

	for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; InstanceIndex++)
	{	
		FMatrix InstanceTransform(FMatrix::Identity);

		if (SimTarget == ENiagaraSimTarget::CPUSim)
		{
			FVector4 InstancePos = GetInstancePosition(InstanceIndex);
			
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
				FVector InstanceSca(GetInstanceScale(InstanceIndex));
				ScaleMatrix.M[0][0] *= InstanceSca.X;
				ScaleMatrix.M[1][1] *= InstanceSca.Y;
				ScaleMatrix.M[2][2] *= InstanceSca.Z;
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
		InstanceGPUTransformsBuffer.Initialize(3*4*sizeof(float), CPUInstancesCount, BUF_Static);
		RayTracingInstance.InstanceGPUTransformsSRV = InstanceGPUTransformsBuffer.SRV;

		FNiagaraGPURayTracingTransformsCS::FPermutationDomain PermutationVector;

		TShaderMapRef<FNiagaraGPURayTracingTransformsCS> GPURayTracingTransformsCS(GetGlobalShaderMap(FeatureLevel), PermutationVector);
		RHICmdList.SetComputeShader(GPURayTracingTransformsCS.GetComputeShader());

		const FUintVector4 NiagaraOffsets(
			VFVariables[ENiagaraMeshVFLayout::Position].GetGPUOffset(), 
			VFVariables[ENiagaraMeshVFLayout::Transform].GetGPUOffset(),
			VFVariables[ENiagaraMeshVFLayout::Scale].GetGPUOffset(),
			bLocalSpace? 1 : 0);

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

		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, InstanceGPUTransformsBuffer.UAV);
	}

	OutRayTracingInstances.Add(RayTracingInstance);
}
#endif


FNiagaraDynamicDataBase *FNiagaraRendererMeshes::GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderGT);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGenMeshVertexData);

	const UNiagaraMeshRendererProperties* Properties = CastChecked<const UNiagaraMeshRendererProperties>(InProperties);

	if (Properties->ParticleMesh == nullptr)
	{
		return nullptr;
	}

	FNiagaraDynamicDataMesh *DynamicData = nullptr;

	if (Properties->ParticleMesh)
	{
		DynamicData = new FNiagaraDynamicDataMesh(Emitter);

		const int32 LODIndex = GetLODIndex();
		const FStaticMeshLODResources& LODModel = MeshRenderData->LODResources[LODIndex];

		check(BaseMaterials_GT.Num() == LODModel.Sections.Num());

		DynamicData->Materials.Reset(LODModel.Sections.Num());
		DynamicData->SetMaterialRelevance(BaseMaterialRelevance_GT);
		for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
		{
			const FStaticMeshSection& Section = LODModel.Sections[SectionIndex];
			UMaterialInterface* SectionMat = nullptr;

			//In preparation for a material override feature, we pass our material(s) and relevance in via dynamic data.
			//The renderer ensures we have the correct usage and relevance for materials in BaseMaterials_GT.
			//Any override feature must also do the same for materials that are set.
			check(BaseMaterials_GT[SectionIndex]->CheckMaterialUsage_Concurrent(MATUSAGE_NiagaraMeshParticles));
			DynamicData->Materials.Add(BaseMaterials_GT[SectionIndex]->GetRenderProxy());
		}
	}

	return DynamicData;
}

int FNiagaraRendererMeshes::GetDynamicDataSize()const
{
	uint32 Size = sizeof(FNiagaraDynamicDataMesh);
	return Size;
}

bool FNiagaraRendererMeshes::IsMaterialValid(UMaterialInterface* Mat)const
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