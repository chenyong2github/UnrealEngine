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

/* Render buffers for root deformation for dynamic meshes */
struct FHairStrandsRestRootResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairStrandsRestRootResource(const FHairStrandsRootData& RootData);
	FHairStrandsRestRootResource(const FHairStrandsDatas* HairStrandsDatas, uint32 LODCount, const TArray<uint32>& NumSamples);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsRestRootResource"); }

	/* Populate GPU LOD data from RootData (this function doesn't initialize resources) */
	void PopulateFromRootData();

	FRWBuffer RootPositionBuffer;
	FRWBuffer RootNormalBuffer;
	FRWBuffer VertexToCurveIndexBuffer;

	struct FLOD
	{
		enum class EStatus { Invalid, Initialized, Completed };

		const bool IsValid() const { return Status == EStatus::Completed; }
		EStatus Status = EStatus::Invalid;
		int32 LODIndex = -1;

		/* Triangle on which a root is attached */
		/* When the projection is done with source to target mesh transfer, the projection indices does not match.
		   In this case we need to separate index computation. The barycentric coords remain the same however. */
		FRWBuffer RootTriangleIndexBuffer;
		FRWBuffer RootTriangleBarycentricBuffer;

		/* Strand hair roots translation and rotation in rest position relative to the bound triangle. Positions are relative to the rest root center */
		FRWBuffer RestRootTrianglePosition0Buffer;
		FRWBuffer RestRootTrianglePosition1Buffer;
		FRWBuffer RestRootTrianglePosition2Buffer;

		/* Strand hair mesh interpolation matrix and sample indices */
		uint32 SampleCount = 0;
		FRWBuffer MeshInterpolationWeightsBuffer;
		FRWBuffer MeshSampleIndicesBuffer;
		FRWBuffer RestSamplePositionsBuffer;
	};

	/* Store the hair projection information for each mesh LOD */
	TArray<FLOD> LODs;

	/* Store CPU data for root info & root binding */
	FHairStrandsRootData RootData;
};

/* Render buffers for root deformation for dynamic meshes */
struct FHairStrandsDeformedRootResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairStrandsDeformedRootResource();
	FHairStrandsDeformedRootResource(const FHairStrandsRestRootResource* InRestResources);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsDeformedRootResource"); }

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
		FRWBuffer DeformedRootTrianglePosition0Buffer;
		FRWBuffer DeformedRootTrianglePosition1Buffer;
		FRWBuffer DeformedRootTrianglePosition2Buffer;

		/* Strand hair mesh interpolation matrix and sample indices */
		uint32 SampleCount = 0;
		FRWBuffer DeformedSamplePositionsBuffer;
		FRWBuffer MeshSampleWeightsBuffer;
	};

	/* Store the hair projection information for each mesh LOD */
	uint32 RootCount = 0;
	TArray<FLOD> LODs;
};

/* Render buffers that will be used for rendering */
struct FHairStrandsRestResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairStrandsRestResource(const FHairStrandsDatas::FRenderData& HairStrandRenderData, const FVector& PositionOffset);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsResource"); }

	/* Strand hair rest position buffer */
	FRWBuffer RestPositionBuffer;

	/* Strand hair attribute buffer */
	FRWBuffer AttributeBuffer;

	/* Strand hair material buffer */
	FRWBuffer MaterialBuffer;
	
	/* Position offset as the rest positions are expressed in relative coordinate (16bits) */
	FVector PositionOffset = FVector::ZeroVector;

	/* Reference to the hair strands render data */
	const FHairStrandsDatas::FRenderData& RenderData;

	inline uint32 GetVertexCount() const { return RenderData.Positions.Num() / FHairStrandsPositionFormat::ComponentCount; }
};

struct FHairStrandsDeformedResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairStrandsDeformedResource(const FHairStrandsDatas::FRenderData& HairStrandRenderData, bool bInitializeData);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsDeformedResource"); }

	/* Strand hair deformed position buffer (previous and current) */
	FRWBuffer DeformedPositionBuffer[2];

	/* Strand hair tangent buffer */
	FRWBuffer TangentBuffer;

	/* Position offset as the deformed positions are expressed in relative coordinate (16bits) */
	FVector PositionOffset[2] = {FVector::ZeroVector, FVector::ZeroVector};

	/* Reference to the hair strands render data */
	const FHairStrandsDatas::FRenderData& RenderData;

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
	inline uint32 GetIndex(EFrameType T) const					{ return T == EFrameType::Current ? CurrentIndex : 1u - CurrentIndex; }
	inline FRWBuffer& GetBuffer(EFrameType T)					{ return DeformedPositionBuffer[GetIndex(T)];  }
	inline FVector& GetPositionOffset(EFrameType T)				{ return PositionOffset[GetIndex(T)]; }
	inline const FVector& GetPositionOffset(EFrameType T) const { return PositionOffset[GetIndex(T)]; }
	inline void SwapBuffer()									{ CurrentIndex = 1u - CurrentIndex; }
};

struct FHairStrandsClusterCullingResource : public FRenderResource
{
	FHairStrandsClusterCullingResource(const FHairStrandsClusterCullingData& Data);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsClusterResource"); }

	/* Cluster info buffer */
	FRWBufferStructured ClusterInfoBuffer;	 
	FRWBufferStructured ClusterLODInfoBuffer;

	/* VertexId => ClusterId to know which AABB to contribute to*/
	FReadBuffer VertexToClusterIdBuffer;

	/* Concatenated data for each cluster: list of VertexId pointed to by ClusterInfoBuffer */
	FReadBuffer ClusterVertexIdBuffer;

	const FHairStrandsClusterCullingData& Data;
};

struct FHairStrandsInterpolationResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairStrandsInterpolationResource(const FHairStrandsInterpolationDatas::FRenderData& InterpolationRenderData, const FHairStrandsDatas& SimDatas);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsInterplationResource"); }

	FRWBuffer Interpolation0Buffer;
	FRWBuffer Interpolation1Buffer;

	// For debug purpose only (should be remove once all hair simulation is correctly handled)
	FRWBuffer SimRootPointIndexBuffer;
	TArray<FHairStrandsRootIndexFormat::Type> SimRootPointIndex;

	/* Reference to the hair strands interpolation render data */
	const FHairStrandsInterpolationDatas::FRenderData& RenderData;
};

#if RHI_RAYTRACING
struct FHairStrandsRaytracingResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairStrandsRaytracingResource(const FHairStrandsDatas& HairStrandsDatas);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairStrandsRaytracingResource"); }

	FRWBuffer PositionBuffer;
	FRayTracingGeometry RayTracingGeometry;
	uint32 VertexCount;
};
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cards

class FHairCardIndexBuffer : public FIndexBuffer
{
public:
	const TArray<FHairCardsIndexFormat::Type>& Indices;
	FHairCardIndexBuffer(const TArray<FHairCardsIndexFormat::Type>& InIndices) :Indices(InIndices) {}
	virtual void InitRHI() override;
};

/* Render buffers that will be used for rendering */
struct FHairCardsRestResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairCardsRestResource(const FHairCardsDatas::FRenderData& HairCardsRenderData, uint32 VertexCount, uint32 PrimitiveCount);

	virtual void InitResource() override;
	virtual void ReleaseResource() override;

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairCardsResource"); }

	/* Strand hair rest position buffer */
	FRWBuffer RestPositionBuffer;
	FHairCardIndexBuffer RestIndexBuffer;
	uint32 VertexCount;
	uint32 PrimitiveCount;

	FRWBuffer NormalsBuffer;
	FRWBuffer UVsBuffer;

	FSamplerStateRHIRef DepthSampler;
	FSamplerStateRHIRef TangentSampler;
	FSamplerStateRHIRef CoverageSampler;
	FSamplerStateRHIRef AttributeSampler;

	FTextureReferenceRHIRef	DepthTexture = nullptr;
	FTextureReferenceRHIRef	CoverageTexture = nullptr;
	FTextureReferenceRHIRef	TangentTexture = nullptr;
	FTextureReferenceRHIRef	AttributeTexture = nullptr;

	/* Reference to the hair strands render data */
	const FHairCardsDatas::FRenderData& RenderData;
};

/* Render buffers that will be used for rendering */
struct FHairCardsProceduralResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairCardsProceduralResource(const FHairCardsProceduralDatas::FRenderData& HairCardsRenderData, const FIntPoint& AtlasResolution, const FHairCardsVoxel& InVoxel);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairCardsResource"); }

	/* Strand hair rest position buffer */		
	uint32 CardBoundCount;
	FIntPoint AtlasResolution;

	FRWBuffer AtlasRectBuffer;
	FRWBuffer LengthBuffer;
	FRWBuffer CardItToClusterBuffer;
	FRWBuffer ClusterIdToVerticesBuffer;
	FRWBuffer ClusterBoundBuffer;
	FRWBuffer CardsStrandsPositions;
	FRWBuffer CardsStrandsAttributes;

	FHairCardsVoxel CardVoxel;

	/* Position offset as the rest positions are expressed in relative coordinate (16bits) */
	//FVector PositionOffset = FVector::ZeroVector;

	/* Reference to the hair strands render data */
	const FHairCardsProceduralDatas::FRenderData& RenderData;
};

struct FHairCardsDeformedResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairCardsDeformedResource(const FHairCardsDatas::FRenderData& HairStrandRenderData, bool bInitializeData);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairCardsDeformedResource"); }

	/* Strand hair deformed position buffer (previous and current) */
	FRWBuffer DeformedPositionBuffer[2];

	/* Reference to the hair strands render data */
	const FHairCardsDatas::FRenderData& RenderData;

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
	inline uint32 GetIndex(EFrameType T)			{ return T == EFrameType::Current ? CurrentIndex : 1u - CurrentIndex; }
	inline FRWBuffer& GetBuffer(EFrameType T)		{ return DeformedPositionBuffer[GetIndex(T)];  }
	inline void SwapBuffer()						{ CurrentIndex = 1u - CurrentIndex; }
};

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

	struct FRenderData
	{
		TArray<FHairCardsInterpolationFormat::Type> Interpolation;
	} RenderData;
};

FArchive& operator<<(FArchive& Ar, FHairCardsInterpolationDatas& CardInterpData);

struct FHairCardsInterpolationResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairCardsInterpolationResource(const FHairCardsInterpolationDatas::FRenderData& InterpolationRenderData);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairCardsInterplationResource"); }

	FRWBuffer InterpolationBuffer;

	/* Reference to the hair strands interpolation render data */
	const FHairCardsInterpolationDatas::FRenderData& RenderData;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Meshes

/* Render buffers that will be used for rendering */
struct FHairMeshesRestResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairMeshesRestResource(const FHairMeshesDatas::FRenderData& HairMeshesRenderData, uint32 VertexCount, uint32 PrimitiveCount);

	virtual void InitResource() override;
	virtual void ReleaseResource() override;

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairMeshesRestResource"); }

	/* Strand hair rest position buffer */
	FRWBuffer PositionBuffer;
	FHairCardIndexBuffer IndexBuffer;
	uint32 VertexCount;
	uint32 PrimitiveCount;

	FRWBuffer NormalsBuffer;
	FRWBuffer UVsBuffer;

	FSamplerStateRHIRef DepthSampler;
	FSamplerStateRHIRef TangentSampler;
	FSamplerStateRHIRef CoverageSampler;
	FSamplerStateRHIRef AttributeSampler;

	FTextureReferenceRHIRef	DepthTexture = nullptr;
	FTextureReferenceRHIRef	CoverageTexture = nullptr;
	FTextureReferenceRHIRef	TangentTexture = nullptr;
	FTextureReferenceRHIRef	AttributeTexture = nullptr;

	/* Reference to the hair strands render data */
	const FHairMeshesDatas::FRenderData& RenderData;
};


/* Render buffers that will be used for rendering */
struct FHairMeshesDeformedResource : public FRenderResource
{
	/** Build the hair strands resource */
	FHairMeshesDeformedResource(const FHairMeshesDatas::FRenderData& HairMeshesRenderData, bool bInInitializedData);

	/* Init the buffer */
	virtual void InitRHI() override;

	/* Release the buffer */
	virtual void ReleaseRHI() override;

	/* Get the resource name */
	virtual FString GetFriendlyName() const override { return TEXT("FHairMeshesDeformedResource"); }

	/* Strand hair deformed position buffer (previous and current) */
	FRWBuffer DeformedPositionBuffer[2];

	/* Reference to the hair strands render data */
	const FHairMeshesDatas::FRenderData& RenderData;

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
	inline FRWBuffer& GetBuffer(EFrameType T) { return DeformedPositionBuffer[GetIndex(T)]; }
	inline void SwapBuffer() { CurrentIndex = 1u - CurrentIndex; }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug data (used for debug visalization but also for texture generation)
void CreateHairStrandsDebugDatas(const FHairStrandsDatas& InData, float WorldVoxelSize, FHairStrandsDebugDatas& Out);
void CreateHairStrandsDebugResources(class FRDGBuilder& GraphBuilder, const FHairStrandsDebugDatas* In, FHairStrandsDebugDatas::FResources* Out);