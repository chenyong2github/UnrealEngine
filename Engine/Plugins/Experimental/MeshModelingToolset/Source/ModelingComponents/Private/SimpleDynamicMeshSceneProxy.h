// Copyright Epic Games, Inc. All Rights Reserved. 

#pragma once

#include "SimpleDynamicMeshComponent.h"
#include "BaseDynamicMeshSceneProxy.h"
#include "Async/ParallelFor.h"


/**
 * Scene Proxy for USimpleDynamicMeshComponent.
 * 
 * Based on FProceduralMeshSceneProxy but simplified in various ways.
 * 
 * Supports wireframe-on-shaded rendering.
 * 
 */
class FSimpleDynamicMeshSceneProxy final : public FBaseDynamicMeshSceneProxy
{
private:
	FMaterialRelevance MaterialRelevance;

	// note: FBaseDynamicMeshSceneProxy owns and will destroy these
	TArray<FMeshRenderBufferSet*> RenderBufferSets;

	// if true, we store entire mesh in single RenderBuffers and we can do some optimizations
	bool bIsSingleBuffer = false;

public:
	/** Component that created this proxy (is there a way to look this up?) */
	USimpleDynamicMeshComponent* ParentComponent;


	FSimpleDynamicMeshSceneProxy(USimpleDynamicMeshComponent* Component)
		: FBaseDynamicMeshSceneProxy(Component)
		, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
	{
		ParentComponent = Component;
	}


	virtual void GetActiveRenderBufferSets(TArray<FMeshRenderBufferSet*>& Buffers) const override
	{
		Buffers = RenderBufferSets;
	}



	virtual void Initialize()
	{
		// allocate buffer sets based on materials
		check(RenderBufferSets.Num() == 0);
		int32 NumMaterials = GetNumMaterials();
		if (NumMaterials == 0)
		{
			RenderBufferSets.SetNum(1);
			RenderBufferSets[0] = AllocateNewRenderBufferSet();
			RenderBufferSets[0]->Material = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		else
		{
			RenderBufferSets.SetNum(NumMaterials);
			for (int32 k = 0; k < NumMaterials; ++k)
			{
				RenderBufferSets[k] = AllocateNewRenderBufferSet();
				RenderBufferSets[k]->Material = GetMaterial(k);
			}
		}


		FDynamicMesh3* Mesh = ParentComponent->GetMesh();
		if (Mesh->HasAttributes() && Mesh->Attributes()->HasMaterialID() && NumMaterials > 1)
		{
			bIsSingleBuffer = false;
			InitializeByMaterial(RenderBufferSets);
		}
		else
		{
			bIsSingleBuffer = true;
			InitializeSingleBufferSet(RenderBufferSets[0]);
		}
	}









	/**
	 * Initialize a single set of mesh buffers for the entire mesh
	 */
	virtual void InitializeSingleBufferSet(FMeshRenderBufferSet* RenderBuffers)
	{
		FDynamicMesh3* Mesh = ParentComponent->GetMesh();

		// find suitable overlays
		FDynamicMeshUVOverlay* UVOverlay = nullptr;
		FDynamicMeshNormalOverlay* NormalOverlay = nullptr;
		if (Mesh->HasAttributes())
		{
			UVOverlay = Mesh->Attributes()->PrimaryUV();
			NormalOverlay = Mesh->Attributes()->PrimaryNormals();
		}

		InitializeBuffersFromOverlays(RenderBuffers, Mesh,
			Mesh->TriangleCount(), Mesh->TriangleIndicesItr(),
			UVOverlay, NormalOverlay);

		ENQUEUE_RENDER_COMMAND(FOctreeDynamicMeshSceneProxyInitializeSingle)(
			[RenderBuffers](FRHICommandListImmediate& RHICmdList)
		{
			RenderBuffers->Upload();
		});
	}



	/**
	 * Initialize the mesh buffers, one per material
	 */
	virtual void InitializeByMaterial(TArray<FMeshRenderBufferSet*>& BufferSets)
	{
		FDynamicMesh3* Mesh = ParentComponent->GetMesh();
		check(Mesh->HasAttributes() && Mesh->Attributes()->HasMaterialID());

		// find suitable overlays
		FDynamicMeshUVOverlay* UVOverlay = Mesh->Attributes()->PrimaryUV();
		FDynamicMeshNormalOverlay* NormalOverlay = Mesh->Attributes()->PrimaryNormals();
		FDynamicMeshMaterialAttribute* MaterialID = Mesh->Attributes()->GetMaterialID();

		TFunction<void(int, int, int, FVector3f&, FVector3f&)> TangentsFunc = nullptr;
		FMeshTangentsf* Tangents = ParentComponent->GetTangents();
		if (Tangents != nullptr)
		{
			TangentsFunc = [Tangents](int VertexID, int TriangleID, int TriVtxIdx, FVector3f& TangentX, FVector3f& TangentY)
			{
				return Tangents->GetPerTriangleTangent(TriangleID, TriVtxIdx, TangentX, TangentY);
			};
		}

		// count number of triangles for each material (could do in parallel?)
		int NumMaterials = BufferSets.Num();
		TArray<FThreadSafeCounter> Counts;
		Counts.SetNum(NumMaterials);
		for (int k = 0; k < NumMaterials; ++k)
		{
			Counts[k].Reset();
		}
		ParallelFor(Mesh->MaxTriangleID(), [&](int tid)
		{
			int MatIdx;
			MaterialID->GetValue(tid, &MatIdx);
			if (MatIdx >= 0 && MatIdx < NumMaterials)
			{
				Counts[MatIdx].Increment();
			}
		});

		// find max count
		int32 MaxCount = 0;
		for (FThreadSafeCounter& Count : Counts)
		{
			MaxCount = FMath::Max(Count.GetValue(), MaxCount);
		}

		// init renderbuffers for each material
		// could do this in parallel but then we need to allocate separate triangle arrays...is it worth it?
		TArray<int> Triangles;
		Triangles.Reserve(MaxCount);
		for (int k = 0; k < NumMaterials; ++k)
		{
			if (Counts[k].GetValue() > 0)
			{
				FMeshRenderBufferSet* RenderBuffers = BufferSets[k];

				Triangles.Reset();
				for (int tid : Mesh->TriangleIndicesItr())
				{
					int MatIdx;
					MaterialID->GetValue(tid, &MatIdx);
					if (MatIdx == k)
					{
						Triangles.Add(tid);
					}
				}

				InitializeBuffersFromOverlays(RenderBuffers, Mesh,
					Triangles.Num(), Triangles,
					UVOverlay, NormalOverlay);

				RenderBuffers->Triangles = Triangles;

				ENQUEUE_RENDER_COMMAND(FOctreeDynamicMeshSceneProxyInitializeSingle)(
					[RenderBuffers](FRHICommandListImmediate& RHICmdList)
				{
					RenderBuffers->Upload();
				});
			}
		}
	}






	/**
	 * Update the vertex position/normal/color buffers
	 */
	virtual void FastUpdateVertices(bool bPositions, bool bNormals, bool bColors)
	{
		// This needs to be rewritten for split-by-material buffers.
		// Could store triangle set with each buffer, and then rebuild vtx buffer(s) as needed?

		FDynamicMesh3* Mesh = ParentComponent->GetMesh();

		// find suitable overlays
		FDynamicMeshNormalOverlay* NormalOverlay = nullptr;
		if (bNormals)
		{
			check(Mesh->HasAttributes());
			NormalOverlay = Mesh->Attributes()->PrimaryNormals();
		}

		if (bIsSingleBuffer)
		{
			check(RenderBufferSets.Num() == 1);
			FMeshRenderBufferSet* Buffers = RenderBufferSets[0];
			UpdateVertexBuffersFromOverlays(Buffers, Mesh,
				Mesh->TriangleCount(), Mesh->TriangleIndicesItr(),
				NormalOverlay,
				bPositions, bNormals, bColors);

			ENQUEUE_RENDER_COMMAND(FSimpleDynamicMeshSceneProxyFastUpdateVertices)(
				[Buffers, bPositions, bNormals, bColors](FRHICommandListImmediate& RHICmdList)
			{
				Buffers->UploadVertexUpdate(bPositions, bNormals, bColors);
			});
		}
		else
		{
			ParallelFor(RenderBufferSets.Num(), [&](int i)
			{
				FMeshRenderBufferSet* Buffers = RenderBufferSets[i];
				if (Buffers->TriangleCount == 0)
				{
					return;
				}
				check(Buffers->Triangles.IsSet());
				UpdateVertexBuffersFromOverlays(Buffers, Mesh,
					Buffers->Triangles->Num(), Buffers->Triangles.GetValue(),
					NormalOverlay,
					bPositions, bNormals, bColors);

				ENQUEUE_RENDER_COMMAND(FSimpleDynamicMeshSceneProxyFastUpdateVertices)(
					[Buffers, bPositions, bNormals, bColors](FRHICommandListImmediate& RHICmdList)
				{
					Buffers->UploadVertexUpdate(bPositions, bNormals, bColors);
				});
			});
		}

	}




public:



	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;

		if (ParentComponent->bDrawOnTop)
		{
			Result.bDrawRelevance = IsShown(View);
			Result.bDynamicRelevance = true;
			Result.bShadowRelevance = false;
			Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
			Result.bEditorNoDepthTestPrimitiveRelevance = true;
		}
		else
		{
			Result.bDrawRelevance = IsShown(View);
			Result.bShadowRelevance = IsShadowCast(View);
			Result.bDynamicRelevance = true;
			Result.bRenderInMainPass = ShouldRenderInMainPass();
			Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
			Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
			Result.bRenderCustomDepth = ShouldRenderCustomDepth();
			MaterialRelevance.SetPrimitiveViewRelevance(Result);
			Result.bVelocityRelevance = IsMovable() && Result.bOpaqueRelevance && Result.bRenderInMainPass;
		}

		return Result;
	}


	virtual void GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const override
	{
		if (ParentComponent->bDrawOnTop)
		{
			bDynamic = false;
			bRelevant = false;
			bLightMapped = false;
			bShadowMapped = false;
		}
		else
		{
			FPrimitiveSceneProxy::GetLightRelevance(LightSceneProxy, bDynamic, bRelevant, bLightMapped, bShadowMapped);
		}
	}


	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}

	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }

	uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }


	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}
};







