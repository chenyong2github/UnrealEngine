// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "HairDescription.h"
#include "HairStrandsDatas.h"
#include "HairCardsDatas.h"
#include "RenderResource.h"
#include "HairStrandsInterface.h"
#include "HairStrandsMeshProjection.h"

inline uint32 GetBufferTotalNumBytes(const FRDGExternalBuffer& In) 
{
	return In.Buffer ? In.Buffer->Desc.GetTotalNumBytes() : 0;
}

enum class EHairStrandsResourcesType : uint8
{
	Guides,		// Guides used for simulation
	Strands,	// Rendering strands 
	Cards		// Guides used for deforming the cards geometry (which is different from the simulation guides)
};

enum class EHairStrandsAllocationType : uint8
{
	Immediate,	// Resources are allocated immediately
	Deferred	// Resources allocation is deferred to first usage
};

/* Hair resouces which whom allocation can be deferred */
struct FHairCommonResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairCommonResource(EHairStrandsAllocationType AllocationType, bool bUseRenderGraph=true);

	/* Init/Release buffers (FRenderResource) */
	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

	/* Init/Release buffers (FHairCommonResource) */
	void Allocate(FRDGBuilder& GraphBuilder);
	virtual void InternalAllocate() {};
	virtual void InternalAllocate(FRDGBuilder& GraphBuilder) {};
	virtual void InternalRelease() {};

	bool bUseRenderGraph = true;
	bool bIsInitialized = false;
	EHairStrandsAllocationType AllocationType = EHairStrandsAllocationType::Deferred;
};

/* Render buffers for root deformation for dynamic meshes */
struct FHairStrandsRestRootResource : public FHairCommonResource
{
	/** Build the hair strands resource */
	FHairStrandsRestRootResource(const FHairStrandsRootData& RootData, EHairStrandsResourcesType CurveType);

	/* Init/Release buffers */
	virtual void InternalAllocate(FRDGBuilder& GraphBuilder) override;
	virtual void InternalRelease() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsRestRootResource"); }

	/* Populate GPU LOD data from RootData (this function doesn't initialize resources) */
	void PopulateFromRootData();

	/* Return the memory size for GPU resources */
	uint32 GetResourcesSize() const 
	{
		uint32 Total = 0;
		Total += GetBufferTotalNumBytes(VertexToCurveIndexBuffer);
		for (const FLOD& LOD : LODs)
		{
			Total += GetBufferTotalNumBytes(LOD.RootTriangleIndexBuffer);
			Total += GetBufferTotalNumBytes(LOD.RootTriangleBarycentricBuffer);
			Total += GetBufferTotalNumBytes(LOD.RestRootTrianglePosition0Buffer);
			Total += GetBufferTotalNumBytes(LOD.RestRootTrianglePosition1Buffer);
			Total += GetBufferTotalNumBytes(LOD.RestRootTrianglePosition2Buffer);
			Total += GetBufferTotalNumBytes(LOD.MeshInterpolationWeightsBuffer);
			Total += GetBufferTotalNumBytes(LOD.MeshSampleIndicesBuffer);
			Total += GetBufferTotalNumBytes(LOD.RestSamplePositionsBuffer);
		}
		return Total;
	}

	FRDGExternalBuffer VertexToCurveIndexBuffer;

	struct FLOD
	{
		enum class EStatus { Invalid, Initialized, Completed };

		const bool IsValid() const { return Status == EStatus::Completed; }
		EStatus Status = EStatus::Invalid;
		int32 LODIndex = -1;

		/* Triangle on which a root is attached */
		/* When the projection is done with source to target mesh transfer, the projection indices does not match.
		   In this case we need to separate index computation. The barycentric coords remain the same however. */
		FRDGExternalBuffer RootTriangleIndexBuffer;
		FRDGExternalBuffer RootTriangleBarycentricBuffer;

		/* Strand hair roots translation and rotation in rest position relative to the bound triangle. Positions are relative to the rest root center */
		FRDGExternalBuffer RestRootTrianglePosition0Buffer;
		FRDGExternalBuffer RestRootTrianglePosition1Buffer;
		FRDGExternalBuffer RestRootTrianglePosition2Buffer;

		/* Strand hair mesh interpolation matrix and sample indices */
		uint32 SampleCount = 0;
		FRDGExternalBuffer MeshInterpolationWeightsBuffer;
		FRDGExternalBuffer MeshSampleIndicesBuffer;
		FRDGExternalBuffer RestSamplePositionsBuffer;
	};

	/* Store the hair projection information for each mesh LOD */
	TArray<FLOD> LODs;

	/* Store CPU data for root info & root binding */
	const FHairStrandsRootData& RootData;

	/* Type of curves */
	const EHairStrandsResourcesType CurveType;
};

/* Render buffers for root deformation for dynamic meshes */
struct FHairStrandsDeformedRootResource : public FHairCommonResource
{
	/** Build the hair strands resource */
	FHairStrandsDeformedRootResource(EHairStrandsResourcesType CurveType);
	FHairStrandsDeformedRootResource(const FHairStrandsRestRootResource* InRestResources, EHairStrandsResourcesType CurveType);

	/* Init/Release buffers */
	virtual void InternalAllocate(FRDGBuilder& GraphBuilder) override;
	virtual void InternalRelease() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsDeformedRootResource"); }

	/* Indirect if the current root resources are valid and up to date */
	bool IsValid() const { return MeshLODIndex >= 0 && MeshLODIndex < LODs.Num() && LODs[MeshLODIndex].IsValid(); }
	bool IsValid(int32 InMeshLODIndex) const { return InMeshLODIndex >= 0 && InMeshLODIndex < LODs.Num() && LODs[InMeshLODIndex].IsValid(); }

	/* Return the memory size for GPU resources */
	uint32 GetResourcesSize() const
	{
		uint32 Total = 0;
		for (const FLOD& LOD : LODs)
		{
			Total += GetBufferTotalNumBytes(LOD.DeformedRootTrianglePosition0Buffer);
			Total += GetBufferTotalNumBytes(LOD.DeformedRootTrianglePosition1Buffer);
			Total += GetBufferTotalNumBytes(LOD.DeformedRootTrianglePosition2Buffer);
			Total += GetBufferTotalNumBytes(LOD.DeformedSamplePositionsBuffer);
			Total += GetBufferTotalNumBytes(LOD.MeshSampleWeightsBuffer);
		}
		return Total;
	}

	struct FLOD
	{
		enum class EStatus { Invalid, Initialized, Completed };

		// A LOD is considered valid as long as its resources are initialized. 
		// Its state will become completed once its triangle position will be 
		// update, but in order to be update its status needs to be valid.
		const bool IsValid() const { return Status == EStatus::Initialized || Status == EStatus::Completed; }
		EStatus Status = EStatus::Invalid;
		int32 LODIndex = -1;

		/* Strand hair roots translation and rotation in triangle-deformed position relative to the bound triangle. Positions are relative the deformed root center*/
		FRDGExternalBuffer DeformedRootTrianglePosition0Buffer;
		FRDGExternalBuffer DeformedRootTrianglePosition1Buffer;
		FRDGExternalBuffer DeformedRootTrianglePosition2Buffer;

		/* Strand hair mesh interpolation matrix and sample indices */
		uint32 SampleCount = 0;
		FRDGExternalBuffer DeformedSamplePositionsBuffer;
		FRDGExternalBuffer MeshSampleWeightsBuffer;
	};

	/* Store the hair projection information for each mesh LOD */
	uint32 RootCount = 0;
	TArray<FLOD> LODs;

	/* Last update MeshLODIndex */
	int32 MeshLODIndex = -1;

	/* Type of curves */
	const EHairStrandsResourcesType CurveType;
};

/* Render buffers that will be used for rendering */
struct FHairStrandsRestResource : public FHairCommonResource
{
	/** Build the hair strands resource */
	FHairStrandsRestResource(const FHairStrandsBulkData& InBulkData, EHairStrandsResourcesType CurveType);

	/* Init/Release buffers */
	virtual void InternalAllocate(FRDGBuilder& GraphBuilder) override;
	virtual void InternalRelease() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsResource"); }

	FRDGExternalBuffer GetTangentBuffer(class FRDGBuilder& GraphBuilder, class FGlobalShaderMap* ShaderMap);
	
	FVector GetPositionOffset() const { return BulkData.GetPositionOffset(); }

	/* Return the memory size for GPU resources */
	uint32 GetResourcesSize() const
	{
		uint32 Total = 0;
		Total += GetBufferTotalNumBytes(PositionBuffer);
		Total += GetBufferTotalNumBytes(PositionOffsetBuffer);
		Total += GetBufferTotalNumBytes(AttributeBuffer);
		Total += GetBufferTotalNumBytes(MaterialBuffer);
		Total += GetBufferTotalNumBytes(TangentBuffer);
		return Total;
	}

	/* Strand hair rest position buffer */
	FRDGExternalBuffer  PositionBuffer;

	/* Strand hair rest offset position buffer */
	FRDGExternalBuffer  PositionOffsetBuffer;

	/* Strand hair tangent buffer (non-allocated unless used for static geometry) */
	FRDGExternalBuffer TangentBuffer;

	/* Strand hair attribute buffer */
	FRDGExternalBuffer AttributeBuffer;

	/* Strand hair material buffer */
	FRDGExternalBuffer MaterialBuffer;

	/* Reference to the hair strands render data */
	const FHairStrandsBulkData& BulkData;

	/* Type of curves */
	const EHairStrandsResourcesType CurveType;

	inline uint32 GetVertexCount() const { return BulkData.PointCount; }
};

struct FHairStrandsDeformedResource : public FHairCommonResource
{
	/** Build the hair strands resource */
	FHairStrandsDeformedResource(const FHairStrandsBulkData& BulkData, bool bInitializeData, EHairStrandsResourcesType CurveType);

	/* Init/Release buffers */
	virtual void InternalAllocate(FRDGBuilder& GraphBuilder) override;
	virtual void InternalRelease() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsDeformedResource"); }

	/* Return the memory size for GPU resources */
	uint32 GetResourcesSize() const
	{
		uint32 Total = 0;
		Total += GetBufferTotalNumBytes(DeformedPositionBuffer[0]);
		Total += GetBufferTotalNumBytes(DeformedPositionBuffer[1]);
		Total += GetBufferTotalNumBytes(TangentBuffer);
		return Total;
	}

	/* Strand hair deformed position buffer (previous and current) */
	FRDGExternalBuffer DeformedPositionBuffer[2];

	/* Strand hair deformed position buffer (previous and current) */
	FRDGExternalBuffer DeformedOffsetBuffer[2];

	/* Strand hair tangent buffer */
	FRDGExternalBuffer TangentBuffer;

	/* Position offset as the deformed positions are expressed in relative coordinate (16bits) */
	FVector PositionOffset[2] = {FVector::ZeroVector, FVector::ZeroVector};

	/* Reference to the hair strands render data */
	const FHairStrandsBulkData& BulkData;

	/* Whether the GPU data should be initialized with the asset data or not */
	const bool bInitializedData = false;

	/* Whether the GPU data should be initialized with the asset data or not */
	uint32 CurrentIndex = 0;

	/* Type of curves */
	const EHairStrandsResourcesType CurveType;

	enum EFrameType
	{
		Previous,
		Current
	};

	// Helper accessors
	inline uint32 GetIndex(EFrameType T) const					{ return T == EFrameType::Current ? CurrentIndex : 1u - CurrentIndex; }
	inline FRDGExternalBuffer& GetBuffer(EFrameType T)			{ return DeformedPositionBuffer[GetIndex(T)];  }
	inline FVector& GetPositionOffset(EFrameType T)				{ return PositionOffset[GetIndex(T)]; }
	inline FRDGExternalBuffer& GetPositionOffsetBuffer(EFrameType T) { return DeformedOffsetBuffer[GetIndex(T)]; }
	inline const FVector& GetPositionOffset(EFrameType T) const { return PositionOffset[GetIndex(T)]; }
	inline void SwapBuffer()									{ CurrentIndex = 1u - CurrentIndex; }
	//bool NeedsToUpdateTangent();
};

struct FHairStrandsClusterCullingResource : public FHairCommonResource
{
	FHairStrandsClusterCullingResource(const FHairStrandsClusterCullingData& Data);

	/* Init/Release buffers */
	virtual void InternalAllocate(FRDGBuilder& GraphBuilder) override;
	virtual void InternalRelease() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsClusterResource"); }

	/* Return the memory size for GPU resources */
	uint32 GetResourcesSize() const
	{
		uint32 Total = 0;
		Total += GetBufferTotalNumBytes(ClusterInfoBuffer);
		Total += GetBufferTotalNumBytes(ClusterLODInfoBuffer);
		Total += GetBufferTotalNumBytes(VertexToClusterIdBuffer);
		Total += GetBufferTotalNumBytes(ClusterVertexIdBuffer);
		return Total;
	}

	/* Cluster info buffer */
	FRDGExternalBuffer ClusterInfoBuffer;
	FRDGExternalBuffer ClusterLODInfoBuffer;

	/* VertexId => ClusterId to know which AABB to contribute to*/
	FRDGExternalBuffer VertexToClusterIdBuffer;

	/* Concatenated data for each cluster: list of VertexId pointed to by ClusterInfoBuffer */
	FRDGExternalBuffer ClusterVertexIdBuffer;

	const FHairStrandsClusterCullingData Data;
};

struct FHairStrandsInterpolationResource : public FHairCommonResource
{
	/** Build the hair strands resource */
	FHairStrandsInterpolationResource(const FHairStrandsInterpolationBulkData& InBulkData);
	
	/* Init/Release buffers */
	virtual void InternalAllocate(FRDGBuilder& GraphBuilder) override;
	virtual void InternalRelease() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsInterplationResource"); }

	/* Return the memory size for GPU resources */
	uint32 GetResourcesSize() const
	{
		uint32 Total = 0;
		Total += GetBufferTotalNumBytes(Interpolation0Buffer);
		Total += GetBufferTotalNumBytes(Interpolation1Buffer);
		Total += GetBufferTotalNumBytes(SimRootPointIndexBuffer);
		return Total;
	}

	FRDGExternalBuffer Interpolation0Buffer;
	FRDGExternalBuffer Interpolation1Buffer;
	FRDGExternalBuffer SimRootPointIndexBuffer;

	/* Reference to the hair strands interpolation render data */
	const FHairStrandsInterpolationBulkData& BulkData;
};

#if RHI_RAYTRACING
struct FHairStrandsRaytracingResource : public FHairCommonResource
{
	/** Build the hair strands resource */
	FHairStrandsRaytracingResource(const FHairStrandsBulkData& InData);
	FHairStrandsRaytracingResource(const FHairCardsBulkData& InData);
	FHairStrandsRaytracingResource(const FHairMeshesBulkData& InData);

	/* Init/Release buffers */
	virtual void InternalAllocate(FRDGBuilder& GraphBuilder) override;
	virtual void InternalRelease() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsRaytracingResource"); }

	/* Return the memory size for GPU resources */
	uint32 GetResourcesSize() const
	{
		uint32 Total = 0;
		Total += GetBufferTotalNumBytes(PositionBuffer);
		return Total;
	}

	FRDGExternalBuffer PositionBuffer;
	FRayTracingGeometry RayTracingGeometry;
	uint32 VertexCount = 0;
	bool bIsRTGeometryInitialized = false;
};
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cards

class FHairCardsVertexBuffer : public FVertexBufferWithSRV
{
public:
	virtual void InitRHI() override {}
};

class FHairCardIndexBuffer : public FIndexBuffer
{
public:
	const TArray<FHairCardsIndexFormat::Type>& Indices;
	FHairCardIndexBuffer(const TArray<FHairCardsIndexFormat::Type>& InIndices) :Indices(InIndices) {}
	virtual void InitRHI() override;
};

struct FHairCardsBulkData;

/* Render buffers that will be used for rendering */
struct FHairCardsRestResource : public FHairCommonResource
{
	/** Build the hair strands resource */
	FHairCardsRestResource(const FHairCardsBulkData& InBulkData);

	virtual void InitResource() override;
	virtual void ReleaseResource() override;

	/* Init/release buffers */
	virtual void InternalAllocate() override;
	virtual void InternalRelease() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairCardsResource"); }

	/* Return the memory size for GPU resources */
	uint32 GetResourcesSize() const
	{
		uint32 Total = 0;
		Total += BulkData.Positions.GetAllocatedSize();
		Total += BulkData.Normals.GetAllocatedSize();
		Total += BulkData.Indices.GetAllocatedSize();
		Total += BulkData.UVs.GetAllocatedSize();
		return Total;
	}

	uint32 GetVertexCount() const { return BulkData.GetNumVertices();  }
	uint32 GetPrimitiveCount() const { return BulkData.GetNumTriangles(); }

	/* Strand hair rest position buffer */
	FHairCardsVertexBuffer RestPositionBuffer;
	FHairCardIndexBuffer RestIndexBuffer;
	bool bInvertUV = false;

	FHairCardsVertexBuffer NormalsBuffer;
	FHairCardsVertexBuffer UVsBuffer;

	FSamplerStateRHIRef DepthSampler;
	FSamplerStateRHIRef TangentSampler;
	FSamplerStateRHIRef CoverageSampler;
	FSamplerStateRHIRef AttributeSampler;
	FSamplerStateRHIRef AuxilaryDataSampler;

	FTextureReferenceRHIRef	DepthTexture = nullptr;
	FTextureReferenceRHIRef	CoverageTexture = nullptr;
	FTextureReferenceRHIRef	TangentTexture = nullptr;
	FTextureReferenceRHIRef	AttributeTexture = nullptr;
	FTextureReferenceRHIRef	AuxilaryDataTexture = nullptr;

	/* Reference to the hair strands render data */
	const FHairCardsBulkData& BulkData;
};

/* Render buffers that will be used for rendering */
struct FHairCardsProceduralResource : public FHairCommonResource
{
	/** Build the hair strands resource */
	FHairCardsProceduralResource(const FHairCardsProceduralDatas::FRenderData& HairCardsRenderData, const FIntPoint& AtlasResolution, const FHairCardsVoxel& InVoxel);

	/* Init/release buffers */
	virtual void InternalAllocate(FRDGBuilder& GraphBuilder) override;
	virtual void InternalRelease() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairCardsResource"); }

	/* Return the memory size for GPU resources */
	uint32 GetResourcesSize() const
	{
		return 0;
	}

	/* Strand hair rest position buffer */		
	uint32 CardBoundCount;
	FIntPoint AtlasResolution;

	FRDGExternalBuffer AtlasRectBuffer;
	FRDGExternalBuffer LengthBuffer;
	FRDGExternalBuffer CardItToClusterBuffer;
	FRDGExternalBuffer ClusterIdToVerticesBuffer;
	FRDGExternalBuffer ClusterBoundBuffer;
	FRDGExternalBuffer CardsStrandsPositions;
	FRDGExternalBuffer CardsStrandsAttributes;

	FHairCardsVoxel CardVoxel;

	/* Position offset as the rest positions are expressed in relative coordinate (16bits) */
	//FVector PositionOffset = FVector::ZeroVector;

	/* Reference to the hair strands render data */
	const FHairCardsProceduralDatas::FRenderData& RenderData;
};

struct FHairCardsDeformedResource : public FHairCommonResource
{
	/** Build the hair strands resource */
	FHairCardsDeformedResource(const FHairCardsBulkData& BulkData, bool bInitializeData);

	/* Init/release buffers */
	virtual void InternalAllocate(FRDGBuilder& GraphBuilder) override;
	virtual void InternalRelease() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairCardsDeformedResource"); }

	/* Return the memory size for GPU resources */
	uint32 GetResourcesSize() const
	{
		uint32 Total = 0;
		Total += GetBufferTotalNumBytes(DeformedPositionBuffer[0]);
		Total += GetBufferTotalNumBytes(DeformedPositionBuffer[1]);
		return Total;
	}

	/* Strand hair deformed position buffer (previous and current) */
	FRDGExternalBuffer DeformedPositionBuffer[2];

	/* Reference to the hair strands render data */
	const FHairCardsBulkData& BulkData;

	/* Whether the GPU data should be initialized with the asset data or not */
	const bool bInitializedData = false;

	/* Whether the GPU data should be initialized with the asset data or not */
	uint32 CurrentIndex = 0;

	enum EFrameType
	{
		Previous,
		Current
	};

	// Helper accessors
	inline uint32 GetIndex(EFrameType T)				{ return T == EFrameType::Current ? CurrentIndex : 1u - CurrentIndex; }
	inline FRDGExternalBuffer& GetBuffer(EFrameType T)	{ return DeformedPositionBuffer[GetIndex(T)];  }
	inline void SwapBuffer()							{ CurrentIndex = 1u - CurrentIndex; }
};

struct FHairCardsInterpolationBulkData;

/** Hair cards points interpolation attributes */
struct HAIRSTRANDSCORE_API FHairCardsInterpolationDatas
{
	/** Set the number of interpolated points */
	void SetNum(const uint32 NumPoints);

	/** Reset the interpolated points to 0 */
	void Reset();

	/** Get the number of interpolated points */
	uint32 Num() const { return PointsSimCurvesVertexIndex.Num(); }

	/** Simulation curve indices, ordered by closest influence */
	TArray<int32> PointsSimCurvesIndex;

	/** Closest vertex indices on simulation curve, ordered by closest influence */
	TArray<int32> PointsSimCurvesVertexIndex;

	/** Lerp value between the closest vertex indices and the next one, ordered by closest influence */
	TArray<float> PointsSimCurvesVertexLerp;
};

struct HAIRSTRANDSCORE_API FHairCardsInterpolationBulkData
{
	TArray<FHairCardsInterpolationFormat::Type> Interpolation;

	void Serialize(FArchive& Ar);
};

struct FHairCardsInterpolationResource : public FHairCommonResource
{
	/** Build the hair strands resource */
	FHairCardsInterpolationResource(const FHairCardsInterpolationBulkData& InBulkData);

	/* Init/release buffers */
	virtual void InternalAllocate(FRDGBuilder& GraphBuilder) override;
	virtual void InternalRelease() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairCardsInterplationResource"); }

	/* Return the memory size for GPU resources */
	uint32 GetResourcesSize() const
	{
		uint32 Total = 0;
		Total += GetBufferTotalNumBytes(InterpolationBuffer);
		return Total;
	}

	FRDGExternalBuffer InterpolationBuffer;

	/* Reference to the hair strands interpolation render data */
	const FHairCardsInterpolationBulkData& BulkData;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Meshes

/* Render buffers that will be used for rendering */
struct FHairMeshesRestResource : public FHairCommonResource
{
	/** Build the hair strands resource */
	FHairMeshesRestResource(const FHairMeshesBulkData& BulkData);

	virtual void InitResource() override;
	virtual void ReleaseResource() override;

	/* Init/release buffers */
	virtual void InternalAllocate() override;
	virtual void InternalRelease() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairMeshesRestResource"); }

	/* Return the memory size for GPU resources */
	uint32 GetResourcesSize() const
	{
		uint32 Total = 0;
		Total += BulkData.Positions.GetAllocatedSize();
		Total += BulkData.Normals.GetAllocatedSize();
		Total += BulkData.Indices.GetAllocatedSize();
		Total += BulkData.UVs.GetAllocatedSize();
		return Total;
	}

	uint32 GetVertexCount() const { return BulkData.GetNumVertices(); }
	uint32 GetPrimitiveCount() const { return BulkData.GetNumTriangles(); }

	/* Strand hair rest position buffer */
	FHairCardsVertexBuffer RestPositionBuffer;
	FHairCardIndexBuffer IndexBuffer;	

	FHairCardsVertexBuffer NormalsBuffer;
	FHairCardsVertexBuffer UVsBuffer;

	FSamplerStateRHIRef DepthSampler;
	FSamplerStateRHIRef TangentSampler;
	FSamplerStateRHIRef CoverageSampler;
	FSamplerStateRHIRef AttributeSampler;
	FSamplerStateRHIRef AuxilaryDataSampler;

	FTextureReferenceRHIRef	DepthTexture = nullptr;
	FTextureReferenceRHIRef	CoverageTexture = nullptr;
	FTextureReferenceRHIRef	TangentTexture = nullptr;
	FTextureReferenceRHIRef	AttributeTexture = nullptr;
	FTextureReferenceRHIRef	AuxilaryDataTexture = nullptr;

	/* Reference to the hair strands render data */
	const FHairMeshesBulkData& BulkData;
};


/* Render buffers that will be used for rendering */
struct FHairMeshesDeformedResource : public FHairCommonResource
{
	/** Build the hair strands resource */
	FHairMeshesDeformedResource(const FHairMeshesBulkData& InBulkData, bool bInInitializedData);

	/* Init/release buffers */
	virtual void InternalAllocate(FRDGBuilder& GraphBuilder) override;
	virtual void InternalRelease() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairMeshesDeformedResource"); }

	/* Return the memory size for GPU resources */
	uint32 GetResourcesSize() const
	{
		uint32 Total = 0;
		Total += GetBufferTotalNumBytes(DeformedPositionBuffer[0]);
		Total += GetBufferTotalNumBytes(DeformedPositionBuffer[1]);
		return Total;
	}

	/* Strand hair deformed position buffer (previous and current) */
	FRDGExternalBuffer DeformedPositionBuffer[2];

	/* Reference to the hair strands render data */
	const FHairMeshesBulkData& BulkData;

	/* Whether the GPU data should be initialized with the asset data or not */
	const bool bInitializedData = false;

	/* Whether the GPU data should be initialized with the asset data or not */
	uint32 CurrentIndex = 0;

	enum EFrameType
	{
		Previous,
		Current
	};

	// Helper accessors
	inline uint32 GetIndex(EFrameType T) { return T == EFrameType::Current ? CurrentIndex : 1u - CurrentIndex; }
	inline FRDGExternalBuffer& GetBuffer(EFrameType T) { return DeformedPositionBuffer[GetIndex(T)]; }
	inline void SwapBuffer() { CurrentIndex = 1u - CurrentIndex; }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug data (used for debug visalization but also for texture generation)
void CreateHairStrandsDebugDatas(const FHairStrandsDatas& InData, float WorldVoxelSize, FHairStrandsDebugDatas& Out);
void CreateHairStrandsDebugResources(class FRDGBuilder& GraphBuilder, const FHairStrandsDebugDatas* In, FHairStrandsDebugDatas::FResources* Out);