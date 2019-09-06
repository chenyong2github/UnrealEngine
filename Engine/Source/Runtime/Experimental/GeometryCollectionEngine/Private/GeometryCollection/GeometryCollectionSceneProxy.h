// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DynamicMeshBuilder.h"
#include "EngineGlobals.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "StaticMeshResources.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "GeometryCollectionRendering.h"
#include "GeometryCollection/GeometryCollectionEditorSelection.h"
#include "HitProxies.h"
#include "EngineUtils.h"

#if GEOMETRYCOLLECTION_EDITOR_SELECTION
#include "GeometryCollection/GeometryCollectionHitProxy.h"
#endif


class UGeometryCollectionComponent;
struct FGeometryCollectionSection;
struct HGeometryCollection;

/** Index Buffer */
class FGeometryCollectionIndexBuffer : public FIndexBuffer
{
public:
	virtual void InitRHI() override
	{
		FRHIResourceCreateInfo CreateInfo;
		IndexBufferRHI = RHICreateIndexBuffer(sizeof(int32), NumIndices * sizeof(int32), BUF_Dynamic, CreateInfo);
	}

	int32 NumIndices;
};

/** Vertex Buffer for Bone Map*/
class FGeometryCollectionBoneMapBuffer : public FVertexBuffer
{
public:
	virtual void InitRHI() override
	{
		FRHIResourceCreateInfo CreateInfo;

		// #note: Bone Map is stored in uint16, but shaders only support uint32
		VertexBufferRHI = RHICreateVertexBuffer(NumVertices * sizeof(uint32), BUF_Static | BUF_ShaderResource, CreateInfo);		
		VertexBufferSRV = RHICreateShaderResourceView(VertexBufferRHI, sizeof(uint32), PF_R32_UINT);		
	}

	int32 NumVertices;

	FShaderResourceViewRHIRef VertexBufferSRV;
};

/** Vertex Buffer for transform data */
class FGeometryCollectionTransformBuffer : public FVertexBuffer
{
public:
	virtual void InitRHI() override
	{
		FRHIResourceCreateInfo CreateInfo;

		// #note: This differs from instanced static mesh in that we are storing the entire transform in the buffer rather than
		// splitting out the translation.  This is to simplify transferring data at runtime as a memcopy
		VertexBufferRHI = RHICreateVertexBuffer(NumTransforms * sizeof(FVector4) * 4, BUF_Dynamic | BUF_ShaderResource, CreateInfo);		
		VertexBufferSRV = RHICreateShaderResourceView(VertexBufferRHI, 16, PF_A32B32G32R32F);
	}

	int32 NumTransforms;

	FShaderResourceViewRHIRef VertexBufferSRV;
};

/** Immutable rendering data (kind of) */
struct FGeometryCollectionConstantData
{
	TArray<FVector> Vertices;
	TArray<FIntVector> Indices;
	TArray<FVector> Normals;
	TArray<FVector> TangentU;
	TArray<FVector> TangentV;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<int32> BoneMap;
	TArray<FLinearColor> BoneColors;
	TArray<FGeometryCollectionSection> Sections;

	uint32 NumTransforms;

	FBox LocalBounds;
	
	TArray<FIntVector> OriginalMeshIndices;
	TArray<FGeometryCollectionSection> OriginalMeshSections;

	TArray<FMatrix> RestTransforms;
};

/** Mutable rendering data */
struct FGeometryCollectionDynamicData
{
	TArray<FMatrix> Transforms;
	TArray<FMatrix> PrevTransforms;
	bool IsDynamic;
	bool IsLoading;

	FGeometryCollectionDynamicData() : IsDynamic(false) {}
};

/***
*   FGeometryCollectionSceneProxy
*    
*	The FGeometryCollectionSceneProxy manages the interaction between the GeometryCollectionComponent
*   on the game thread and the vertex buffers on the render thread.
*
*   NOTE : This class is still in flux, and has a few pending todos. Your comments and 
*   thoughts are appreciated though. The remaining items to address involve:
*   - @todo double buffer - The double buffering of the FGeometryCollectionDynamicData.
*   - @todo previous state - Saving the previous FGeometryCollectionDynamicData for rendering motion blur.
*   - @todo shared memory model - The Asset(or Actor?) should hold the Vertex buffer, and pass the reference to the SceneProxy
*   - @todo GPU skin : Make the skinning use the GpuVertexShader
*/
class FGeometryCollectionSceneProxy final : public FPrimitiveSceneProxy
{
	TArray<UMaterialInterface*> Materials;

	FMaterialRelevance MaterialRelevance;

	int32 NumVertices;
	int32 NumIndices;

	FGeometryCollectionVertexFactory VertexFactory;
	
	bool bSupportsManualVertexFetch;
	
	FStaticMeshVertexBuffers VertexBuffers;
	FGeometryCollectionIndexBuffer IndexBuffer;
	FGeometryCollectionIndexBuffer OriginalMeshIndexBuffer;
	FGeometryCollectionBoneMapBuffer BoneMapBuffer;
	TArray<FGeometryCollectionTransformBuffer, TInlineAllocator<3>> TransformBuffers;
	TArray<FGeometryCollectionTransformBuffer, TInlineAllocator<3>> PrevTransformBuffers;

	int32 CurrentTransformBufferIndex = 0;

	TArray<FGeometryCollectionSection> Sections;
#if GEOMETRYCOLLECTION_EDITOR_SELECTION
	FColorVertexBuffer HitProxyIdBuffer;
	TArray<FGeometryCollectionSection> SubSections;
	TArray<TRefCountPtr<HGeometryCollection>> SubSectionHitProxies;
	TMap<int32, int32> SubSectionHitProxyIndexMap;
	// @todo FractureTools - Reconcile with SubSectionHitProxies.  Currently subsection hit proxies dont work for per-vertex submission
	TArray<TRefCountPtr<HGeometryCollectionBone>> PerBoneHitProxies;
	bool bUsesSubSections;
#endif

	FGeometryCollectionDynamicData* DynamicData;
	FGeometryCollectionConstantData* ConstantData;

	bool bShowBoneColors;
	bool bEnableBoneSelection;
	int BoneSelectionMaterialID;

	bool TransformVertexBuffersContainsOriginalMesh;

public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FGeometryCollectionSceneProxy(UGeometryCollectionComponent* Component);

	/** virtual destructor */
	virtual ~FGeometryCollectionSceneProxy();

	/** Current number of vertices to render */
	int32 GetRequiredVertexCount() const { return NumVertices; }

	/** Current number of indices to connect */
	int32 GetRequiredIndexCount() const { return NumIndices; }

	/** Called on render thread to setup static geometry for rendering */
	void SetConstantData_RenderThread(FGeometryCollectionConstantData* NewConstantData, bool ForceInit = false);

	/** Called on render thread to setup dynamic geometry for rendering */
	void SetDynamicData_RenderThread(FGeometryCollectionDynamicData* NewDynamicData);

	/** Called on render thread to construct the vertex definitions */
	void BuildGeometry(const FGeometryCollectionConstantData* ConstantDataIn, TArray<FDynamicMeshVertex>& OutVertices, TArray<int32>& OutIndices, TArray<int32> &OutOriginalMeshIndices);

	/** Called on render thread to setup dynamic geometry for rendering */
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

	/** Manage the view assignment */
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;

	// @todo allocated size : make this reflect internally allocated memory. 
	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }

	/** Size of the base class */
	uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }

	// FPrimitiveSceneProxy interface.
#if WITH_EDITOR
	virtual HHitProxy* CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy> >& OutHitProxies) override;
	virtual const FColorVertexBuffer* GetCustomHitProxyIdBuffer() const override { return bEnableBoneSelection ? &HitProxyIdBuffer : nullptr; }
#endif // WITH_EDITOR

#if GEOMETRYCOLLECTION_EDITOR_SELECTION
	/** Enable/disable the per transform selection mode. 
	 *  This forces more sections/mesh batches to be sent to the renderer while also allowing the editor
	 *  to return a special HitProxy containing the transform index of the section that has been clicked on.
	 */
	void UseSubSections(bool bInUsesSubSections, bool bForceInit);
#endif

protected:

	/** Create the rendering buffer resources */
	void InitResources();

	/** Return the rendering buffer resources */
	void ReleaseResources();

	/** Get material proxy from material ID */
	FMaterialRenderProxy* GetMaterial(FMeshElementCollector& Collector, int32 MaterialIndex) const;

	FGeometryCollectionTransformBuffer& GetCurrentTransformBuffer()
	{
		return TransformBuffers[CurrentTransformBufferIndex];
	}

	FGeometryCollectionTransformBuffer& GetCurrentPrevTransformBuffer()
	{
		return PrevTransformBuffers[CurrentTransformBufferIndex];
	}

	void CycleTransformBuffers(bool bCycle)
	{
		if (bCycle)
		{
			CurrentTransformBufferIndex = (CurrentTransformBufferIndex + 1) % TransformBuffers.Num();
		}
	}

private:
#if GEOMETRYCOLLECTION_EDITOR_SELECTION
	/** Create transform index based subsections for all current sections. */
	void InitializeSubSections_RenderThread();

	/** Release subsections by emptying the associated arrays. */
	void ReleaseSubSections_RenderThread();
#endif
};