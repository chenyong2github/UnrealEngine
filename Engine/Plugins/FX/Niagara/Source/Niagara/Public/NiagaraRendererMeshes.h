// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraRenderer.h: Base class for Niagara render modules
==============================================================================*/
#pragma once

#include "NiagaraMeshVertexFactory.h"
#include "NiagaraRenderer.h"
#include "NiagaraMeshRendererProperties.h"

class FNiagaraDataSet;

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

	void SetupVertexFactory(FNiagaraMeshVertexFactory *InVertexFactory, const FStaticMeshLODResources& LODResources) const;

protected:
	struct FParticleGPUBufferData
	{
		FRHIShaderResourceView* FloatSRV = nullptr;
		FRHIShaderResourceView* HalfSRV = nullptr;
		FRHIShaderResourceView* IntSRV = nullptr;
		uint32 FloatDataStride = 0;
		uint32 HalfDataStride = 0;
		uint32 IntDataStride = 0;
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

	virtual int32 GetMaxIndirectArgs() const override;
	int32 GetLODIndex(int32 MeshIndex) const;

	void PrepareParticleBuffers(
		FGlobalDynamicReadBuffer& DynamicReadBuffer,
		FNiagaraDataBuffer& SourceParticleData,
		const FNiagaraRendererLayout& RendererLayout,
		bool bDoGPUCulling,
		FParticleGPUBufferData& OutData,
		uint32& OutRendererVisTagOffset,
		uint32& OutFlipbookIndexOffset) const;

	FNiagaraMeshUniformBufferRef CreatePerViewUniformBuffer(
		const FMeshData& MeshData,
		const FNiagaraSceneProxy& SceneProxy,
		const FNiagaraRendererLayout& RendererLayout,
		const FSceneView& View,		
		const FParticleGPUBufferData& BufferData,
		FVector& OutWorldSpacePivotOffset,
		FSphere& OutCullingSphere) const;
		
	void InitializeSortInfo(
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
		uint32 FlipbookIdxOffset,
		FNiagaraGPUSortInfo& OutSortInfo) const;

	void CreateMeshBatchForSection(
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
		bool bDoGPUCulling) const;

private:	

	TArray<FMeshData, TInlineAllocator<1>> Meshes;

	ENiagaraSortMode SortMode;
	ENiagaraMeshFacingMode FacingMode;
	uint32 bOverrideMaterials : 1;
	uint32 bSortOnlyWhenTranslucent : 1;
	uint32 bLockedAxisEnable : 1;
	uint32 bEnableCulling : 1;
	uint32 bEnableFrustumCulling : 1;

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

	const FNiagaraRendererLayout* RendererLayoutWithCustomSorting;
	const FNiagaraRendererLayout* RendererLayoutWithoutCustomSorting;
};
