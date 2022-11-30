// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UniformBuffer.h"
#include "HitProxies.h"
#include "MaterialShared.h"
#include "Engine/Scene.h"
#include "PrimitiveUniformShaderParameters.h"
#include "VT/RuntimeVirtualTextureEnum.h"

#define USE_MESH_BATCH_VALIDATION !UE_BUILD_SHIPPING

class FLightCacheInterface;

enum EPrimitiveIdMode
{
	/** 
	 * PrimitiveId will be taken from the FPrimitiveSceneInfo corresponding to the FMeshBatch. 
	 * Primitive data will then be fetched by supporting VF's from the GPUScene persistent PrimitiveBuffer.
	 */
	PrimID_FromPrimitiveSceneInfo		= 0,

	/** 
     * The renderer will upload Primitive data from the FMeshBatchElement's PrimitiveUniformBufferResource to the end of the GPUScene PrimitiveBuffer, and assign the offset to DynamicPrimitiveShaderDataIndex.
	 * PrimitiveId for drawing will be computed as Scene->NumPrimitives + FMeshBatchElement's DynamicPrimitiveShaderDataIndex. 
	 */
	PrimID_DynamicPrimitiveShaderData	= 1,

	/** 
	 * PrimitiveId will always be 0.  Instancing not supported.  
	 * View.PrimitiveSceneDataOverrideSRV must be set in this configuration to control what the shader fetches at PrimitiveId == 0.
	 */
	PrimID_ForceZero					= 2,

	PrimID_Num							= 4,
	PrimID_NumBits						= 2,
};

/**
 * A batch mesh element definition.
 */
struct FMeshBatchElement
{
	/** 
	 * Primitive uniform buffer RHI
	 * Must be null for vertex factories that manually fetch primitive data from scene data, in which case FPrimitiveSceneProxy::UniformBuffer will be used.
	 */
	FRHIUniformBuffer* PrimitiveUniformBuffer;

	/** 
	 * Primitive uniform buffer to use for rendering, used when PrimitiveUniformBuffer is null. 
	 * This interface allows a FMeshBatchElement to be setup for a uniform buffer that has not been initialized yet, (TUniformBuffer* is known but not the FRHIUniformBuffer*)
	 */
	const TUniformBuffer<FPrimitiveUniformShaderParameters>* PrimitiveUniformBufferResource;

	const FIndexBuffer* IndexBuffer;

	union 
	{
		/** If !bIsSplineProxy, Instance runs, where number of runs is specified by NumInstances.  Run structure is [StartInstanceIndex, EndInstanceIndex]. */
		uint32* InstanceRuns;
		/** If bIsSplineProxy, a pointer back to the proxy */
		class FSplineMeshSceneProxy* SplineMeshSceneProxy;
	};
	const void* UserData;

	// Meaning depends on the vertex factory, e.g. FGPUSkinPassthroughVertexFactory: element index in FGPUSkinCache::CachedElements
	void* VertexFactoryUserData;

	FRHIVertexBuffer* IndirectArgsBuffer;
	uint32 IndirectArgsOffset;

	/** Assigned by renderer */
	EPrimitiveIdMode PrimitiveIdMode : PrimID_NumBits + 1;

	/** Assigned by renderer */
	uint32 DynamicPrimitiveShaderDataIndex : 24;

	uint32 FirstIndex;
	/** When 0, IndirectArgsBuffer will be used. */
	uint32 NumPrimitives;

	/** Number of instances to draw.  If InstanceRuns is valid, this is actually the number of runs in InstanceRuns. */
	uint32 NumInstances;
	uint32 BaseVertexIndex;
	uint32 MinVertexIndex;
	uint32 MaxVertexIndex;
	int32 UserIndex;
	float MinScreenSize;
	float MaxScreenSize;

	uint32 InstancedLODIndex : 4;
	uint32 InstancedLODRange : 4;
	uint32 bUserDataIsColorVertexBuffer : 1;
	uint32 bIsSplineProxy : 1;
	uint32 bIsInstanceRuns : 1;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	/** Conceptual element index used for debug viewmodes. */
	int32 VisualizeElementIndex : 8;
#endif

	FORCEINLINE int32 GetNumPrimitives() const
	{
		if (bIsInstanceRuns && InstanceRuns)
		{
			int32 Count = 0;
			for (uint32 Run = 0; Run < NumInstances; Run++)
			{
				Count += NumPrimitives * (InstanceRuns[Run * 2 + 1] - InstanceRuns[Run * 2] + 1);
			}
			return Count;
		}
		else
		{
			return NumPrimitives * NumInstances;
		}
	}

	FMeshBatchElement()
	:	PrimitiveUniformBuffer(nullptr)
	,	PrimitiveUniformBufferResource(nullptr)
	,	IndexBuffer(nullptr)
	,	InstanceRuns(nullptr)
	,	UserData(nullptr)
	,	VertexFactoryUserData(nullptr)
	,	IndirectArgsBuffer(nullptr)
	,	IndirectArgsOffset(0)
	,	PrimitiveIdMode(PrimID_FromPrimitiveSceneInfo)
	,	DynamicPrimitiveShaderDataIndex(0)
	,	NumInstances(1)
	,	BaseVertexIndex(0)
	,	UserIndex(-1)
	,	MinScreenSize(0.0f)
	,	MaxScreenSize(1.0f)
	,	InstancedLODIndex(0)
	,	InstancedLODRange(0)
	,	bUserDataIsColorVertexBuffer(false)
	,	bIsSplineProxy(false)
	,	bIsInstanceRuns(false)
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	,	VisualizeElementIndex(INDEX_NONE)
#endif
	{
	}
};

// Helper functions for hair strands shaders
ENGINE_API bool IsHairStrandsGeometrySupported(const EShaderPlatform Platform);
ENGINE_API bool IsCompatibleWithHairStrands(const FMaterial* Material, const ERHIFeatureLevel::Type FeatureLevel);
ENGINE_API bool IsCompatibleWithHairStrands(EShaderPlatform Platform, const FMaterialShaderParameters& Parameters);

/**
 * A batch of mesh elements, all with the same material and vertex buffer
 */
struct FMeshBatch
{
	TArray<FMeshBatchElement,TInlineAllocator<1> > Elements;

	/** Vertex factory for rendering, required. */
	const FVertexFactory* VertexFactory;

	/** Material proxy for rendering, required. */
	const FMaterialRenderProxy* MaterialRenderProxy;

	// can be NULL
	const FLightCacheInterface* LCI;

	/** The current hit proxy ID being rendered. */
	FHitProxyId BatchHitProxyId;

	/** This is the threshold that will be used to know if we should use this mesh batch or use one with no tessellation enabled */
	float TessellationDisablingShadowMapMeshSize;

	/* Mesh Id in a primitive. Used for stable sorting of draws belonging to the same primitive. **/
	uint16 MeshIdInPrimitive;

	/* Used to for stable sorting of draws belonging to the same batch Id. */
	uint16 InstancingBatchId : 12;
	uint16 InstancingBatchVertexStreamIndex : 4;

	/** LOD index of the mesh, used for fading LOD transitions. */
	int8 LODIndex;
	uint8 SegmentIndex;

	uint32 ReverseCulling : 1;
	uint32 bDisableBackfaceCulling : 1;

	/** 
	 * Pass feature relevance flags.  Allows a proxy to submit fast representations for passes which can take advantage of it, 
	 * for example separate index buffer for depth-only rendering since vertices can be merged based on position and ignore UV differences.
	 */
	uint32 CastShadow		: 1;	// Whether it can be used in shadow renderpasses.
	uint32 bUseForMaterial	: 1;	// Whether it can be used in renderpasses requiring material outputs.
	uint32 bUseForDepthPass : 1;	// Whether it can be used in depth pass.
	uint32 bUseAsOccluder	: 1;	// Hint whether this mesh is a good occluder.
	uint32 bWireframe		: 1;
	// e.g. PT_TriangleList(default), PT_LineList, ..
	uint32 Type : PT_NumBits;
	// e.g. SDPG_World (default), SDPG_Foreground
	uint32 DepthPriorityGroup : SDPG_NumBits;

	/** Whether view mode overrides can be applied to this mesh eg unlit, wireframe. */
	uint32 bCanApplyViewModeOverrides : 1;

	/** 
	 * Whether to treat the batch as selected in special viewmodes like wireframe. 
	 * This is needed instead of just Proxy->IsSelected() because some proxies do weird things with selection highlighting, like FStaticMeshSceneProxy.
	 */
	uint32 bUseWireframeSelectionColoring : 1;

	/** 
	 * Whether the batch should receive the selection outline.  
	 * This is useful for proxies which support selection on a per-mesh batch basis.
	 * They submit multiple mesh batches when selected, some of which have bUseSelectionOutline enabled.
	 */
	uint32 bUseSelectionOutline : 1;

	/** Whether the mesh batch can be selected through editor selection, aka hit proxies. */
	uint32 bSelectable : 1;
	
	/** Whether the mesh batch should apply dithered LOD. */
	uint32 bDitheredLODTransition : 1;

	/** Whether the mesh batch can be rendered to virtual textures. */
	uint32 bRenderToVirtualTexture : 1;
	/** What virtual texture material type this mesh batch should be rendered with. */
	uint32 RuntimeVirtualTextureMaterialType : RuntimeVirtualTexture::MaterialType_NumBits;

#if RHI_RAYTRACING
	uint32 CastRayTracedShadow : 1;	// Whether it casts ray traced shadow.
#endif

#if (!(UE_BUILD_SHIPPING || UE_BUILD_TEST) || WITH_EDITOR)
	/** Conceptual HLOD index used for the HLOD Coloration visualization. */
	int8 VisualizeHLODIndex;
#endif

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	/** Conceptual LOD index used for the LOD Coloration visualization. */
	int8 VisualizeLODIndex;
#endif

	FORCEINLINE bool IsTranslucent(ERHIFeatureLevel::Type InFeatureLevel) const
	{
		// Note: blend mode does not depend on the feature level we are actually rendering in.
		return IsTranslucentBlendMode(MaterialRenderProxy->GetMaterial(InFeatureLevel)->GetBlendMode());
	}

	// todo: can be optimized with a single function that returns multiple states (Translucent, Decal, Masked) 
	FORCEINLINE bool IsDecal(ERHIFeatureLevel::Type InFeatureLevel) const
	{
		// Note: does not depend on the feature level we are actually rendering in.
		const FMaterial* Mat = MaterialRenderProxy->GetMaterial(InFeatureLevel);

		return Mat->IsDeferredDecal();
	}

	FORCEINLINE bool IsDualBlend(ERHIFeatureLevel::Type InFeatureLevel) const
	{
		const FMaterial* Mat = MaterialRenderProxy->GetMaterial(InFeatureLevel);

		return Mat->IsDualBlendingEnabled(GShaderPlatformForFeatureLevel[InFeatureLevel]);
	}

	FORCEINLINE bool UseForHairStrands(ERHIFeatureLevel::Type InFeatureLevel) const
	{
		if (ERHIFeatureLevel::SM5 != InFeatureLevel)
			return false;

		const FMaterial* Mat = MaterialRenderProxy->GetMaterial(InFeatureLevel);
		return IsCompatibleWithHairStrands(Mat, InFeatureLevel);
	}

	FORCEINLINE bool IsMasked(ERHIFeatureLevel::Type InFeatureLevel) const
	{
		// Note: blend mode does not depend on the feature level we are actually rendering in.
		return MaterialRenderProxy->GetMaterial(InFeatureLevel)->IsMasked();
	}

	/** Converts from an int32 index into a int8 */
	static int8 QuantizeLODIndex(int32 NewLODIndex)
	{
		checkSlow(NewLODIndex >= SCHAR_MIN && NewLODIndex <= SCHAR_MAX);
		return (int8)NewLODIndex;
	}

	FORCEINLINE int32 GetNumPrimitives() const
	{
		int32 Count = 0;
		for (int32 ElementIdx = 0; ElementIdx < Elements.Num(); ++ElementIdx)
		{
			Count += Elements[ElementIdx].GetNumPrimitives();
		}
		return Count;
	}

	FORCEINLINE bool HasAnyDrawCalls() const
	{
		for (int32 ElementIdx = 0; ElementIdx < Elements.Num(); ++ElementIdx)
		{
			if (Elements[ElementIdx].GetNumPrimitives() > 0 || Elements[ElementIdx].IndirectArgsBuffer)
			{
				return true;
			}
		}
		return false;
	}

	ENGINE_API void PreparePrimitiveUniformBuffer(const FPrimitiveSceneProxy* PrimitiveSceneProxy, ERHIFeatureLevel::Type FeatureLevel);

#if USE_MESH_BATCH_VALIDATION
	ENGINE_API bool Validate(const FPrimitiveSceneProxy* SceneProxy, ERHIFeatureLevel::Type FeatureLevel) const;
#else
	FORCEINLINE bool Validate(const FPrimitiveSceneProxy* SceneProxy, ERHIFeatureLevel::Type FeatureLevel) const { return true; }
#endif

	/** Default constructor. */
	FMeshBatch()
	:	VertexFactory(nullptr)
	,	MaterialRenderProxy(nullptr)
	,	LCI(nullptr)
	,	TessellationDisablingShadowMapMeshSize(0.0f)
	,	MeshIdInPrimitive(0)
	,   InstancingBatchId(0)
	,   InstancingBatchVertexStreamIndex(0xF)
	,	LODIndex(INDEX_NONE)
	,	SegmentIndex(0xFF)
	,	ReverseCulling(false)
	,	bDisableBackfaceCulling(false)
	,	CastShadow(true)
	,   bUseForMaterial(true)
	,	bUseForDepthPass(true)
	,	bUseAsOccluder(true)
	,	bWireframe(false)
	,	Type(PT_TriangleList)
	,	DepthPriorityGroup(SDPG_World)
	,	bCanApplyViewModeOverrides(false)
	,	bUseWireframeSelectionColoring(false)
	,	bUseSelectionOutline(true)
	,	bSelectable(true)
	,	bDitheredLODTransition(false)
	,	bRenderToVirtualTexture(false)
	,	RuntimeVirtualTextureMaterialType(0)
#if RHI_RAYTRACING
	,	CastRayTracedShadow(true)
#endif
#if (!(UE_BUILD_SHIPPING || UE_BUILD_TEST) || WITH_EDITOR)
	,	VisualizeHLODIndex(INDEX_NONE)
#endif
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	,	VisualizeLODIndex(INDEX_NONE)
#endif
	{
		// By default always add the first element.
		new(Elements) FMeshBatchElement;
	}
};

struct FUniformBufferValue
{
	const FShaderParametersMetadata* Type = nullptr;
	FRHIUniformBuffer* UniformBuffer;
};




