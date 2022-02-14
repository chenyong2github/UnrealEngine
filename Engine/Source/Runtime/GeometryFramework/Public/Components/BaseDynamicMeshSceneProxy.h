// Copyright Epic Games, Inc. All Rights Reserved. 

#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"
#include "VertexFactory.h"
#include "LocalVertexFactory.h"
#include "PrimitiveViewRelevance.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Rendering/ColorVertexBuffer.h"
#include "DynamicMeshBuilder.h"
#include "Components/BaseDynamicMeshComponent.h"
#include "Materials/Material.h"
#include "RayTracingDefinitions.h"
#include "RayTracingInstance.h"

using UE::Geometry::FDynamicMesh3;
using UE::Geometry::FDynamicMeshAttributeSet;
using UE::Geometry::FDynamicMeshUVOverlay;
using UE::Geometry::FDynamicMeshNormalOverlay;
using UE::Geometry::FDynamicMeshColorOverlay;
using UE::Geometry::FDynamicMeshMaterialAttribute;

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

	/**
	 * configure whether raytracing should be enabled for this RenderBufferSet
	 */
	bool bEnableRaytracing = false;

#if RHI_RAYTRACING
	/**
	 * Raytracing buffers
	 */
	FRayTracingGeometry PrimaryRayTracingGeometry;
	FRayTracingGeometry SecondaryRayTracingGeometry;
	bool bIsRayTracingDataValid = false;
#endif

	/**
	 * In situations where we want to *update* the existing Vertex or Index buffers, we need to synchronize
	 * access between the Game and Render threads. We use this lock to do that.
	 */
	FCriticalSection BuffersLock;


	FMeshRenderBufferSet(ERHIFeatureLevel::Type FeatureLevelType)
		: VertexFactory(FeatureLevelType, "FMeshRenderBufferSet")
	{
		StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);
		StaticMeshVertexBuffer.SetUseHighPrecisionTangentBasis(true);
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

#if RHI_RAYTRACING
			if (bEnableRaytracing)
			{
				PrimaryRayTracingGeometry.ReleaseResource();
				SecondaryRayTracingGeometry.ReleaseResource();
			}
#endif
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

		InvalidateRayTracingData();
		ValidateRayTracingData();		// currently we are immediately validating. This may be revisited in future.
	}


	/**
	 * Fast path to only update the primary and secondary index buffers. This can be used
	 * when (eg) the secondary index buffer is being used to highlight/hide a subset of triangles.
	 * @warning This can only be called on the Rendering Thread.
	 */
	void UploadIndexBufferUpdate()
	{
		// todo: can this be done with RHI locking and memcpy, like in TransferVertexUpdateToGPU?

		check(IsInRenderingThread());
		if (IndexBuffer.Indices.Num() > 0)
		{
			InitOrUpdateResource(&IndexBuffer);
		}
		if (bEnableSecondaryIndexBuffer && SecondaryIndexBuffer.Indices.Num() > 0)
		{
			InitOrUpdateResource(&SecondaryIndexBuffer);
		}

		InvalidateRayTracingData();
		ValidateRayTracingData();		// currently we are immediately validating. This may be revisited in future.
	}


	/**
	 * Fast path to only update vertex buffers. This path rebuilds all the
	 * resources and reconfigures the vertex factory, so the counts/etc could be modified.
	 * @warning This can only be called on the Rendering Thread.
	 */
	void UploadVertexUpdate(bool bPositions, bool bMeshAttribs, bool bColors)
	{
		// todo: look at calls to this function, it seems possible that TransferVertexUpdateToGPU
		// could be used instead (which should be somewhat more efficient?). It's not clear if there
		// are any situations where we would change vertex buffer size w/o also updating the index
		// buffers (in which case we are fully rebuilding the buffers...)

		check(IsInRenderingThread());

		if (TriangleCount == 0)
		{
			return;
		}

		if (bPositions)
		{
			InitOrUpdateResource(&this->PositionVertexBuffer);
		}
		if (bMeshAttribs)
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
		this->ColorVertexBuffer.BindColorVertexBuffer(&this->VertexFactory, Data);
		this->VertexFactory.SetData(Data);

		InitOrUpdateResource(&this->VertexFactory);

		InvalidateRayTracingData();
		ValidateRayTracingData();		// currently we are immediately validating. This may be revisited in future.
	}


	/**
	 * Fast path to update various vertex buffers. This path does not support changing the
	 * size/counts of any of the sub-buffers, a direct memcopy from the CPU-side buffer to the RHI buffer is used.
	 * @warning This can only be called on the Rendering Thread.
	 */
	void TransferVertexUpdateToGPU(bool bPositions, bool bNormals, bool bTexCoords, bool bColors)
	{
		check(IsInRenderingThread());
		if (TriangleCount == 0)
		{
			return;
		}

		if (bPositions)
		{
			FPositionVertexBuffer& VertexBuffer = this->PositionVertexBuffer;
			void* VertexBufferData = RHILockBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(), VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
			RHIUnlockBuffer(VertexBuffer.VertexBufferRHI);
		}
		if (bNormals)
		{
			FStaticMeshVertexBuffer& VertexBuffer = this->StaticMeshVertexBuffer;
			void* VertexBufferData = RHILockBuffer(VertexBuffer.TangentsVertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetTangentSize(), RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, VertexBuffer.GetTangentData(), VertexBuffer.GetTangentSize());
			RHIUnlockBuffer(VertexBuffer.TangentsVertexBuffer.VertexBufferRHI);
		}
		if (bColors)
		{
			FColorVertexBuffer& VertexBuffer = this->ColorVertexBuffer;
			void* VertexBufferData = RHILockBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(), VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
			RHIUnlockBuffer(VertexBuffer.VertexBufferRHI);
		}
		if (bTexCoords)
		{
			FStaticMeshVertexBuffer& VertexBuffer = this->StaticMeshVertexBuffer;
			void* VertexBufferData = RHILockBuffer(VertexBuffer.TexCoordVertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetTexCoordSize(), RLM_WriteOnly);
			FMemory::Memcpy(VertexBufferData, VertexBuffer.GetTexCoordData(), VertexBuffer.GetTexCoordSize());
			RHIUnlockBuffer(VertexBuffer.TexCoordVertexBuffer.VertexBufferRHI);
		}

		InvalidateRayTracingData();
		ValidateRayTracingData();		// currently we are immediately validating. This may be revisited in future.
	}


	void InvalidateRayTracingData()
	{
#if RHI_RAYTRACING
		bIsRayTracingDataValid = false;
#endif
	}

	// Verify that valid raytracing data is available. This will cause a rebuild of the
	// raytracing data if any of our buffers have been modified. Currently this is called
	// by GetDynamicRayTracingInstances to ensure the RT data is available when needed.
	void ValidateRayTracingData()
	{
#if RHI_RAYTRACING
		if (bIsRayTracingDataValid == false && IsRayTracingEnabled() && bEnableRaytracing)
		{
			UpdateRaytracingGeometryIfEnabled();

			bIsRayTracingDataValid = true;
		}
#endif
	}


protected:

	// rebuild raytracing data for current buffers
	void UpdateRaytracingGeometryIfEnabled()
	{
#if RHI_RAYTRACING
		// do we always want to do this?
		PrimaryRayTracingGeometry.ReleaseResource();		
		SecondaryRayTracingGeometry.ReleaseResource();
			
		for (int32 k = 0; k < 2; ++k)
		{
			FDynamicMeshIndexBuffer32& UseIndexBuffer = (k == 0) ? IndexBuffer : SecondaryIndexBuffer;
			if (UseIndexBuffer.Indices.Num() == 0)
			{
				continue;
			}

			FRayTracingGeometry& RayTracingGeometry = (k == 0) ? PrimaryRayTracingGeometry : SecondaryRayTracingGeometry;

			FRayTracingGeometryInitializer Initializer;
			Initializer.IndexBuffer = UseIndexBuffer.IndexBufferRHI;
			Initializer.TotalPrimitiveCount = UseIndexBuffer.Indices.Num() / 3;
			Initializer.GeometryType = RTGT_Triangles;
			Initializer.bFastBuild = true;
			Initializer.bAllowUpdate = false;

			RayTracingGeometry.SetInitializer(Initializer);
			RayTracingGeometry.InitResource();

			FRayTracingGeometrySegment Segment;
			Segment.VertexBuffer = PositionVertexBuffer.VertexBufferRHI;
			Segment.NumPrimitives = RayTracingGeometry.Initializer.TotalPrimitiveCount;
			Segment.MaxVertices = PositionVertexBuffer.GetNumVertices();
			RayTracingGeometry.Initializer.Segments.Add(Segment);

			RayTracingGeometry.UpdateRHI();
		}
#endif
	}



	/**
	 * Initializes a render resource, or update it if already initialized.
	 * @warning This function can only be called on the Render Thread
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
	using FIndex2i = UE::Geometry::FIndex2i;
	using FIndex3i = UE::Geometry::FIndex3i;
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

	// control raytracing support
	bool bEnableRaytracing = false;

	// Allow view-mode overrides. 
	bool bEnableViewModeOverrides = true;

public:
	FBaseDynamicMeshSceneProxy(UBaseDynamicMeshComponent* Component)
		: FPrimitiveSceneProxy(Component),
		ParentBaseComponent(Component),
		bEnableRaytracing(Component->GetEnableRaytracing()),
		bEnableViewModeOverrides(Component->GetViewModeOverridesEnabled())
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
		RenderBufferSet->bEnableRaytracing = this->bEnableRaytracing;

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
		FScopeLock Lock(&AllocatedSetsLock);
		if (ensure(AllocatedBufferSets.Contains(BufferSet)))
		{
			AllocatedBufferSets.Remove(BufferSet);
			Lock.Unlock();

			FMeshRenderBufferSet::DestroyRenderBufferSet(BufferSet);
		}
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
		const FDynamicMeshUVOverlay* UVOverlay,
		const FDynamicMeshNormalOverlay* NormalOverlay,
		const FDynamicMeshColorOverlay* ColorOverlay,
		TFunctionRef<void(int, int, int, const FVector3f&, FVector3f&, FVector3f&)> TangentsFunc,
		bool bTrackTriangles = false)
	{
		TArray<const FDynamicMeshUVOverlay*> UVOverlays;
		UVOverlays.Add(UVOverlay);
		InitializeBuffersFromOverlays(RenderBuffers, Mesh, NumTriangles, Enumerable,
			UVOverlays, NormalOverlay, ColorOverlay, TangentsFunc, bTrackTriangles);
	}



	/**
	 * Initialize rendering buffers from given attribute overlays.
	 * Creates three vertices per triangle, IE no shared vertices in buffers.
	 */
	template<typename TriangleEnumerable, typename UVOverlayListAllocator>
	void InitializeBuffersFromOverlays(
		FMeshRenderBufferSet* RenderBuffers,
		const FDynamicMesh3* Mesh,
		int NumTriangles, TriangleEnumerable Enumerable,
		const TArray<const FDynamicMeshUVOverlay*, UVOverlayListAllocator>& UVOverlays,
		const FDynamicMeshNormalOverlay* NormalOverlay,
		const FDynamicMeshColorOverlay* ColorOverlay,
		TFunctionRef<void(int, int, int, const FVector3f&, FVector3f&, FVector3f&)> TangentsFunc,
		bool bTrackTriangles = false)
	{
		RenderBuffers->TriangleCount = NumTriangles;
		if (NumTriangles == 0)
		{
			return;
		}

		bool bHaveColors = (ColorOverlay != nullptr) && (bIgnoreVertexColors == false);

		int NumVertices = NumTriangles * 3;
		int NumUVOverlays = UVOverlays.Num();
		int NumTexCoords = FMath::Max(1, NumUVOverlays);		// must have at least one tex coord
		TArray<FIndex3i, TFixedAllocator<MAX_STATIC_TEXCOORDS>> UVTriangles;
		UVTriangles.SetNum(NumTexCoords);

		{
			RenderBuffers->PositionVertexBuffer.Init(NumVertices);
			RenderBuffers->StaticMeshVertexBuffer.Init(NumVertices, NumTexCoords );
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
			for (int32 k = 0; k < NumTexCoords; ++k)
			{
				UVTriangles[k] = (k < NumUVOverlays && UVOverlays[k] != nullptr) ? UVOverlays[k]->GetTriangle(TriangleID) : FIndex3i::Invalid();
			}
			FIndex3i TriNormal = (NormalOverlay != nullptr) ? NormalOverlay->GetTriangle(TriangleID) : FIndex3i::Zero();
			FIndex3i TriColor = (ColorOverlay != nullptr) ? ColorOverlay->GetTriangle(TriangleID) : FIndex3i::Zero();

			FColor UniformTriColor = ConstantVertexColor;
			if (bUsePerTriangleColor && PerTriangleColorFunc != nullptr)
			{
				UniformTriColor = PerTriangleColorFunc(Mesh, TriangleID);
				bHaveColors = false;
			}

			for (int j = 0; j < 3; ++j)
			{
				RenderBuffers->PositionVertexBuffer.VertexPosition(VertIdx) = (FVector3f)Mesh->GetVertex(Tri[j]);

				FVector3f Normal = (NormalOverlay != nullptr && TriNormal[j] != FDynamicMesh3::InvalidID) ?
					NormalOverlay->GetElement(TriNormal[j]) : Mesh->GetVertexNormal(Tri[j]);

				// get tangents
				TangentsFunc(Tri[j], TriangleID, j, Normal, TangentX, TangentY);

				RenderBuffers->StaticMeshVertexBuffer.SetVertexTangents(VertIdx, TangentX, TangentY, Normal);

				for (int32 k = 0; k < NumTexCoords; ++k)
				{
					FVector2f UV = (UVTriangles[k][j] != FDynamicMesh3::InvalidID) ?
						UVOverlays[k]->GetElement(UVTriangles[k][j]) : FVector2f::Zero();
					RenderBuffers->StaticMeshVertexBuffer.SetVertexUV(VertIdx, k, UV);
				}

				FColor VertexFColor = (bHaveColors && TriColor[j] != FDynamicMesh3::InvalidID) ?
					UE::Geometry::ToFColor(ColorOverlay->GetElement(TriColor[j])) : UniformTriColor;

				RenderBuffers->ColorVertexBuffer.VertexColor(VertIdx) = VertexFColor;

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
		if (ensure(bUseSecondaryTriBuffers == true && RenderBuffers->Triangles.IsSet()) == false)
		{
			return;
		}

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
	 * RecomputeRenderBufferTriangleIndexSets re-sorts the existing set of triangles in a FMeshRenderBufferSet
	 * into primary and secondary index buffers. Note that UploadIndexBufferUpdate() must be called
	 * after this function!
	 */
	void RecomputeRenderBufferTriangleIndexSets(
		FMeshRenderBufferSet* RenderBuffers, 
		const FDynamicMesh3* Mesh)
	{
		if (RenderBuffers->TriangleCount == 0)
		{
			return;
		}
		if (ensure(RenderBuffers->Triangles.IsSet() && RenderBuffers->Triangles->Num() > 0) == false)
		{
			return;
		}

		//bool bDuplicate = false;		// flag for future use, in case we want to draw all triangles in primary and duplicates in secondary...
		RenderBuffers->IndexBuffer.Indices.Reset();
		RenderBuffers->SecondaryIndexBuffer.Indices.Reset();
		
		TArray<uint32>& Indices = RenderBuffers->IndexBuffer.Indices;
		TArray<uint32>& SecondaryIndices = RenderBuffers->SecondaryIndexBuffer.Indices;
		const TArray<int32>& TriangleIDs = RenderBuffers->Triangles.GetValue();

		int NumTris = TriangleIDs.Num();
		for (int k = 0; k < NumTris; ++k)
		{
			int TriangleID = TriangleIDs[k];
			bool bInclude = SecondaryTriFilterFunc(Mesh, TriangleID);
			if (bInclude)
			{
				SecondaryIndices.Add(3 * k);
				SecondaryIndices.Add(3 * k + 1);
				SecondaryIndices.Add(3 * k + 2);
			}
			else // if (bDuplicate == false)
			{
				Indices.Add(3 * k);
				Indices.Add(3 * k + 1);
				Indices.Add(3 * k + 2);
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
		const FDynamicMeshNormalOverlay* NormalOverlay,
		const FDynamicMeshColorOverlay* ColorOverlay,
		TFunctionRef<void(int, int, int, const FVector3f&, FVector3f&, FVector3f&)> TangentsFunc,
		bool bUpdatePositions = true,
		bool bUpdateNormals = false,
		bool bUpdateColors = false)
	{
		if (RenderBuffers->TriangleCount == 0)
		{
			return;
		}

		bool bHaveColors = (ColorOverlay != nullptr) && (bIgnoreVertexColors == false);

		int NumVertices = NumTriangles * 3;
		if ( (bUpdatePositions && ensure(RenderBuffers->PositionVertexBuffer.GetNumVertices() == NumVertices) == false )
			|| (bUpdateNormals && ensure(RenderBuffers->StaticMeshVertexBuffer.GetNumVertices() == NumVertices) == false )
			|| (bUpdateColors && ensure(RenderBuffers->ColorVertexBuffer.GetNumVertices() == NumVertices) == false ) )
		{
			return;
		}

		int VertIdx = 0;
		FVector3f TangentX, TangentY;
		for (int TriangleID : Enumerable)
		{
			FIndex3i Tri = Mesh->GetTriangle(TriangleID);

			FIndex3i TriNormal = (bUpdateNormals && NormalOverlay != nullptr) ? NormalOverlay->GetTriangle(TriangleID) : FIndex3i::Zero();
			FIndex3i TriColor = (bUpdateColors && ColorOverlay != nullptr) ? ColorOverlay->GetTriangle(TriangleID) : FIndex3i::Zero();

			FColor UniformTriColor = ConstantVertexColor;
			if (bUpdateColors && bUsePerTriangleColor && PerTriangleColorFunc != nullptr)
			{
				UniformTriColor = PerTriangleColorFunc(Mesh, TriangleID);
				bHaveColors = false;
			}

			for (int j = 0; j < 3; ++j)
			{
				if (bUpdatePositions)
				{
					RenderBuffers->PositionVertexBuffer.VertexPosition(VertIdx) = (FVector3f)Mesh->GetVertex(Tri[j]);
				}

				if (bUpdateNormals)
				{
					// get normal and tangent
					FVector3f Normal = (NormalOverlay != nullptr && TriNormal[j] != FDynamicMesh3::InvalidID) ?
						NormalOverlay->GetElement(TriNormal[j]) : Mesh->GetVertexNormal(Tri[j]);
					TangentsFunc(Tri[j], TriangleID, j, Normal, TangentX, TangentY);

					RenderBuffers->StaticMeshVertexBuffer.SetVertexTangents(VertIdx, (FVector3f)TangentX, (FVector3f)TangentY, (FVector3f)Normal);
				}

				if (bUpdateColors)
				{
					FColor VertexFColor = (bHaveColors  && TriColor[j] != FDynamicMesh3::InvalidID) ?
						UE::Geometry::ToFColor(ColorOverlay->GetElement(TriColor[j])) : UniformTriColor;
					RenderBuffers->ColorVertexBuffer.VertexColor(VertIdx) = VertexFColor;
				}

				VertIdx++;
			}
		}
	}

	/**
	 * Update vertex uvs of an existing set of render buffers.
	 * Assumes that buffers were created with unshared vertices, ie three vertices per triangle, eg by InitializeBuffersFromOverlays()
	 */
	template<typename TriangleEnumerable, typename UVOverlayListAllocator>
	void UpdateVertexUVBufferFromOverlays(
		FMeshRenderBufferSet* RenderBuffers,
		const FDynamicMesh3* Mesh,
		int32 NumTriangles, TriangleEnumerable Enumerable,
		const TArray<const FDynamicMeshUVOverlay*, UVOverlayListAllocator>& UVOverlays)
	{
		// We align the update to the way we set UV's in InitializeBuffersFromOverlays.

		if (RenderBuffers->TriangleCount == 0)
		{
			return;
		}
		int NumVertices = NumTriangles * 3;
		if (ensure(RenderBuffers->StaticMeshVertexBuffer.GetNumVertices() == NumVertices) == false)
		{
			return;
		}

		int NumUVOverlays = UVOverlays.Num();
		int NumTexCoords = RenderBuffers->StaticMeshVertexBuffer.GetNumTexCoords();
		if (!ensure(NumUVOverlays <= NumTexCoords))
		{
			return;
		}

		// Temporarily stores the UV element indices for all UV channels of a single triangle
		TArray<FIndex3i, TFixedAllocator<MAX_STATIC_TEXCOORDS>> UVTriangles;
		UVTriangles.SetNum(NumTexCoords);

		int VertIdx = 0;
		for (int TriangleID : Enumerable)
		{
			for (int32 k = 0; k < NumTexCoords; ++k)
			{
				UVTriangles[k] = (k < NumUVOverlays && UVOverlays[k] != nullptr) ? UVOverlays[k]->GetTriangle(TriangleID) : FIndex3i::Invalid();
			}

			for (int j = 0; j < 3; ++j)
			{
				for (int32 k = 0; k < NumTexCoords; ++k)
				{
					FVector2f UV = (UVTriangles[k][j] != FDynamicMesh3::InvalidID) ?
						UVOverlays[k]->GetElement(UVTriangles[k][j]) : FVector2f::Zero();
					RenderBuffers->StaticMeshVertexBuffer.SetVertexUV(VertIdx, k, UV);
				}

				++VertIdx;
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
	 * Set whether or not to validate mesh batch materials against the component materials.
	 */
	void SetVerifyUsedMaterials(const bool bState)
	{
		bVerifyUsedMaterials = bState;
	}


	/**
	 * This needs to be called if the set of active materials changes, otherwise
	 * the check in FPrimitiveSceneProxy::VerifyUsedMaterial() will fail if an override
	 * material is set, if materials change, etc, etc
	 */
	virtual void UpdatedReferencedMaterials()
	{
#if WITH_EDITOR
		TArray<UMaterialInterface*> Materials;
		ParentBaseComponent->GetUsedMaterials(Materials, true);

		// Temporarily disable material verification while the enqueued render command is in flight.
		// The original value for bVerifyUsedMaterials gets restored when the command is executed.
		// If we do not do this, material verification might spuriously fail in cases where the render command for changing
		// the verfifcation material is still in flight but the render thread is already trying to render the mesh.
		const uint8 bRestoreVerifyUsedMaterials = bVerifyUsedMaterials;
		bVerifyUsedMaterials = false;

		ENQUEUE_RENDER_COMMAND(FMeshRenderBufferSetDestroy)(
			[this, Materials, bRestoreVerifyUsedMaterials](FRHICommandListImmediate& RHICmdList)
		{
			this->SetUsedMaterialForVerification(Materials);
			this->bVerifyUsedMaterials = bRestoreVerifyUsedMaterials;
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
		QUICK_SCOPE_CYCLE_COUNTER(STAT_BaseDynamicMeshSceneProxy_GetDynamicMeshElements);

		const bool bWireframe = (AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe)
			|| ParentBaseComponent->GetEnableWireframeRenderPass();

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
		bool bDrawSecondaryBuffers = ParentBaseComponent->GetSecondaryBuffersVisibility();

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

					// lock buffers so that they aren't modified while we are submitting them
					FScopeLock BuffersLock(&BufferSet->BuffersLock);

					// do we need separate one of these for each MeshRenderBufferSet?
					FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
					DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, DrawsVelocity(), bOutputVelocity, GetCustomPrimitiveData());

					if (BufferSet->IndexBuffer.Indices.Num() > 0)
					{
						// Unlike most meshes, which just use the wireframe material in wireframe mode, we draw the wireframe on top of the normal material if needed,
						// as this is easier to interpret. However, we do not do this in ortho viewports, where it frequently causes the our edit gizmo to be hidden 
						// beneath the material. So, only draw the base material if we are in perspective mode, or we're in ortho but not in wireframe.
						if (View->IsPerspectiveProjection() || !(AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe))
						{
							DrawBatch(Collector, *BufferSet, BufferSet->IndexBuffer, MaterialProxy, false, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
						}
						if (bWireframe)
						{
							DrawBatch(Collector, *BufferSet, BufferSet->IndexBuffer, WireframeMaterialProxy, true, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
						}
					}

					// draw secondary buffer if we have it, falling back to base material if we don't have the Secondary material
					FMaterialRenderProxy* UseSecondaryMaterialProxy = (SecondaryMaterialProxy != nullptr) ? SecondaryMaterialProxy : MaterialProxy;
					if (bDrawSecondaryBuffers && BufferSet->SecondaryIndexBuffer.Indices.Num() > 0 && UseSecondaryMaterialProxy != nullptr)
					{
						DrawBatch(Collector, *BufferSet, BufferSet->SecondaryIndexBuffer, UseSecondaryMaterialProxy, false, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
						if (bWireframe)
						{
							DrawBatch(Collector, *BufferSet, BufferSet->SecondaryIndexBuffer, UseSecondaryMaterialProxy, true, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
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
		Mesh.bCanApplyViewModeOverrides = this->bEnableViewModeOverrides;
		Collector.AddMesh(ViewIndex, Mesh);
	}




#if RHI_RAYTRACING

	virtual bool IsRayTracingRelevant() const override { return true; }

	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances) override
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_BaseDynamicMeshSceneProxy_GetDynamicRayTracingInstances);

		ESceneDepthPriorityGroup DepthPriority = SDPG_World;

		TArray<FMeshRenderBufferSet*> Buffers;
		GetActiveRenderBufferSets(Buffers);

		FMaterialRenderProxy* SecondaryMaterialProxy =
			ParentBaseComponent->HasSecondaryRenderMaterial() ? ParentBaseComponent->GetSecondaryRenderMaterial()->GetRenderProxy() : nullptr;
		bool bDrawSecondaryBuffers = ParentBaseComponent->GetSecondaryBuffersVisibility();

		bool bHasPrecomputedVolumetricLightmap;
		FMatrix PreviousLocalToWorld;
		int32 SingleCaptureIndex;
		bool bOutputVelocity;
		GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);

		// is it safe to share this between primary and secondary raytracing batches?
		FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Context.RayTracingMeshResourceCollector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
		DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, DrawsVelocity(), bOutputVelocity);

		// Draw the active buffer sets
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
			if (BufferSet->bIsRayTracingDataValid == false)
			{
				continue;
			}

			// Lock buffers so that they aren't modified while we are submitting them.
			FScopeLock BuffersLock(&BufferSet->BuffersLock);

			// draw primary index buffer
			if (BufferSet->IndexBuffer.Indices.Num() > 0 
				&& BufferSet->PrimaryRayTracingGeometry.RayTracingGeometryRHI.IsValid())
			{
				ensure(BufferSet->PrimaryRayTracingGeometry.Initializer.IndexBuffer.IsValid());
				DrawRayTracingBatch(Context, *BufferSet, BufferSet->IndexBuffer, BufferSet->PrimaryRayTracingGeometry, MaterialProxy, DepthPriority, DynamicPrimitiveUniformBuffer, OutRayTracingInstances);
			}

			// draw secondary index buffer if we have it, falling back to base material if we don't have the Secondary material
			FMaterialRenderProxy* UseSecondaryMaterialProxy = (SecondaryMaterialProxy != nullptr) ? SecondaryMaterialProxy : MaterialProxy;
			if (bDrawSecondaryBuffers 
				&& BufferSet->SecondaryIndexBuffer.Indices.Num() > 0 
				&& UseSecondaryMaterialProxy != nullptr
				&& BufferSet->SecondaryRayTracingGeometry.RayTracingGeometryRHI.IsValid() )
			{
				ensure(BufferSet->SecondaryRayTracingGeometry.Initializer.IndexBuffer.IsValid());
				DrawRayTracingBatch(Context, *BufferSet, BufferSet->SecondaryIndexBuffer, BufferSet->SecondaryRayTracingGeometry, UseSecondaryMaterialProxy, DepthPriority, DynamicPrimitiveUniformBuffer, OutRayTracingInstances);
			}
		}
	}



	/**
	* Draw a single-frame raytracing FMeshBatch for a FMeshRenderBufferSet
	*/
	virtual void DrawRayTracingBatch(
		FRayTracingMaterialGatheringContext& Context,
		const FMeshRenderBufferSet& RenderBuffers,
		const FDynamicMeshIndexBuffer32& IndexBuffer,
		FRayTracingGeometry& RayTracingGeometry,
		FMaterialRenderProxy* UseMaterialProxy,
		ESceneDepthPriorityGroup DepthPriority,
		FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer,
		TArray<FRayTracingInstance>& OutRayTracingInstances	) const
	{
		ensure(RayTracingGeometry.Initializer.IndexBuffer.IsValid());

		FRayTracingInstance RayTracingInstance;
		RayTracingInstance.Geometry = &RayTracingGeometry;
		RayTracingInstance.InstanceTransforms.Add(GetLocalToWorld());

		uint32 SectionIdx = 0;
		FMeshBatch MeshBatch;

		MeshBatch.VertexFactory = &RenderBuffers.VertexFactory;
		MeshBatch.SegmentIndex = 0;
		MeshBatch.MaterialRenderProxy = UseMaterialProxy;
		MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
		MeshBatch.Type = PT_TriangleList;
		MeshBatch.DepthPriorityGroup = DepthPriority;
		MeshBatch.bCanApplyViewModeOverrides = this->bEnableViewModeOverrides;
		MeshBatch.CastRayTracedShadow = IsShadowCast(Context.ReferenceView);

		FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
		BatchElement.IndexBuffer = &IndexBuffer;
		BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;
		BatchElement.FirstIndex = 0;
		BatchElement.NumPrimitives = IndexBuffer.Indices.Num() / 3;
		BatchElement.MinVertexIndex = 0;
		BatchElement.MaxVertexIndex = RenderBuffers.PositionVertexBuffer.GetNumVertices() - 1;

		RayTracingInstance.Materials.Add(MeshBatch);

		RayTracingInstance.BuildInstanceMaskAndFlags(GetScene().GetFeatureLevel());
		OutRayTracingInstances.Add(RayTracingInstance);
	}


#endif // RHI_RAYTRACING






};