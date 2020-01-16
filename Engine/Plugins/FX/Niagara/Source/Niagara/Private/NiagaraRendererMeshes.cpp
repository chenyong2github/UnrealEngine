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
		if ( VertexFactory )
		{
			VertexFactory->SetParticleData(nullptr, 0, 0);
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
	SortMode = Properties->SortMode;
	bSortOnlyWhenTranslucent = Properties->bSortOnlyWhenTranslucent;
	bOverrideMaterials = Properties->bOverrideMaterials;
	SubImageSize = Properties->SubImageSize;
	bSubImageBlend = Properties->bSubImageBlend;

	const FNiagaraDataSet& Data = Emitter->GetData();

	MaterialParamValidMask = 0;
	TotalVFComponents = 0;
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
	MaterialParamValidMask |= SetVertexFactoryVariable(Data, Properties->DynamicMaterialBinding.DataSetVariable, ENiagaraMeshVFLayout::DynamicParam0) ? 0x1 : 0;
	MaterialParamValidMask |= SetVertexFactoryVariable(Data, Properties->DynamicMaterial1Binding.DataSetVariable, ENiagaraMeshVFLayout::DynamicParam1) ? 0x2 : 0;
	MaterialParamValidMask |= SetVertexFactoryVariable(Data, Properties->DynamicMaterial2Binding.DataSetVariable, ENiagaraMeshVFLayout::DynamicParam2) ? 0x4 : 0;
	MaterialParamValidMask |= SetVertexFactoryVariable(Data, Properties->DynamicMaterial3Binding.DataSetVariable, ENiagaraMeshVFLayout::DynamicParam3) ? 0x8 : 0;

	MeshMinimumLOD = Properties->ParticleMesh->MinLOD.GetValueForFeatureLevel(FeatureLevel);
}

FNiagaraRendererMeshes::~FNiagaraRendererMeshes()
{
	for ( FNiagaraMeshVertexFactory* VertexFactory : VertexFactories )
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

void FNiagaraRendererMeshes::CreateRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher)
{
	FNiagaraRenderer::CreateRenderThreadResources(Batcher);
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
	if( SourceParticleData == nullptr ||
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

	// @TODO : support multiple LOD and section, using an inlined array and/or the SceneRenderingAllocator
	uint32 IndirectArgsOffset = INDEX_NONE;
	int32 NumInstances = SourceParticleData->GetNumInstances();

	FGlobalDynamicReadBuffer& DynamicReadBuffer = Collector.GetDynamicReadBuffer();
	FGlobalDynamicReadBuffer::FAllocation ParticleData;

	//Grab the material proxies we'll be using for each section and check them for translucency.
	bool bHasTranslucentMaterials = false;
	for (FMaterialRenderProxy* MaterialProxy : DynamicDataMesh->Materials)
	{
		check(MaterialProxy);
		EBlendMode BlendMode = MaterialProxy->GetMaterial(FeatureLevel)->GetBlendMode();
		bHasTranslucentMaterials |= BlendMode == BLEND_AlphaComposite || BlendMode == BLEND_AlphaHoldout || BlendMode == BLEND_Translucent;
	}

	bool bShouldSort = SortMode != ENiagaraSortMode::None && (bHasTranslucentMaterials || !bSortOnlyWhenTranslucent);
	bool bCustomSorting = SortMode == ENiagaraSortMode::CustomAscending || SortMode == ENiagaraSortMode::CustomDecending;
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
			ParticleData = DynamicReadBuffer.AllocateFloat(TotalFloatSize);
			FMemory::Memcpy(ParticleData.Buffer, SourceParticleData->GetFloatBuffer().GetData(), SourceParticleData->GetFloatBuffer().Num());
		}
	}
	
	
	if(SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		IndirectArgsOffset = Batcher->GetGPUInstanceCounterManager().AddDrawIndirect(SourceParticleData->GetGPUInstanceCountBufferOffset(), NumIndicesPerInstance);
	}

	{
		int32 VertexFactoryIndex = 0;

		// Compute the per-view uniform buffers.
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];

				// Doesn't seem to work for some reason. See comment in FDynamicMeshEmitterData::GetMeshLODIndexFromProxy()
				// const int32 LODIndex = FMath::Max<int32>((int32)MeshRenderData->CurrentFirstLODIdx, MeshMinimumLOD);
				int32 LODIndex = (int32)MeshRenderData->CurrentFirstLODIdx;
				while (LODIndex < MeshRenderData->LODResources.Num() - 1 && !MeshRenderData->LODResources[LODIndex].GetNumVertices())
				{
					++LODIndex;
				}
				const FStaticMeshLODResources& LODModel = MeshRenderData->LODResources[LODIndex];

				// Get the next vertex factory to use
				FNiagaraMeshVertexFactory* VertexFactory = nullptr;
				if ( VertexFactories.IsValidIndex(VertexFactoryIndex) )
				{
					VertexFactory = VertexFactories[VertexFactoryIndex];
					++VertexFactoryIndex;

					if ( VertexFactory->GetLODIndex() != LODIndex )
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
					VertexFactory->SetMeshFacingMode((uint32)FacingMode);
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

				PerViewUniformParameters.MaterialParamValidMask = MaterialParamValidMask;
				PerViewUniformParameters.SizeDataOffset = INDEX_NONE;
				PerViewUniformParameters.DefaultPos = bLocalSpace ? FVector4(0.0f, 0.0f, 0.0f, 1.0f) : FVector4(SceneProxy->GetLocalToWorld().GetOrigin());
				PerViewUniformParameters.SubImageSize = FVector4(SubImageSize.X, SubImageSize.Y, 1.0f / SubImageSize.X, 1.0f / SubImageSize.Y);
				PerViewUniformParameters.SubImageBlendMode = bSubImageBlend;

				//Sort particles if needed.
				CollectorResources.VertexFactory->SetSortedIndices(nullptr, 0xFFFFFFFF);

				FNiagaraGPUSortInfo SortInfo;
				SortInfo.SortAttributeOffset = INDEX_NONE;
				int32 SortVarIdx = INDEX_NONE;
				if (bShouldSort)
				{
					SortInfo.ParticleCount = NumInstances;
					SortInfo.SortMode = SortMode;
					if (bCustomSorting)
					{
						SortVarIdx = ENiagaraMeshVFLayout::CustomSorting;
						SortInfo.SortAttributeOffset = VFVariables[ENiagaraMeshVFLayout::CustomSorting].GetGPUOffset();
						SortInfo.ViewOrigin.Set(0, 0, 0);
						SortInfo.ViewDirection.Set(0, 0, 1);
					}
					else
					{
						SortVarIdx = ENiagaraMeshVFLayout::Position;
						SortInfo.SortAttributeOffset = VFVariables[ENiagaraMeshVFLayout::Position].GetGPUOffset();
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
					check(ParticleData.IsValid());
					if (SortInfo.SortMode != ENiagaraSortMode::None && SortInfo.SortAttributeOffset != INDEX_NONE)
					{
						if (GNiagaraGPUSorting &&
							GNiagaraGPUSortingCPUToGPUThreshold != INDEX_NONE &&
							SortInfo.ParticleCount >= GNiagaraGPUSortingCPUToGPUThreshold)
						{
							SortInfo.ParticleCount = NumInstances;
							SortInfo.ParticleDataFloatSRV = ParticleData.ReadBuffer->SRV;
							SortInfo.FloatDataOffset = ParticleData.FirstIndex / sizeof(float);
							SortInfo.FloatDataStride = SourceParticleData->GetFloatStride() / sizeof(float);
							const int32 IndexBufferOffset = Batcher->AddSortedGPUSimulation(SortInfo);
							if (IndexBufferOffset != INDEX_NONE)
							{
								CollectorResources.VertexFactory->SetSortedIndices(Batcher->GetGPUSortedBuffer().VertexBufferSRV, IndexBufferOffset);
							}
						}
						else
						{
							FGlobalDynamicReadBuffer::FAllocation SortedIndices;
							SortedIndices = DynamicReadBuffer.AllocateInt32(NumInstances);
							SortIndices(SortInfo, SortVarIdx, *SourceParticleData, SortedIndices);
							CollectorResources.VertexFactory->SetSortedIndices(SortedIndices.ReadBuffer->SRV, SortedIndices.FirstIndex / sizeof(float));
						}
					}
					int32 ParticleDataStride = GbEnableMinimalGPUBuffers ? SourceParticleData->GetNumInstances() : SourceParticleData->GetFloatStride() / sizeof(float);
					CollectorResources.VertexFactory->SetParticleData(ParticleData.ReadBuffer->SRV, ParticleData.FirstIndex / sizeof(float), ParticleDataStride);
				}
				else
				{
					if (SortInfo.SortMode != ENiagaraSortMode::None && SortInfo.SortAttributeOffset != INDEX_NONE && GNiagaraGPUSorting)
					{
						// Here we need to be conservative about the InstanceCount, since the final value is only known on the GPU after the simulation.
						SortInfo.ParticleCount = SourceParticleData->GetNumInstances();
						SortInfo.ParticleDataFloatSRV = SourceParticleData->GetGPUBufferFloat().SRV;
						SortInfo.FloatDataOffset = 0;
						SortInfo.FloatDataStride = SourceParticleData->GetFloatStride() / sizeof(float);
						SortInfo.GPUParticleCountSRV = Batcher->GetGPUInstanceCounterManager().GetInstanceCountBuffer().SRV;
						SortInfo.GPUParticleCountOffset = SourceParticleData->GetGPUInstanceCountBufferOffset();
						const int32 IndexBufferOffset = Batcher->AddSortedGPUSimulation(SortInfo);
						if (IndexBufferOffset != INDEX_NONE && SortInfo.GPUParticleCountOffset != INDEX_NONE)
						{
							CollectorResources.VertexFactory->SetSortedIndices(Batcher->GetGPUSortedBuffer().VertexBufferSRV, IndexBufferOffset);
						}
					}
					if (SourceParticleData->GetGPUBufferFloat().SRV.IsValid())
					{
						CollectorResources.VertexFactory->SetParticleData(SourceParticleData->GetGPUBufferFloat().SRV, 0, SourceParticleData->GetFloatStride() / sizeof(float));
					}
					else
					{
						CollectorResources.VertexFactory->SetParticleData(FNiagaraRenderer::GetDummyFloatBuffer().SRV, 0, 0);
					}
				}

				// Collector.AllocateOneFrameResource uses default ctor, initialize the vertex factory
				CollectorResources.UniformBuffer = FNiagaraMeshUniformBufferRef::CreateUniformBufferImmediate(PerViewUniformParameters, UniformBuffer_SingleFrame);
				CollectorResources.VertexFactory->SetUniformBuffer(CollectorResources.UniformBuffer);
			
				// GPU mesh rendering currently only supports one mesh section.
				// TODO: Add proper support for multiple mesh sections for GPU mesh particles.
				int32 MaxSection = SimTarget == ENiagaraSimTarget::GPUComputeSim ? 1 : LODModel.Sections.Num();
				const bool bIsWireframe = AllowDebugViewmodes() && View && View->Family->EngineShowFlags.Wireframe;
				for (int32 SectionIndex = 0; SectionIndex < MaxSection; SectionIndex++)
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

					if (IndirectArgsOffset != INDEX_NONE)
					{
						BatchElement.NumPrimitives = 0;
						BatchElement.IndirectArgsOffset = IndirectArgsOffset;
						BatchElement.IndirectArgsBuffer = Batcher->GetGPUInstanceCounterManager().GetDrawIndirectBuffer().Buffer;
					}
					else
					{
						check(BatchElement.NumPrimitives > 0);
					}

					Mesh.bCanApplyViewModeOverrides = true;
					Mesh.bUseWireframeSelectionColoring = SceneProxy->IsSelected();

					Collector.AddMesh(ViewIndex, Mesh);

					INC_DWORD_STAT_BY(STAT_NiagaraNumMeshVerts, NumInstances * LODModel.GetNumVertices());
					INC_DWORD_STAT_BY(STAT_NiagaraNumMeshes, NumInstances);
				}
			}
		}
	}
}

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

		// Doesn't seem to work for some reason. See comment in FDynamicMeshEmitterData::GetMeshLODIndexFromProxy()
		// const int32 LODIndex = FMath::Max<int32>((int32)MeshRenderData->CurrentFirstLODIdx, MeshMinimumLOD);
		int32 LODIndex = (int32)MeshRenderData->CurrentFirstLODIdx;
		while (LODIndex < MeshRenderData->LODResources.Num() - 1 && !MeshRenderData->LODResources[LODIndex].GetNumVertices())
		{
			++LODIndex;
		}
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
	return Mat && Mat->CheckMaterialUsage(MATUSAGE_NiagaraMeshParticles);
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
		while(--i >= 0)
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