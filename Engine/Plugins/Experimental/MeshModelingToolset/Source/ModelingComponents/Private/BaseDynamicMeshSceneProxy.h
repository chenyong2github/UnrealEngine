// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved. 

#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"
#include "VertexFactory.h"
#include "LocalVertexFactory.h"
#include "PrimitiveViewRelevance.h"
#include "BaseDynamicMeshComponent.h"


/**
 * FMeshRenderBufferSet stores a set of RenderBuffers for a mesh
 */
class FMeshRenderBufferSet
{
public:
	/** Number of triangles in this renderbuffer set. Note that triangles may be split between IndexBuffer and SecondaryIndexBuffer. */
	int TriangleCount = 0;

	/** The buffer containing vertex data. */
	FStaticMeshVertexBuffer StaticMeshVertexBuffer;
	/** The buffer containing the position vertex data. */
	FPositionVertexBuffer PositionVertexBuffer;
	/** The buffer containing the vertex color data. */
	FColorVertexBuffer ColorVertexBuffer;

	/** triangle indices */
	FDynamicMeshIndexBuffer32 IndexBuffer;

	/** vertex factory */
	FLocalVertexFactory VertexFactory;

	/** Material to draw this mesh with */
	UMaterialInterface* Material = nullptr;

	/**
	 * Optional list of triangles stored in this buffer. Storing this allows us
	 * to rebuild the buffers if vertex data changes.
	 */
	TOptional<TArray<int>> Triangles;

	/**
	 * If secondary index buffer is enabled, we populate this index buffer with additional triangles indexing into the same vertex buffers
	 */
	bool bEnableSecondaryIndexBuffer = false;

	/**
	 * partition or subset of IndexBuffer that indexes into same vertex buffers
	 */
	FDynamicMeshIndexBuffer32 SecondaryIndexBuffer;


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
			VertexFactory.ReleaseResource();
			if (IndexBuffer.IsInitialized())
			{
				IndexBuffer.ReleaseResource();
			}
			if (SecondaryIndexBuffer.IsInitialized())
			{
				SecondaryIndexBuffer.ReleaseResource();
			}
		}
	}


	/**
	 * Upload initialized mesh buffers. 
	 * @warning This can only be called on the Rendering Thread.
	 */
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
		// currently no lightmaps support
		//this->StaticMeshVertexBuffer.BindLightMapVertexBuffer(&this->VertexFactory, Data, LightMapIndex);
		this->ColorVertexBuffer.BindColorVertexBuffer(&this->VertexFactory, Data);
		this->VertexFactory.SetData(Data);

		InitOrUpdateResource(&this->VertexFactory);
		PositionVertexBuffer.InitResource();
		StaticMeshVertexBuffer.InitResource();
		ColorVertexBuffer.InitResource();
		VertexFactory.InitResource();

		if (IndexBuffer.Indices.Num() > 0)
		{
			IndexBuffer.InitResource();
		}
		if (bEnableSecondaryIndexBuffer && SecondaryIndexBuffer.Indices.Num() > 0)
		{
			SecondaryIndexBuffer.InitResource();
		}
	}



	void UploadVertexUpdate(bool bPositions, bool bNormals, bool bColors)
	{
		check(IsInRenderingThread());

		if (TriangleCount == 0)
		{
			return;
		}

		if (bPositions)
		{
			InitOrUpdateResource(&this->PositionVertexBuffer);
		}
		if (bNormals)
		{
			InitOrUpdateResource(&this->StaticMeshVertexBuffer);
		}
		if (bColors)
		{
			InitOrUpdateResource(&this->ColorVertexBuffer);
		}

		FLocalVertexFactory::FDataType Data;
		this->PositionVertexBuffer.BindPositionVertexBuffer(&this->VertexFactory, Data);
		this->StaticMeshVertexBuffer.BindTangentVertexBuffer(&this->VertexFactory, Data);
		this->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(&this->VertexFactory, Data);
		// currently no lightmaps support
		//this->StaticMeshVertexBuffer.BindLightMapVertexBuffer(&this->VertexFactory, Data, LightMapIndex);
		this->ColorVertexBuffer.BindColorVertexBuffer(&this->VertexFactory, Data);
		this->VertexFactory.SetData(Data);

		InitOrUpdateResource(&this->VertexFactory);
	}




	/**
	 * Initializes a render resource, or update it if already initialized.
	 * @wearning This function can only be called on the Render Thread
	 */
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




protected:
	friend class FBaseDynamicMeshSceneProxy;

	/**
	 * Enqueue a command on the Render Thread to destroy the passed in buffer set.
	 * At this point the buffer set should be considered invalid.
	 */
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


};




/**
 * FBaseDynamicMeshSceneProxy is an abstract base class for a Render Proxy
 * for a UBaseDynamicMeshComponent, where the assumption is that mesh data
 * will be stored in FMeshRenderBufferSet instances
 */
class FBaseDynamicMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	UBaseDynamicMeshComponent* ParentBaseComponent;

	/**
	 * Constant color assigned to vertices if no other vertex color is specified
	 */
	FColor ConstantVertexColor = FColor::White;

	/**
	 * If true, vertex colors on the FDynamicMesh will be ignored
	 */
	bool bIgnoreVertexColors = false;

	/**
	 * If true, a per-triangle color is used to set vertex colors
	 */
	bool bUsePerTriangleColor = false;

	/**
	 * Per-triangle color function. Only called if bUsePerTriangleColor=true
	 */
	TFunction<FColor(const FDynamicMesh3*, int)> PerTriangleColorFunc = nullptr;


	/**
	 * If true, populate secondary buffers using SecondaryTriFilterFunc
	 */
	bool bUseSecondaryTriBuffers = false;

	/**
	 * Filter predicate for secondary triangle index buffer. Only called if bUseSecondaryTriBuffers=true
	 */
	TUniqueFunction<bool(const FDynamicMesh3*, int32)> SecondaryTriFilterFunc = nullptr;

protected:
	// Set of currently-allocated RenderBuffers. We own these pointers and must clean them up.
	// Must guard access with AllocatedSetsLock!!
	TSet<FMeshRenderBufferSet*> AllocatedBufferSets;

	// use to control access to AllocatedBufferSets 
	FCriticalSection AllocatedSetsLock;

public:
	FBaseDynamicMeshSceneProxy(UBaseDynamicMeshComponent* Component)
		: FPrimitiveSceneProxy(Component),
		ParentBaseComponent(Component)
	{
	}


	virtual ~FBaseDynamicMeshSceneProxy()
	{
		// we are assuming in code below that this is always called from the rendering thread
		check(IsInRenderingThread());

		// destroy all existing renderbuffers
		for (FMeshRenderBufferSet* BufferSet : AllocatedBufferSets)
		{
			FMeshRenderBufferSet::DestroyRenderBufferSet(BufferSet);
		}
	}


	//
	// FBaseDynamicMeshSceneProxy API - subclasses must implement these functions
	//


	/**
	 * Return set of active renderbuffers. Must be implemented by subclass.
	 * This is the set of render buffers that will be drawn by GetDynamicMeshElements
	 */
	virtual void GetActiveRenderBufferSets(TArray<FMeshRenderBufferSet*>& Buffers) const = 0;



	//
	// RenderBuffer management
	//


	/**
	 * Allocates a set of render buffers. FPrimitiveSceneProxy will keep track of these
	 * buffers and destroy them on destruction.
	 */
	virtual FMeshRenderBufferSet* AllocateNewRenderBufferSet()
	{
		// should we hang onto these and destroy them in constructor? leaving to subclass seems risky?
		FMeshRenderBufferSet* RenderBufferSet = new FMeshRenderBufferSet(GetScene().GetFeatureLevel());

		RenderBufferSet->Material = UMaterial::GetDefaultMaterial(MD_Surface);

		AllocatedSetsLock.Lock();
		AllocatedBufferSets.Add(RenderBufferSet);
		AllocatedSetsLock.Unlock();

		return RenderBufferSet;
	}

	/**
	 * Explicitly release a set of RenderBuffers
	 */
	virtual void ReleaseRenderBufferSet(FMeshRenderBufferSet* BufferSet)
	{
		AllocatedSetsLock.Lock();
		check(AllocatedBufferSets.Contains(BufferSet));
		AllocatedBufferSets.Remove(BufferSet);
		AllocatedSetsLock.Unlock();

		FMeshRenderBufferSet::DestroyRenderBufferSet(BufferSet);
	}


	/**
	 * Initialize rendering buffers from given attribute overlays.
	 * Creates three vertices per triangle, IE no shared vertices in buffers.
	 */
	template<typename TriangleEnumerable>
	void InitializeBuffersFromOverlays(
		FMeshRenderBufferSet* RenderBuffers,
		const FDynamicMesh3* Mesh,
		int NumTriangles, TriangleEnumerable Enumerable,
		FDynamicMeshUVOverlay* UVOverlay,
		FDynamicMeshNormalOverlay* NormalOverlay,
		TFunction<void(int, int, int, FVector3f&, FVector3f&)> TangentsFunc = nullptr,
		bool bTrackTriangles = false)
	{
		//SCOPE_CYCLE_COUNTER(STAT_SculptToolOctree_InitializeBufferFromOverlay);

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

		// build triangle list if requested, or if we are using secondary buffers in which case we need it to filter later
		bool bBuildTriangleList = bTrackTriangles || bUseSecondaryTriBuffers;
		if (bBuildTriangleList)
		{
			RenderBuffers->Triangles = TArray<int32>();
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
				TriColor = PerTriangleColorFunc(Mesh, TriangleID);
				bHaveColors = false;
			}

			for (int j = 0; j < 3; ++j)
			{
				RenderBuffers->PositionVertexBuffer.VertexPosition(VertIdx) = (FVector)Mesh->GetVertex(Tri[j]);

				FVector3f Normal = (NormalOverlay != nullptr && TriNormal[j] != FDynamicMesh3::InvalidID) ?
					NormalOverlay->GetElement(TriNormal[j]) : Mesh->GetVertexNormal(Tri[j]);

				// either request known tangent, or calculate a nonsense one
				if (TangentsFunc != nullptr)
				{
					TangentsFunc(Tri[j], TriangleID, j, TangentX, TangentY);
				}
				else
				{
					VectorUtil::MakePerpVectors(Normal, TangentX, TangentY);
				}
				RenderBuffers->StaticMeshVertexBuffer.SetVertexTangents(VertIdx, (FVector)TangentX, (FVector)TangentY, (FVector)Normal);

				FVector2f UV = (UVOverlay != nullptr && TriUV[j] != FDynamicMesh3::InvalidID) ?
					UVOverlay->GetElement(TriUV[j]) : FVector2f::Zero();
				RenderBuffers->StaticMeshVertexBuffer.SetVertexUV(VertIdx, 0, (FVector2D)UV);

				RenderBuffers->ColorVertexBuffer.VertexColor(VertIdx) = (bHaveColors) ?
					(FColor)Mesh->GetVertexColor(Tri[j]) : TriColor;

				RenderBuffers->IndexBuffer.Indices[TriIdx++] = VertIdx;		// currently TriIdx == VertIdx so we don't really need both...
				VertIdx++;
			}

			if (bBuildTriangleList)
			{
				RenderBuffers->Triangles->Add(TriangleID);
			}
		}

		// split triangles into secondary buffer (at bit redudant since we just built IndexBuffer, but we may optionally duplicate triangles in the future
		if (bUseSecondaryTriBuffers)
		{
			RenderBuffers->bEnableSecondaryIndexBuffer = true;
			UpdateSecondaryTriangleBuffer(RenderBuffers, Mesh, false);
		}
	}


	/**
	 * Filter the triangles in a FMeshRenderBufferSet into the SecondaryIndexBuffer.
	 * Requires that RenderBuffers->Triangles has been initialized.
	 * @param bDuplicate if set, then primary IndexBuffer is unmodified and SecondaryIndexBuffer contains duplicates. Otherwise triangles are sorted via predicate into either primary or secondary.
	 */
	void UpdateSecondaryTriangleBuffer(
		FMeshRenderBufferSet* RenderBuffers,
		const FDynamicMesh3* Mesh,
		bool bDuplicate)
	{
		check(bUseSecondaryTriBuffers == true);
		check(RenderBuffers->Triangles.IsSet());

		const TArray<int32>& TriangleIDs = RenderBuffers->Triangles.GetValue();
		int NumTris = TriangleIDs.Num();
		TArray<uint32>& Indices = RenderBuffers->IndexBuffer.Indices;
		TArray<uint32>& SecondaryIndices = RenderBuffers->SecondaryIndexBuffer.Indices;

		RenderBuffers->SecondaryIndexBuffer.Indices.Reset();
		if (bDuplicate == false)
		{
			RenderBuffers->IndexBuffer.Indices.Reset();
		}
		for ( int k = 0; k < NumTris; ++k)
		{
			int TriangleID = TriangleIDs[k];
			bool bInclude = SecondaryTriFilterFunc(Mesh, TriangleID);
			if (bInclude)
			{
				SecondaryIndices.Add(3*k);
				SecondaryIndices.Add(3*k + 1);
				SecondaryIndices.Add(3*k + 2);
			} 
			else if (bDuplicate == false)
			{
				Indices.Add(3*k);
				Indices.Add(3*k + 1);
				Indices.Add(3*k + 2);
			}
		}
	}


	/**
	 * Update vertex positions/normals/colors of an existing set of render buffers.
	 * Assumes that buffers were created with unshared vertices, ie three vertices per triangle, eg by InitializeBuffersFromOverlays()
	 */
	template<typename TriangleEnumerable>
	void UpdateVertexBuffersFromOverlays(
		FMeshRenderBufferSet* RenderBuffers,
		const FDynamicMesh3* Mesh,
		int NumTriangles, TriangleEnumerable Enumerable,
		FDynamicMeshNormalOverlay* NormalOverlay,
		bool bUpdatePositions = true,
		bool bUpdateNormals = false,
		bool bUpdateColors = false)
	{
		if (RenderBuffers->TriangleCount == 0)
		{
			return;
		}

		bool bHaveColors = Mesh->HasVertexColors() && (bIgnoreVertexColors == false);

		int NumVertices = NumTriangles * 3;
		check(RenderBuffers->PositionVertexBuffer.GetNumVertices() == NumVertices);
		if (bUpdateNormals)
		{
			check(RenderBuffers->StaticMeshVertexBuffer.GetNumVertices() == NumVertices);
		}
		if (bUpdateColors)
		{
			check(RenderBuffers->ColorVertexBuffer.GetNumVertices() == NumVertices);
		}

		int VertIdx = 0;
		FVector3f TangentX, TangentY;
		for (int TriangleID : Enumerable)
		{
			FIndex3i Tri = Mesh->GetTriangle(TriangleID);

			FIndex3i TriNormal = (bUpdateColors && NormalOverlay != nullptr) ? NormalOverlay->GetTriangle(TriangleID) : FIndex3i::Zero();

			FColor TriColor = ConstantVertexColor;
			if (bUpdateColors && bUsePerTriangleColor && PerTriangleColorFunc != nullptr)
			{
				TriColor = PerTriangleColorFunc(Mesh, TriangleID);
				bHaveColors = false;
			}

			for (int j = 0; j < 3; ++j)
			{
				if (bUpdatePositions)
				{
					RenderBuffers->PositionVertexBuffer.VertexPosition(VertIdx) = (FVector)Mesh->GetVertex(Tri[j]);
				}

				if (bUpdateNormals)
				{
					FVector3f Normal = (NormalOverlay != nullptr && TriNormal[j] != FDynamicMesh3::InvalidID) ?
						NormalOverlay->GetElement(TriNormal[j]) : Mesh->GetVertexNormal(Tri[j]);

					// calculate a nonsense tangent
					VectorUtil::MakePerpVectors(Normal, TangentX, TangentY);
					RenderBuffers->StaticMeshVertexBuffer.SetVertexTangents(VertIdx, (FVector)TangentX, (FVector)TangentY, (FVector)Normal);
				}

				if (bUpdateColors)
				{
					RenderBuffers->ColorVertexBuffer.VertexColor(VertIdx) = (bHaveColors) ?
						(FColor)Mesh->GetVertexColor(Tri[j]) : TriColor;
				}

				VertIdx++;
			}
		}
	}



	/**
	 * @return number of active materials
	 */
	virtual int32 GetNumMaterials() const
	{
		return ParentBaseComponent->GetNumMaterials();
	}

	/**
	 * Safe GetMaterial function that will never return nullptr
	 */
	virtual UMaterialInterface* GetMaterial(int32 k) const
	{
		UMaterialInterface* Material = ParentBaseComponent->GetMaterial(k);
		return (Material != nullptr) ? Material : UMaterial::GetDefaultMaterial(MD_Surface);
	}


	/**
	 * This needs to be called if the set of active materials changes, otherwise
	 * the check in FPrimitiveSceneProxy::VerifyUsedMaterial() will fail if an override
	 * material is set, if materials change, etc, etc
	 */
	virtual void UpdatedReferencedMaterials()
	{
		// copied from FPrimitiveSceneProxy::FPrimitiveSceneProxy()
#if WITH_EDITOR
		TArray<UMaterialInterface*> Materials;
		ParentBaseComponent->GetUsedMaterials(Materials, true);
		ENQUEUE_RENDER_COMMAND(FMeshRenderBufferSetDestroy)(
			[this, Materials](FRHICommandListImmediate& RHICmdList)
		{
			this->SetUsedMaterialForVerification(Materials);
		});
#endif
	}


	//
	// FBaseDynamicMeshSceneProxy implementation
	//


	/**
	 * Render set of active RenderBuffers returned by GetActiveRenderBufferSets
	 */
	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views, 
		const FSceneViewFamily& ViewFamily, 
		uint32 VisibilityMap, 
		FMeshElementCollector& Collector) const override
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_OctreeDynamicMeshSceneProxy_GetDynamicMeshElements);

		const bool bWireframe = (AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe)
			|| ParentBaseComponent->EnableWireframeRenderPass();

		// set up wireframe material. Probably bad to reference GEngine here...also this material is very bad?
		FMaterialRenderProxy* WireframeMaterialProxy = nullptr;
		if (bWireframe)
		{
			FColoredMaterialRenderProxy* WireframeMaterialInstance = new FColoredMaterialRenderProxy(
				GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
				FLinearColor(0, 0.5f, 1.f)
			);
			Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
			WireframeMaterialProxy = WireframeMaterialInstance;
		}

		ESceneDepthPriorityGroup DepthPriority = SDPG_World;

		TArray<FMeshRenderBufferSet*> Buffers;
		GetActiveRenderBufferSets(Buffers);

		FMaterialRenderProxy* SecondaryMaterialProxy =
			ParentBaseComponent->HasSecondaryRenderMaterial() ? ParentBaseComponent->GetSecondaryRenderMaterial()->GetRenderProxy() : nullptr;

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
				for (FMeshRenderBufferSet* BufferSet : Buffers)
				{
					UMaterialInterface* UseMaterial = BufferSet->Material;
					if (ParentBaseComponent->HasOverrideRenderMaterial(0))
					{
						UseMaterial = ParentBaseComponent->GetOverrideRenderMaterial(0);
					}
					FMaterialRenderProxy* MaterialProxy = UseMaterial->GetRenderProxy();

					if (BufferSet->TriangleCount == 0)
					{
						continue;
					}

					// do we need separate one of these for each MeshRenderBufferSet?
					FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
					DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, DrawsVelocity(), bOutputVelocity);

					if (BufferSet->IndexBuffer.Indices.Num() > 0)
					{
						DrawBatch(Collector, *BufferSet, BufferSet->IndexBuffer, MaterialProxy, false, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
						if (bWireframe)
						{
							DrawBatch(Collector, *BufferSet, BufferSet->IndexBuffer, WireframeMaterialProxy, true, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
						}
					}

					// draw secondary buffer if we have it, and have secondary material
					if (BufferSet->SecondaryIndexBuffer.Indices.Num() > 0 && SecondaryMaterialProxy != nullptr)
					{
						DrawBatch(Collector, *BufferSet, BufferSet->SecondaryIndexBuffer, SecondaryMaterialProxy, false, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
						if (bWireframe)
						{
							DrawBatch(Collector, *BufferSet, BufferSet->SecondaryIndexBuffer, WireframeMaterialProxy, true, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
						}
					}
				}
			}
		}
	}



	/**
	 * Draw a single-frame FMeshBatch for a FMeshRenderBufferSet
	 */
	virtual void DrawBatch(FMeshElementCollector& Collector,
		const FMeshRenderBufferSet& RenderBuffers,
		const FDynamicMeshIndexBuffer32& IndexBuffer,
		FMaterialRenderProxy* UseMaterial,
		bool bWireframe,
		ESceneDepthPriorityGroup DepthPriority,
		int ViewIndex,
		FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer) const
	{
		FMeshBatch& Mesh = Collector.AllocateMesh();
		FMeshBatchElement& BatchElement = Mesh.Elements[0];
		BatchElement.IndexBuffer = &IndexBuffer;
		Mesh.bWireframe = bWireframe;
		Mesh.VertexFactory = &RenderBuffers.VertexFactory;
		Mesh.MaterialRenderProxy = UseMaterial;

		BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

		BatchElement.FirstIndex = 0;
		BatchElement.NumPrimitives = IndexBuffer.Indices.Num() / 3;
		BatchElement.MinVertexIndex = 0;
		BatchElement.MaxVertexIndex = RenderBuffers.PositionVertexBuffer.GetNumVertices() - 1;
		Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
		Mesh.Type = PT_TriangleList;
		Mesh.DepthPriorityGroup = DepthPriority;
		Mesh.bCanApplyViewModeOverrides = false;
		Collector.AddMesh(ViewIndex, Mesh);
	}







};