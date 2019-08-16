// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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


DECLARE_DWORD_COUNTER_STAT(TEXT("NumMeshesRenderer"), STAT_NiagaraNumMeshes, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumMesheVerts"), STAT_NiagaraNumMeshVerts, STATGROUP_Niagara);

static int32 GbEnableNiagaraMeshRendering = 1;
static FAutoConsoleVariableRef CVarEnableNiagaraMeshRendering(
	TEXT("fx.EnableNiagaraMeshRendering"),
	GbEnableNiagaraMeshRendering,
	TEXT("If == 0, Niagara Mesh Renderers are disabled. \n"),
	ECVF_Default
);

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
	FNiagaraMeshVertexFactory VertexFactory;
	FNiagaraMeshUniformBufferRef UniformBuffer;

	virtual ~FNiagaraMeshCollectorResourcesMesh()
	{
		VertexFactory.ReleaseResource();
	}
};

//////////////////////////////////////////////////////////////////////////

FNiagaraRendererMeshes::FNiagaraRendererMeshes(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *Props, const FNiagaraEmitterInstance* Emitter)
	: FNiagaraRenderer(FeatureLevel, Props, Emitter)
	, PositionOffset(INDEX_NONE)
	, VelocityOffset(INDEX_NONE)
	, ColorOffset(INDEX_NONE)
	, ScaleOffset(INDEX_NONE)
	, SizeOffset(INDEX_NONE)
	, MaterialParamValidMask(0)
	, MaterialParamOffset(INDEX_NONE)
	, MaterialParamOffset1(INDEX_NONE)
	, MaterialParamOffset2(INDEX_NONE)
	, MaterialParamOffset3(INDEX_NONE)
	, TransformOffset(INDEX_NONE)
	, NormalizedAgeOffset(INDEX_NONE)
	, MaterialRandomOffset(INDEX_NONE)
	, CustomSortingOffset(INDEX_NONE)
{
	check(Emitter);
	check(Props);

	VertexFactory = ConstructNiagaraMeshVertexFactory(NVFT_Mesh, FeatureLevel);
	
	const UNiagaraMeshRendererProperties* Properties = CastChecked<const UNiagaraMeshRendererProperties>(Props);
	check(Properties->ParticleMesh);

	MeshRenderData = Properties->ParticleMesh->RenderData.Get();

	FacingMode = Properties->FacingMode;
	SortMode = Properties->SortMode;
	bSortOnlyWhenTranslucent = Properties->bSortOnlyWhenTranslucent;
	bOverrideMaterials = Properties->bOverrideMaterials;

	const FNiagaraDataSet& Data = Emitter->GetData();
	int32 IntDummy;
	SizeOffset = -1;
	Data.GetVariableComponentOffsets(Properties->PositionBinding.DataSetVariable, PositionOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->VelocityBinding.DataSetVariable, VelocityOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->ColorBinding.DataSetVariable, ColorOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->ScaleBinding.DataSetVariable, ScaleOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->DynamicMaterialBinding.DataSetVariable, MaterialParamOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->DynamicMaterial1Binding.DataSetVariable, MaterialParamOffset1, IntDummy);
	Data.GetVariableComponentOffsets(Properties->DynamicMaterial2Binding.DataSetVariable, MaterialParamOffset2, IntDummy);
	Data.GetVariableComponentOffsets(Properties->DynamicMaterial3Binding.DataSetVariable, MaterialParamOffset3, IntDummy);
	Data.GetVariableComponentOffsets(Properties->MeshOrientationBinding.DataSetVariable, TransformOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->NormalizedAgeBinding.DataSetVariable, NormalizedAgeOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->MaterialRandomBinding.DataSetVariable, MaterialRandomOffset, IntDummy);
	Data.GetVariableComponentOffsets(Properties->CustomSortingBinding.DataSetVariable, CustomSortingOffset, IntDummy);

	MaterialParamValidMask = MaterialParamOffset  == -1 ? 0 : 0x1;
	MaterialParamValidMask |= MaterialParamOffset1 == -1 ? 0 : 0x2;
	MaterialParamValidMask |= MaterialParamOffset2 == -1 ? 0 : 0x4;
	MaterialParamValidMask |= MaterialParamOffset3 == -1 ? 0 : 0x8;

	MeshMinimumLOD = Properties->ParticleMesh->MinLOD.GetValueForFeatureLevel(FeatureLevel);
}

FNiagaraRendererMeshes::~FNiagaraRendererMeshes()
{
	if (VertexFactory != nullptr)
	{
		delete VertexFactory;
		VertexFactory = nullptr;
	}
}

void FNiagaraRendererMeshes::ReleaseRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher)
{
	FNiagaraRenderer::ReleaseRenderThreadResources(Batcher);
	VertexFactory->ReleaseResource();
	WorldSpacePrimitiveUniformBuffer.ReleaseResource();
}

void FNiagaraRendererMeshes::CreateRenderThreadResources(NiagaraEmitterInstanceBatcher* Batcher)
{
	FNiagaraRenderer::CreateRenderThreadResources(Batcher);
	VertexFactory->InitResource();
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
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRender);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraRenderMeshes);
	check(SceneProxy);

	SimpleTimer MeshElementsTimer;

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

	int32 NumInstances = SourceParticleData->GetNumInstances();

	int32 TotalFloatSize = SourceParticleData->GetFloatBuffer().Num() / sizeof(float);
	FGlobalDynamicReadBuffer& DynamicReadBuffer = Collector.GetDynamicReadBuffer();
	FGlobalDynamicReadBuffer::FAllocation ParticleData;

	// @TODO : support multiple LOD and section, using an inlined array and/or the SceneRenderingAllocator
	uint32 IndirectArgsOffset = INDEX_NONE;
	//For cpu sims we allocate render buffers from the global pool. GPU sims own their own.
	if (SimTarget == ENiagaraSimTarget::CPUSim)
	{
		ParticleData = DynamicReadBuffer.AllocateFloat(TotalFloatSize);
		FMemory::Memcpy(ParticleData.Buffer, SourceParticleData->GetFloatBuffer().GetData(), SourceParticleData->GetFloatBuffer().Num());
	}
	else // ENiagaraSimTarget::GPUComputeSim
	{
		IndirectArgsOffset = Batcher->GetGPUInstanceCounterManager().AddDrawIndirect(SourceParticleData->GetGPUInstanceCountBufferOffset(), NumIndicesPerInstance);
	}

	{
		// Update the primitive uniform buffer if needed.
		if (!WorldSpacePrimitiveUniformBuffer.IsInitialized())
		{
			FPrimitiveUniformShaderParameters PrimitiveUniformShaderParameters = GetPrimitiveUniformShaderParameters(
				FMatrix::Identity,
				FMatrix::Identity,
				SceneProxy->GetActorPosition(),
				SceneProxy->GetBounds(),
				SceneProxy->GetLocalBounds(),
				SceneProxy->ReceivesDecals(),
				false,
				false,
				false,
				false,
				SceneProxy->DrawsVelocity(),
				SceneProxy->GetLightingChannelMask(),
				0,
				INDEX_NONE,
				INDEX_NONE,
				SceneProxy->AlwaysHasVelocity()
				);
			WorldSpacePrimitiveUniformBuffer.SetContents(PrimitiveUniformShaderParameters);
			WorldSpacePrimitiveUniformBuffer.InitResource();
		}

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

				FNiagaraMeshCollectorResourcesMesh& CollectorResources = Collector.AllocateOneFrameResource<FNiagaraMeshCollectorResourcesMesh>();
				SetupVertexFactory(&CollectorResources.VertexFactory, LODModel);
				FNiagaraMeshUniformParameters PerViewUniformParameters;// = UniformParameters;
				PerViewUniformParameters.LocalToWorld = bLocalSpace ? SceneProxy->GetLocalToWorld() : FMatrix::Identity;//For now just handle local space like this but maybe in future have a VF variant to avoid the transform entirely?
				PerViewUniformParameters.LocalToWorldInverseTransposed = bLocalSpace ? SceneProxy->GetLocalToWorld().Inverse().GetTransposed() : FMatrix::Identity;
				PerViewUniformParameters.PrevTransformAvailable = false;
				PerViewUniformParameters.DeltaSeconds = ViewFamily.DeltaWorldTime;
				PerViewUniformParameters.PositionDataOffset = PositionOffset;
				PerViewUniformParameters.VelocityDataOffset = VelocityOffset;
				PerViewUniformParameters.ColorDataOffset = ColorOffset;
				PerViewUniformParameters.TransformDataOffset = TransformOffset;
				PerViewUniformParameters.ScaleDataOffset = ScaleOffset;
				PerViewUniformParameters.SizeDataOffset = SizeOffset;
				PerViewUniformParameters.MaterialParamValidMask = MaterialParamValidMask;
				PerViewUniformParameters.MaterialParamDataOffset = MaterialParamOffset;
				PerViewUniformParameters.MaterialParam1DataOffset = MaterialParamOffset1;
				PerViewUniformParameters.MaterialParam2DataOffset = MaterialParamOffset2;
				PerViewUniformParameters.MaterialParam3DataOffset = MaterialParamOffset3;
				PerViewUniformParameters.NormalizedAgeDataOffset = NormalizedAgeOffset;
				PerViewUniformParameters.MaterialRandomDataOffset = MaterialRandomOffset;
				PerViewUniformParameters.DefaultPos = bLocalSpace ? FVector4(0.0f, 0.0f, 0.0f, 1.0f) : FVector4(SceneProxy->GetLocalToWorld().GetOrigin());

				//Grab the material proxies we'll be using for each section and check them for translucency.
				TArray<FMaterialRenderProxy*, TInlineAllocator<32>> MaterialProxies;
				bool bHasTranslucentMaterials = false;
				for (FMaterialRenderProxy* MaterialProxy : DynamicDataMesh->Materials)
				{
					check(MaterialProxy);
					EBlendMode BlendMode = MaterialProxy->GetMaterial(SceneProxy->GetScene().GetFeatureLevel())->GetBlendMode();
					bHasTranslucentMaterials |= BlendMode == BLEND_AlphaComposite || BlendMode == BLEND_AlphaHoldout || BlendMode == BLEND_Translucent;
				}

				//Sort particles if needed.
				CollectorResources.VertexFactory.SetSortedIndices(nullptr, 0xFFFFFFFF);

				FNiagaraGPUSortInfo SortInfo;
				if (View && SortMode != ENiagaraSortMode::None && (bHasTranslucentMaterials || !bSortOnlyWhenTranslucent))
				{
					SortInfo.ParticleCount = NumInstances;
					SortInfo.SortMode = SortMode;
					SortInfo.SortAttributeOffset = (SortInfo.SortMode == ENiagaraSortMode::CustomAscending || SortInfo.SortMode == ENiagaraSortMode::CustomDecending) ? CustomSortingOffset : PositionOffset;
					SortInfo.ViewOrigin = View->ViewMatrices.GetViewOrigin();
					SortInfo.ViewDirection = View->GetViewDirection();
					if (bLocalSpace)
					{
						FMatrix InvTransform = SceneProxy->GetLocalToWorld().InverseFast();
						SortInfo.ViewOrigin = InvTransform.TransformPosition(SortInfo.ViewOrigin);
						SortInfo.ViewDirection = InvTransform.TransformVector(SortInfo.ViewDirection);
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
								CollectorResources.VertexFactory.SetSortedIndices(Batcher->GetGPUSortedBuffer().VertexBufferSRV, IndexBufferOffset);
							}
						}
						else
						{
							FGlobalDynamicReadBuffer::FAllocation SortedIndices;
							SortedIndices = DynamicReadBuffer.AllocateInt32(NumInstances);
							SortIndices(SortInfo.SortMode, SortInfo.SortAttributeOffset, *SourceParticleData, SceneProxy->GetLocalToWorld(), View, SortedIndices);
							CollectorResources.VertexFactory.SetSortedIndices(SortedIndices.ReadBuffer->SRV, SortedIndices.FirstIndex / sizeof(float));
						}
					}
					CollectorResources.VertexFactory.SetParticleData(ParticleData.ReadBuffer->SRV, ParticleData.FirstIndex / sizeof(float), SourceParticleData->GetFloatStride() / sizeof(float));
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
							CollectorResources.VertexFactory.SetSortedIndices(Batcher->GetGPUSortedBuffer().VertexBufferSRV, IndexBufferOffset);
						}
					}
					if (SourceParticleData->GetGPUBufferFloat().SRV.IsValid())
					{
						CollectorResources.VertexFactory.SetParticleData(SourceParticleData->GetGPUBufferFloat().SRV, 0, SourceParticleData->GetFloatStride() / sizeof(float));
					}
					else
					{
						CollectorResources.VertexFactory.SetParticleData(FNiagaraRenderer::GetDummyFloatBuffer().SRV, 0, 0);
					}
				}

				// Collector.AllocateOneFrameResource uses default ctor, initialize the vertex factory
				CollectorResources.VertexFactory.SetParticleFactoryType(NVFT_Mesh);
				CollectorResources.VertexFactory.SetMeshFacingMode((uint32)FacingMode);
				CollectorResources.UniformBuffer = FNiagaraMeshUniformBufferRef::CreateUniformBufferImmediate(PerViewUniformParameters, UniformBuffer_SingleFrame);

				CollectorResources.VertexFactory.InitResource();
				CollectorResources.VertexFactory.SetUniformBuffer(CollectorResources.UniformBuffer);
			
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
					Mesh.VertexFactory = &CollectorResources.VertexFactory;
					Mesh.LCI = NULL;
					Mesh.ReverseCulling = SceneProxy->IsLocalToWorldDeterminantNegative();
					Mesh.CastShadow = SceneProxy->CastsDynamicShadow();
					Mesh.DepthPriorityGroup = (ESceneDepthPriorityGroup)SceneProxy->GetDepthPriorityGroup(View);

					FMeshBatchElement& BatchElement = Mesh.Elements[0];
					BatchElement.PrimitiveUniformBuffer = WorldSpacePrimitiveUniformBuffer.GetUniformBufferRHI();
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

	CPUTimeMS += MeshElementsTimer.GetElapsedMilliseconds();
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

	SimpleTimer VertexDataTimer;

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

	CPUTimeMS = VertexDataTimer.GetElapsedMilliseconds();
	return DynamicData;  
}

int FNiagaraRendererMeshes::GetDynamicDataSize()const
{
	uint32 Size = sizeof(FNiagaraDynamicDataMesh);
	return Size;
}

void FNiagaraRendererMeshes::TransformChanged()
{
	WorldSpacePrimitiveUniformBuffer.ReleaseResource();
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