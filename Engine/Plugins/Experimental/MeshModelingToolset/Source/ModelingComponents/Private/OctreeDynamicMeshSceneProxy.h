// Copyright Epic Games, Inc. All Rights Reserved. 

#pragma once

#include "OctreeDynamicMeshComponent.h"
#include "Util/IndexSetDecompositions.h"


DECLARE_STATS_GROUP(TEXT("SculptToolOctree"), STATGROUP_SculptToolOctree, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("SculptToolOctree_UpdateExisting"), STAT_SculptToolOctree_UpdateExisting, STATGROUP_SculptToolOctree);
DECLARE_CYCLE_STAT(TEXT("SculptToolOctree_UpdateCutSet"), STAT_SculptToolOctree_UpdateCutSet, STATGROUP_SculptToolOctree);
DECLARE_CYCLE_STAT(TEXT("SculptToolOctree_CreateNew"), STAT_SculptToolOctree_CreateNew, STATGROUP_SculptToolOctree);
DECLARE_CYCLE_STAT(TEXT("SculptToolOctree_UpdateSpill"), STAT_SculptToolOctree_UpdateSpill, STATGROUP_SculptToolOctree);
DECLARE_CYCLE_STAT(TEXT("SculptToolOctree_UpdateFromDecomp"), STAT_SculptToolOctree_UpdateFromDecomp, STATGROUP_SculptToolOctree);
DECLARE_CYCLE_STAT(TEXT("SculptToolOctree_UpdateDecompDestroy"), STAT_SculptToolOctree_UpdateDecompDestroy, STATGROUP_SculptToolOctree);
DECLARE_CYCLE_STAT(TEXT("SculptToolOctree_UpdateDecompCreate"), STAT_SculptToolOctree_UpdateDecompCreate, STATGROUP_SculptToolOctree);
DECLARE_CYCLE_STAT(TEXT("SculptToolOctree_InitializeBufferFromOverlay"), STAT_SculptToolOctree_InitializeBufferFromOverlay, STATGROUP_SculptToolOctree);
DECLARE_CYCLE_STAT(TEXT("SculptToolOctree_BufferUpload"), STAT_SculptToolOctree_BufferUpload, STATGROUP_SculptToolOctree);

class FMeshRenderBufferSet
{
public:
	int TriangleCount = 0;

	/** The buffer containing vertex data. */
	FStaticMeshVertexBuffer StaticMeshVertexBuffer;
	/** The buffer containing the position vertex data. */
	FPositionVertexBuffer PositionVertexBuffer;
	/** The buffer containing the vertex color data. */
	FColorVertexBuffer ColorVertexBuffer;

	FDynamicMeshIndexBuffer32 IndexBuffer;
	FLocalVertexFactory VertexFactory;

	FMeshRenderBufferSet(ERHIFeatureLevel::Type FeatureLevelType)
		: VertexFactory(FeatureLevelType, "FMeshRenderBufferSet")
	{

	}


	virtual ~FMeshRenderBufferSet()
	{
		check(IsInRenderingThread());

		if (TriangleCount > 0)
		{
			PositionVertexBuffer.ReleaseResource();
			StaticMeshVertexBuffer.ReleaseResource();
			ColorVertexBuffer.ReleaseResource();
			IndexBuffer.ReleaseResource();
			VertexFactory.ReleaseResource();
		}
	}


	static void DestroyRenderBufferSet(FMeshRenderBufferSet* BufferSet)
	{
		if (BufferSet->TriangleCount == 0)
		{
			return;
		}

		ENQUEUE_RENDER_COMMAND(FMeshRenderBufferSetDestroy)(
			[BufferSet](FRHICommandListImmediate& RHICmdList)
		{
			delete BufferSet;
		});
	}

	void Upload()
	{
		check(IsInRenderingThread());

		if (TriangleCount == 0)
		{
			return;
		}

		InitOrUpdateResource(&this->PositionVertexBuffer);
		InitOrUpdateResource(&this->StaticMeshVertexBuffer);
		InitOrUpdateResource(&this->ColorVertexBuffer);

		FLocalVertexFactory::FDataType Data;
		this->PositionVertexBuffer.BindPositionVertexBuffer(&this->VertexFactory, Data);
		this->StaticMeshVertexBuffer.BindTangentVertexBuffer(&this->VertexFactory, Data);
		this->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(&this->VertexFactory, Data);
		//this->StaticMeshVertexBuffer.BindLightMapVertexBuffer(&this->VertexFactory, Data, LightMapIndex);
		this->ColorVertexBuffer.BindColorVertexBuffer(&this->VertexFactory, Data);
		this->VertexFactory.SetData(Data);

		InitOrUpdateResource(&this->VertexFactory);
		PositionVertexBuffer.InitResource();
		StaticMeshVertexBuffer.InitResource();
		ColorVertexBuffer.InitResource();
		IndexBuffer.InitResource();
		VertexFactory.InitResource();
	}

	// copied from StaticMesh.cpp
	void InitOrUpdateResource(FRenderResource* Resource)
	{
		check(IsInRenderingThread());

		if (!Resource->IsInitialized())
		{
			Resource->InitResource();
		}
		else
		{
			Resource->UpdateRHI();
		}
	}

};

/**
 * Scene Proxy for a mesh buffer.
 * 
 * Based on FProceduralMeshSceneProxy but simplified in various ways.
 * 
 * Supports wireframe-on-shaded rendering.
 * 
 */
class FOctreeDynamicMeshSceneProxy final : public FPrimitiveSceneProxy
{
private:
	UMaterialInterface* Material;

	FMaterialRelevance MaterialRelevance;

	TMap<int32, FMeshRenderBufferSet*> RenderBufferSets;

public:
	/** Component that created this proxy (is there a way to look this up?) */
	UOctreeDynamicMeshComponent* ParentComponent;


	FColor ConstantVertexColor = FColor::White;
	bool bIgnoreVertexColors = false;
	bool bIgnoreVertexNormals = false;


	bool bUsePerTriangleColor = false;
	TFunction<FColor(int)> PerTriangleColorFunc = nullptr;


	FOctreeDynamicMeshSceneProxy(UOctreeDynamicMeshComponent* Component)
		: FPrimitiveSceneProxy(Component)
		, MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
	{
		// This is an assumption we are currently making. We do not necessarily require this
		// but if this check is hit then possibly an assumption is wrong
		check(IsInGameThread());

		ParentComponent = Component;

		// Grab material
		Material = Component->GetMaterial(0);
		if (Material == NULL)
		{
			Material = UMaterial::GetDefaultMaterial(MD_Surface);
		}
	}


	virtual ~FOctreeDynamicMeshSceneProxy()
	{
		// we are assuming in code below that this is always called from the rendering thread
		check(IsInRenderingThread());

		for (auto MapPair : RenderBufferSets)
		{
			FMeshRenderBufferSet* BufferSet = MapPair.Value;
			FMeshRenderBufferSet::DestroyRenderBufferSet(BufferSet);
		}
		RenderBufferSets.Reset();
	}


	FMeshRenderBufferSet* AllocateNewRenderBufferSet()
	{
		FMeshRenderBufferSet* RenderBufferSet = new FMeshRenderBufferSet(GetScene().GetFeatureLevel());
		return RenderBufferSet;
	}



	virtual void InitializeSingleBuffer()
	{
			check(RenderBufferSets.Num() == 0);

			FDynamicMesh3* Mesh = ParentComponent->GetMesh();

			FMeshRenderBufferSet* RenderBuffers = AllocateNewRenderBufferSet();

			// find suitable overlays
			FDynamicMeshUVOverlay* UVOverlay = nullptr;
			FDynamicMeshNormalOverlay* NormalOverlay = nullptr;
			if (Mesh->HasAttributes())
			{
				UVOverlay = Mesh->Attributes()->PrimaryUV();
				NormalOverlay = Mesh->Attributes()->PrimaryNormals();
			}

			InitializeBuffersFromOverlays(Mesh,
				Mesh->TriangleCount(), Mesh->TriangleIndicesItr(),
				UVOverlay, NormalOverlay, RenderBuffers);

			ENQUEUE_RENDER_COMMAND(FOctreeDynamicMeshSceneProxyInitializeSingle)(
				[this, RenderBuffers](FRHICommandListImmediate& RHICmdList)
			{
				RenderBuffers->Upload();
				RenderBufferSets.Add(0, RenderBuffers);
			});
	}



	virtual void InitializeFromDecomposition(const FArrayIndexSetsDecomposition& Decomposition)
	{
		check(RenderBufferSets.Num() == 0);

		FDynamicMesh3* Mesh = ParentComponent->GetMesh();

		// find suitable overlays
		FDynamicMeshUVOverlay* UVOverlay = nullptr;
		FDynamicMeshNormalOverlay* NormalOverlay = nullptr;
		if (Mesh->HasAttributes())
		{
			UVOverlay = Mesh->Attributes()->PrimaryUV();
			NormalOverlay = Mesh->Attributes()->PrimaryNormals();
		}

		const TArray<int32>& SetIDs = Decomposition.GetIndexSetIDs();
		for (int32 SetID : SetIDs)
		{
			const TArray<int32>& Tris = Decomposition.GetIndexSetArray(SetID);
			
			FMeshRenderBufferSet* RenderBuffers = AllocateNewRenderBufferSet();

			InitializeBuffersFromOverlays(Mesh,
				Tris.Num(), Tris,
				UVOverlay, NormalOverlay, RenderBuffers);

			ENQUEUE_RENDER_COMMAND(FOctreeDynamicMeshSceneProxyInitializeFromDecomposition)(
				[this, SetID, RenderBuffers](FRHICommandListImmediate& RHICmdList)
			{
				RenderBuffers->Upload();
				RenderBufferSets.Add(SetID, RenderBuffers);
			});
		}
	}



	virtual void UpdateFromDecomposition(const FArrayIndexSetsDecomposition& Decomposition, const TArray<int32>& SetsToUpdate )
	{
		// CAN WE REUSE EXISTING BUFFER SETS??
		//   - could have timestamp for each decomposition set array...if tris don't change we only have to update vertices
		//   - can re-use allocated memory if new data is smaller

		SCOPE_CYCLE_COUNTER(STAT_SculptToolOctree_UpdateFromDecomp);

		// remove sets to update
		ENQUEUE_RENDER_COMMAND(FOctreeDynamicMeshSceneProxyUpdatePreClean)(
			[this, SetsToUpdate](FRHICommandListImmediate& RHICmdList)
		{
			SCOPE_CYCLE_COUNTER(STAT_SculptToolOctree_UpdateDecompDestroy);
			for (int32 SetID : SetsToUpdate)
			{
				if (RenderBufferSets.Contains(SetID))
				{
					FMeshRenderBufferSet* BufferSet = RenderBufferSets.FindAndRemoveChecked(SetID);
					FMeshRenderBufferSet::DestroyRenderBufferSet(BufferSet);
				}
			}
		});

		FDynamicMesh3* Mesh = ParentComponent->GetMesh();

		// find suitable overlays
		FDynamicMeshUVOverlay* UVOverlay = nullptr;
		FDynamicMeshNormalOverlay* NormalOverlay = nullptr;
		if (Mesh->HasAttributes())
		{
			UVOverlay = Mesh->Attributes()->PrimaryUV();
			NormalOverlay = Mesh->Attributes()->PrimaryNormals();
		}

		{
			FCriticalSection SectionLock;

			SCOPE_CYCLE_COUNTER(STAT_SculptToolOctree_UpdateDecompCreate);
			int NumSets = SetsToUpdate.Num();
			ParallelFor(NumSets, [&](int k)
			{
				int32 SetID = SetsToUpdate[k];
				const TArray<int32>& Tris = Decomposition.GetIndexSetArray(SetID);

				FMeshRenderBufferSet* RenderBuffers = AllocateNewRenderBufferSet();

				InitializeBuffersFromOverlays(Mesh,
					Tris.Num(), Tris,
					UVOverlay, NormalOverlay, RenderBuffers);

				ENQUEUE_RENDER_COMMAND(FOctreeDynamicMeshSceneProxyUpdateAddOne)(
					[this, SetID, RenderBuffers](FRHICommandListImmediate& RHICmdList)
				{
					SCOPE_CYCLE_COUNTER(STAT_SculptToolOctree_BufferUpload);
					RenderBuffers->Upload();
					RenderBufferSets.Add(SetID, RenderBuffers);
				});
			}, false);


		}

		//UE_LOG(LogTemp, Warning, TEXT("Have %d renderbuffers"), RenderBufferSets.Num());
	}



protected:


	/**
	 * Initialize rendering buffers from given attribute overlays.
	 * Creates three vertices per triangle, IE no shared vertices in buffers.
	 */
	template<typename TriangleEnumerable>
	void InitializeBuffersFromOverlays(const FDynamicMesh3* Mesh, 
		int NumTriangles, TriangleEnumerable Enumerable,
		FDynamicMeshUVOverlay* UVOverlay, 
		FDynamicMeshNormalOverlay* NormalOverlay,
		FMeshRenderBufferSet* RenderBuffers)
	{
		SCOPE_CYCLE_COUNTER(STAT_SculptToolOctree_InitializeBufferFromOverlay);

		RenderBuffers->TriangleCount = NumTriangles;
		if (NumTriangles == 0)
		{
			return;
		}

		bool bHaveColors = Mesh->HasVertexColors() && (bIgnoreVertexColors == false);

		int NumVertices = NumTriangles * 3;
		int NumTexCoords = 1;		// no! zero!

		{
			RenderBuffers->PositionVertexBuffer.Init(NumVertices);
			RenderBuffers->StaticMeshVertexBuffer.Init(NumVertices, NumTexCoords);
			RenderBuffers->ColorVertexBuffer.Init(NumVertices);
			RenderBuffers->IndexBuffer.Indices.AddUninitialized(NumTriangles * 3);
		}

		int TriIdx = 0, VertIdx = 0;
		FVector3f TangentX, TangentY;
		for (int TriangleID : Enumerable)
		{
			FIndex3i Tri = Mesh->GetTriangle(TriangleID);
			FIndex3i TriUV = (UVOverlay != nullptr) ? UVOverlay->GetTriangle(TriangleID) : FIndex3i::Zero();
			FIndex3i TriNormal = (NormalOverlay != nullptr) ? NormalOverlay->GetTriangle(TriangleID) : FIndex3i::Zero();

			FColor TriColor = ConstantVertexColor;
			if (bUsePerTriangleColor && PerTriangleColorFunc != nullptr)
			{
				TriColor = PerTriangleColorFunc(TriangleID);
				bHaveColors = false;
			}

			for (int j = 0; j < 3; ++j)
			{
				RenderBuffers->PositionVertexBuffer.VertexPosition(VertIdx) = (FVector)Mesh->GetVertex(Tri[j]);

				FVector3f Normal = (NormalOverlay != nullptr && TriNormal[j] != FDynamicMesh3::InvalidID) ? 
					NormalOverlay->GetElement(TriNormal[j]) : Mesh->GetVertexNormal(Tri[j]);

				// calculate a nonsense tangent
				VectorUtil::MakePerpVectors(Normal, TangentX, TangentY);
				RenderBuffers->StaticMeshVertexBuffer.SetVertexTangents(VertIdx, TangentX, TangentY, Normal);

				FVector2f UV = (UVOverlay != nullptr && TriUV[j] != FDynamicMesh3::InvalidID) ? 
					UVOverlay->GetElement(TriUV[j]) : FVector2f::Zero();
				RenderBuffers->StaticMeshVertexBuffer.SetVertexUV(VertIdx, 0, UV);

				RenderBuffers->ColorVertexBuffer.VertexColor(VertIdx) = (bHaveColors) ?
					(FColor)Mesh->GetVertexColor(Tri[j]) : TriColor;

				RenderBuffers->IndexBuffer.Indices[TriIdx++] = VertIdx;
				VertIdx++;
			}
		}
	}





public:

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_OctreeDynamicMeshSceneProxy_GetDynamicMeshElements);

		const bool bWireframe = (AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe)
			|| ParentComponent->bExplicitShowWireframe;

		FColoredMaterialRenderProxy* WireframeMaterialInstance = new FColoredMaterialRenderProxy(
			GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
			FLinearColor(0, 0.5f, 1.f)
		);

		Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);

		FMaterialRenderProxy* MaterialProxy = Material->GetRenderProxy();
		FMaterialRenderProxy* WireframeMaterialProxy = WireframeMaterialInstance;

		//ESceneDepthPriorityGroup DepthPriority = (ParentComponent->bDrawOnTop) ? SDPG_Foreground : SDPG_World;
		ESceneDepthPriorityGroup DepthPriority = SDPG_World;


		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];

				bool bHasPrecomputedVolumetricLightmap;
				FMatrix PreviousLocalToWorld;
				int32 SingleCaptureIndex;
				bool bOutputVelocity;
				GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);

				// Draw the mesh.
				for (auto MapPair : RenderBufferSets)
				{
					const FMeshRenderBufferSet& RenderBuffers = *MapPair.Value;
					if (RenderBuffers.TriangleCount == 0)
					{
						continue;
					}

					// do we need separate one of these for each MeshRenderBufferSet?
					FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
					DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, DrawsVelocity(), bOutputVelocity);

					DrawBatch(Collector, RenderBuffers, MaterialProxy, false, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
					if (bWireframe)
					{
						DrawBatch(Collector, RenderBuffers, WireframeMaterialProxy, true, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
					}
				}
			}
		}
	}



	virtual void DrawBatch(FMeshElementCollector& Collector, 
		const FMeshRenderBufferSet& RenderBuffers,
		FMaterialRenderProxy* UseMaterial, 
		bool bWireframe,
		ESceneDepthPriorityGroup DepthPriority,
		int ViewIndex,
		FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer) const
	{
		FMeshBatch& Mesh = Collector.AllocateMesh();
		FMeshBatchElement& BatchElement = Mesh.Elements[0];
		BatchElement.IndexBuffer = &RenderBuffers.IndexBuffer;
		Mesh.bWireframe = bWireframe;
		Mesh.VertexFactory = &RenderBuffers.VertexFactory;
		Mesh.MaterialRenderProxy = UseMaterial;

		BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

		BatchElement.FirstIndex = 0;
		BatchElement.NumPrimitives = RenderBuffers.IndexBuffer.Indices.Num() / 3;
		BatchElement.MinVertexIndex = 0;
		BatchElement.MaxVertexIndex = RenderBuffers.PositionVertexBuffer.GetNumVertices() - 1;
		Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
		Mesh.Type = PT_TriangleList;
		Mesh.DepthPriorityGroup = DepthPriority;
		Mesh.bCanApplyViewModeOverrides = false;
		Collector.AddMesh(ViewIndex, Mesh);
	}



	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;

		Result.bDrawRelevance = IsShown(View);
		Result.bShadowRelevance = IsShadowCast(View);
		Result.bDynamicRelevance = true;
		Result.bRenderInMainPass = ShouldRenderInMainPass();
		Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
		Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
		Result.bRenderCustomDepth = ShouldRenderCustomDepth();
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		Result.bVelocityRelevance = IsMovable() && Result.bOpaqueRelevance && Result.bRenderInMainPass;

		return Result;
	}


	virtual void GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const override
	{
		FPrimitiveSceneProxy::GetLightRelevance(LightSceneProxy, bDynamic, bRelevant, bLightMapped, bShadowMapped);
	}


	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}

	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }

	uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }


	virtual void SetMaterial(UMaterialInterface* MaterialIn)
	{
		this->Material = MaterialIn;
	}


	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}
};







