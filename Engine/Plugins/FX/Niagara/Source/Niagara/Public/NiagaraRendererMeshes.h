// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraRenderer.h: Base class for Niagara render modules
==============================================================================*/
#pragma once

#include "NiagaraRenderer.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraMeshVertexFactory.h"

class FNiagaraDataSet;
struct FNiagaraDynamicDataMesh;

/**
* NiagaraRendererSprites renders an FNiagaraEmitterInstance as sprite particles
*/
class NIAGARA_API FNiagaraRendererMeshes : public FNiagaraRenderer
{
public:
	FNiagaraRendererMeshes(ERHIFeatureLevel::Type FeatureLevel, const UNiagaraRendererProperties *InProps, const FNiagaraEmitterInstance* Emitter);
	~FNiagaraRendererMeshes();

	//FNiagaraRenderer Interface
	virtual void Initialize(const UNiagaraRendererProperties* InProps, const FNiagaraEmitterInstance* Emitter, const UNiagaraComponent* InComponent) override;
	virtual void ReleaseRenderThreadResources() override;

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector, const FNiagaraSceneProxy* SceneProxy) const override;
	virtual FNiagaraDynamicDataBase* GenerateDynamicData(const FNiagaraSceneProxy* Proxy, const UNiagaraRendererProperties* InProperties, const FNiagaraEmitterInstance* Emitter) const override;
	virtual int32 GetDynamicDataSize()const override;
	virtual bool IsMaterialValid(const UMaterialInterface* Mat)const override;
#if RHI_RAYTRACING
	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances, const FNiagaraSceneProxy* Proxy) final override;
#endif
	//FNiagaraRenderer Interface END

	void SetupVertexFactory(FNiagaraMeshVertexFactory& InVertexFactory, const FStaticMeshLODResources& LODResources) const;

protected:
	struct FParticleMeshRenderData
	{
		const FNiagaraDynamicDataMesh*	DynamicDataMesh = nullptr;
		class FNiagaraDataBuffer*		SourceParticleData = nullptr;

		bool							bHasTranslucentMaterials = false;
		bool							bSortCullOnGpu = false;
		bool							bNeedsSort = false;
		bool							bNeedsCull = false;

		const FNiagaraRendererLayout*	RendererLayout = nullptr;
		ENiagaraMeshVFLayout::Type		SortVariable;

		FRHIShaderResourceView*			ParticleFloatSRV = nullptr;
		FRHIShaderResourceView*			ParticleHalfSRV = nullptr;
		FRHIShaderResourceView*			ParticleIntSRV = nullptr;
		uint32							ParticleFloatDataStride = 0;
		uint32							ParticleHalfDataStride = 0;
		uint32							ParticleIntDataStride = 0;
		FRHIShaderResourceView*			ParticleSortedIndicesSRV = nullptr;
		uint32							ParticleSortedIndicesOffset = 0xffffffff;

		uint32							RendererVisTagOffset = INDEX_NONE;
		uint32							MeshIndexOffset = INDEX_NONE;

		FVector							WorldSpacePivotOffset = FVector::ZeroVector;
		FSphere							CullingSphere = FSphere(EForceInit::ForceInit);
	};

	struct FMeshData
	{
		FStaticMeshRenderData* RenderData = nullptr;
		int32 MinimumLOD = 0;
		uint32 SourceMeshIndex = INDEX_NONE;
		FVector PivotOffset = FVector(ForceInitToZero);
		ENiagaraMeshPivotOffsetSpace PivotOffsetSpace = ENiagaraMeshPivotOffsetSpace::Mesh;
		FVector Scale = FVector(1.0f, 1.0f, 1.0f);
		FSphere LocalCullingSphere = FSphere(ForceInitToZero);
		TArray<uint32, TInlineAllocator<4>> MaterialRemapTable;
	};

	class FMeshCollectorResourcesBase : public FOneFrameResource
	{
	public:
		FNiagaraMeshUniformBufferRef UniformBuffer;

		virtual ~FMeshCollectorResourcesBase() {}
		virtual FNiagaraMeshVertexFactory& GetVertexFactory() = 0;
	};

	template <typename TVertexFactory>
	class TMeshCollectorResources : public FMeshCollectorResourcesBase
	{
	public:
		TVertexFactory VertexFactory;

		virtual ~TMeshCollectorResources() { VertexFactory.ReleaseResource(); }
		virtual FNiagaraMeshVertexFactory& GetVertexFactory() override { return VertexFactory; }
	};

	using FMeshCollectorResources = TMeshCollectorResources<FNiagaraMeshVertexFactory>;
	using FMeshCollectorResourcesEx = TMeshCollectorResources<FNiagaraMeshVertexFactoryEx>;

	int32 GetLODIndex(int32 MeshIndex) const;

	void PrepareParticleMeshRenderData(FParticleMeshRenderData& ParticleMeshRenderData, FNiagaraDynamicDataBase* InDynamicData, const FNiagaraSceneProxy* SceneProxy) const;
	void PrepareParticleRenderBuffers(FParticleMeshRenderData& ParticleMeshRenderData, FGlobalDynamicReadBuffer& DynamicReadBuffer) const;
	void InitializeSortInfo(FParticleMeshRenderData& ParticleMeshRenderData, const FNiagaraSceneProxy& SceneProxy, const FSceneView& View, int32 ViewIndex, bool bIsInstancedStereo, FNiagaraGPUSortInfo& OutSortInfo) const;
	void PreparePerMeshData(FParticleMeshRenderData& ParticleMeshRenderData, const FNiagaraSceneProxy& SceneProxy, const FMeshData& MeshData) const;
	uint32 PerformSortAndCull(FParticleMeshRenderData& ParticleMeshRenderData, FGlobalDynamicReadBuffer& ReadBuffer, FNiagaraGPUSortInfo& SortInfo, NiagaraEmitterInstanceBatcher* Batcher, int32 MeshIndex) const;
	FNiagaraMeshUniformBufferRef CreatePerViewUniformBuffer(FParticleMeshRenderData& ParticleMeshRenderData, const FSceneView& View, const FMeshData& MeshData, const FNiagaraSceneProxy& SceneProxy) const;

	void CreateMeshBatchForSection(
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
	) const;

private:
	TArray<FMeshData, TInlineAllocator<1>> Meshes;

	ENiagaraRendererSourceDataMode SourceMode;
	ENiagaraSortMode SortMode;
	ENiagaraMeshFacingMode FacingMode;
	uint32 bOverrideMaterials : 1;
	uint32 bSortOnlyWhenTranslucent : 1;
	uint32 bGpuLowLatencyTranslucency : 1;
	uint32 bLockedAxisEnable : 1;
	uint32 bEnableCulling : 1;
	uint32 bEnableFrustumCulling : 1;
	uint32 bAccurateMotionVectors : 1;

	uint32 bSubImageBlend : 1;
	FVector2D SubImageSize;

	FVector LockedAxis;
	ENiagaraMeshLockedAxisSpace LockedAxisSpace;

	FVector2D DistanceCullRange;
	int32 RendererVisTagOffset;
	int32 RendererVisibility;
	int32 MeshIndexOffset;
	uint32 MaterialParamValidMask;
	uint32 MaxSectionCount;

	int32 VFBoundOffsetsInParamStore[ENiagaraMeshVFLayout::Type::Num_Max];
	uint32 bSetAnyBoundVars : 1;

	const FNiagaraRendererLayout* RendererLayoutWithCustomSorting;
	const FNiagaraRendererLayout* RendererLayoutWithoutCustomSorting;
};
