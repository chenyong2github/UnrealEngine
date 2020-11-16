// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/LowLevelMemTracker.h"
#include "LocalVertexFactory.h"
#include "GrowOnlySpanAllocator.h"
#include "UnifiedBuffer.h"
#include "RenderGraphResources.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "SceneManagement.h"
#include "Materials/MaterialInterface.h"
#include "BodySetupEnums.h"
#include "Serialization/BulkData.h"
#include "Misc/MemoryReadStream.h"

/** Whether Nanite::FSceneProxy should store data and enable codepaths needed for debug rendering. */
#if PLATFORM_WINDOWS
#define NANITE_ENABLE_DEBUG_RENDERING (!(UE_BUILD_SHIPPING || UE_BUILD_TEST) || WITH_EDITOR)
#else
#define NANITE_ENABLE_DEBUG_RENDERING 0
#endif

#define MAX_STREAMING_REQUESTS					( 128u * 1024u )										// must match define in NaniteDataDecode.ush
#define MAX_CLUSTER_TRIANGLES					128
#define MAX_CLUSTER_VERTICES					256
#define MAX_CLUSTER_INDICES						( MAX_CLUSTER_TRIANGLES * 3 )
#define MAX_NANITE_UVS							4														// must match define in NaniteDataDecode.ush
#define NUM_ROOT_PAGES							1u														// Should probably be made a per-resource option

#define USE_STRIP_INDICES						1														// must match define in NaniteDataDecode.ush

#define CLUSTER_PAGE_GPU_SIZE_BITS				17														// must match define in NaniteDataDecode.ush
#define CLUSTER_PAGE_GPU_SIZE					( 1 << CLUSTER_PAGE_GPU_SIZE_BITS )						// must match define in NaniteDataDecode.ush
#define CLUSTER_PAGE_DISK_SIZE					( CLUSTER_PAGE_GPU_SIZE * 2 )							// must match define in NaniteDataDecode.ush
#define MAX_CLUSTERS_PER_PAGE_BITS				11														// must match define in NaniteDataDecode.ush
#define MAX_CLUSTERS_PER_PAGE_MASK				( ( 1 << MAX_CLUSTERS_PER_PAGE_BITS ) - 1 )				// must match define in NaniteDataDecode.ush
#define MAX_CLUSTERS_PER_PAGE					( 1 << MAX_CLUSTERS_PER_PAGE_BITS )						// must match define in NaniteDataDecode.ush
#define MAX_CLUSTERS_PER_GROUP_BITS				9														// must match define in NaniteDataDecode.ush
#define MAX_CLUSTERS_PER_GROUP_MASK				( ( 1 << MAX_CLUSTERS_PER_GROUP_BITS ) - 1 )			// must match define in NaniteDataDecode.ush
#define MAX_CLUSTERS_PER_GROUP					( ( 1 << MAX_CLUSTERS_PER_GROUP_BITS ) - 1 )			// must match define in NaniteDataDecode.ush
#define MAX_CLUSTERS_PER_GROUP_TARGET			128														// what we are targeting. MAX_CLUSTERS_PER_GROUP needs to be large 
																										// enough that it won't overflow after constraint-based splitting
#define MAX_HIERACHY_CHILDREN_BITS				6														// must match define in NaniteDataDecode.ush
#define MAX_HIERACHY_CHILDREN					( 1 << MAX_HIERACHY_CHILDREN_BITS )						// must match define in NaniteDataDecode.ush
#define MAX_GPU_PAGES_BITS						13														// must match define in NaniteDataDecode.ush
#define	MAX_GPU_PAGES							( 1 << MAX_GPU_PAGES_BITS )								// must match define in NaniteDataDecode.ush
#define MAX_INSTANCES_BITS						24														// must match define in NaniteDataDecode.ush
#define MAX_INSTANCES							( 1 << MAX_INSTANCES_BITS )								// must match define in NaniteDataDecode.ush
#define MAX_NODES_PER_PRIMITIVE_BITS			16														// must match define in NaniteDataDecode.ush
#define NUM_CULLING_FLAG_BITS					3														// must match define in NaniteDataDecode.ush
#define MAX_RESOURCE_PAGES_BITS					20
#define MAX_RESOURCE_PAGES						(1 << MAX_RESOURCE_PAGES_BITS)
#define MAX_GROUP_PARTS_BITS					3
#define MAX_GROUP_PARTS_MASK					((1 << MAX_GROUP_PARTS_BITS) - 1)
#define MAX_GROUP_PARTS							(1 << MAX_GROUP_PARTS_BITS)

#define NUM_PACKED_CLUSTER_FLOAT4S				8														// must match define in NaniteDataDecode.ush

#define POSITION_QUANTIZATION_BITS				10
#define POSITION_QUANTIZATION_MASK 				((1u << POSITION_QUANTIZATION_BITS) - 1u)
#define NORMAL_QUANTIZATION_BITS				 9

#define MAX_TEXCOORD_QUANTIZATION_BITS			15														// must match define in NaniteDataDecode.ush
#define MAX_COLOR_QUANTIZATION_BITS				 8														// must match define in NaniteDataDecode.ush

#define MAX_TRANSCODE_GROUPS_PER_PAGE			128														// must match define in NaniteDataDecode.ush

#define VERTEX_COLOR_MODE_WHITE					0
#define VERTEX_COLOR_MODE_CONSTANT				1
#define VERTEX_COLOR_MODE_VARIABLE				2

// TODO: Only needed while there are multiple graphs instead of one big one (or a more intelligent resource reuse).
#define NANITE_USE_SCRATCH_BUFFERS				1

#define NANITE_CLUSTER_FLAG_LEAF				0x1

DECLARE_STATS_GROUP( TEXT("Nanite"), STATGROUP_Nanite, STATCAT_Advanced );

DECLARE_GPU_STAT_NAMED_EXTERN(NaniteStreaming, TEXT("Nanite Streaming"));
DECLARE_GPU_STAT_NAMED_EXTERN(NaniteReadback, TEXT("Nanite Readback"));

LLM_DECLARE_TAG_API(Nanite, ENGINE_API);

class UStaticMesh;
class UBodySetup;
class FDistanceFieldVolumeData;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UHierarchicalInstancedStaticMeshComponent;

namespace Nanite
{

struct FUIntVector
{
	uint32 X, Y, Z;

	bool operator==(const FUIntVector& V) const
	{
		return X == V.X && Y == V.Y && Z == V.Z;
	}

	FORCEINLINE friend FArchive& operator<<(FArchive& Ar, FUIntVector& V)
	{
		Ar << V.X;
		Ar << V.Y;
		Ar << V.Z;
		return Ar;
	}
};

struct FPackedHierarchyNode
{
	FSphere		LODBounds[64];
	FSphere		Bounds[64];
	struct
	{
		uint32	MinLODError_MaxParentLODError;
		uint32	ChildStartReference;
		uint32	ResourcePageIndex_NumPages_GroupPartSize;
	} Misc[64];
};

struct FMaterialTriangle
{
	uint32 Index0;
	uint32 Index1;
	uint32 Index2;
	uint32 MaterialIndex;
	uint32 RangeCount;
};

FORCEINLINE uint32 GetBits(uint32 Value, uint32 NumBits, uint32 Offset)
{
	uint32 Mask = (1u << NumBits) - 1u;
	return (Value >> Offset) & Mask;
}

FORCEINLINE void SetBits(uint32& Value, uint32 Bits, uint32 NumBits, uint32 Offset)
{
	uint32 Mask = (1u << NumBits) - 1u;
	check(Bits <= Mask);
	Mask <<= Offset;
	Value = (Value & ~Mask) | (Bits << Offset);
}


// Packed TriCluster as it is used by the GPU
struct FPackedTriCluster
{
	// Members needed for rasterization
	FUIntVector	QuantizedPosStart;
	uint32		PositionOffset;

	FVector		MeshBoundsMin;
	uint32		IndexOffset;

	FVector		MeshBoundsDelta;
	uint32		NumVerts_NumTris_BitsPerIndex_QuantizedPosShift;		// NumVerts:9, NumTris:8, BitsPerIndex:4, QuantizedPosShift:6
	
	// Members needed for culling
	FSphere		LODBounds;

	FVector		BoxBoundsCenter;
	uint32		LODErrorAndEdgeLength;
	
	FVector		BoxBoundsExtent;
	uint32		Flags;

	// Members needed by materials
	uint32		AttributeOffset_BitsPerAttribute;					// AttributeOffset: 22, BitsPerAttribute: 10
	uint32		DecodeInfoOffset_NumUVs_ColorMode;					// DecodeInfoOffset: 22, NumUVs: 3, ColorMode: 2
	uint32		UV_Prec;											// U0:4, V0:4, U1:4, V1:4, U2:4, V2:4, U3:4, V3:4
	uint32		PackedMaterialInfo;

	uint32		ColorMin;
	uint32		ColorBits;		// R:4, G:4, B:4, A:4
	uint32		GroupIndex;		// Debug only
	uint32		Pad0;

	uint32		GetNumVerts() const					{ return GetBits(NumVerts_NumTris_BitsPerIndex_QuantizedPosShift, 9, 0); }
	uint32		GetNumTris() const					{ return GetBits(NumVerts_NumTris_BitsPerIndex_QuantizedPosShift, 8, 9); }
	uint32		GetBitsPerIndex() const				{ return GetBits(NumVerts_NumTris_BitsPerIndex_QuantizedPosShift, 4, 9+8); }
	uint32		GetQuantizedPosShift() const		{ return GetBits(NumVerts_NumTris_BitsPerIndex_QuantizedPosShift, 6, 9+8+4); }

	uint32		GetAttributeOffset() const			{ return GetBits(AttributeOffset_BitsPerAttribute, 22, 0); }
	void		SetAttributeOffset(uint32 Offset)	{ SetBits(AttributeOffset_BitsPerAttribute, Offset, 22, 0); }
	uint32		GetBitsPerAttribute() const			{ return GetBits(AttributeOffset_BitsPerAttribute, 10, 22); }
	void		SetBitsPerAttribute(uint32 Bits)	{ SetBits(AttributeOffset_BitsPerAttribute, Bits, 10, 22); }

	void		SetDecodeInfoOffset(uint32 Offset)	{ SetBits(DecodeInfoOffset_NumUVs_ColorMode, Offset, 22, 0); }
	void		SetNumUVs(uint32 Num)				{ SetBits(DecodeInfoOffset_NumUVs_ColorMode, Num, 3, 22); }
	void		SetColorMode(uint32 Mode)			{ SetBits(DecodeInfoOffset_NumUVs_ColorMode, Mode, 2, 22+3); }
};

struct FPageStreamingState
{
	uint32			BulkOffset;
	uint32			BulkSize;
	uint32			PageUncompressedSize;
	uint32			DependenciesStart;
	uint32			DependenciesNum;
};

class FHierarchyFixup
{
public:
	FHierarchyFixup() {}

	FHierarchyFixup( uint32 InPageIndex, uint32 NodeIndex, uint32 ChildIndex, uint32 InClusterGroupPartStartIndex, uint32 PageDependencyStart, uint32 PageDependencyNum )
	{
		check(InPageIndex < MAX_RESOURCE_PAGES);
		PageIndex = InPageIndex;

		check( NodeIndex < ( 1 << ( 32 - MAX_HIERACHY_CHILDREN_BITS ) ) );
		check( ChildIndex < MAX_HIERACHY_CHILDREN );
		check( InClusterGroupPartStartIndex < ( 1 << ( 32 - MAX_CLUSTERS_PER_GROUP_BITS ) ) );
		HierarchyNodeAndChildIndex = ( NodeIndex << MAX_HIERACHY_CHILDREN_BITS ) | ChildIndex;
		ClusterGroupPartStartIndex = InClusterGroupPartStartIndex;

		check(PageDependencyStart < MAX_RESOURCE_PAGES);
		check(PageDependencyNum <= MAX_GROUP_PARTS_MASK);
		PageDependencyStartAndNum = (PageDependencyStart << MAX_GROUP_PARTS_BITS) | PageDependencyNum;
	}

	uint32 GetPageIndex() const						{ return PageIndex; }
	uint32 GetNodeIndex() const						{ return HierarchyNodeAndChildIndex >> MAX_HIERACHY_CHILDREN_BITS; }
	uint32 GetChildIndex() const					{ return HierarchyNodeAndChildIndex & ( MAX_HIERACHY_CHILDREN - 1 ); }
	uint32 GetClusterGroupPartStartIndex() const	{ return ClusterGroupPartStartIndex; }
	uint32 GetPageDependencyStart() const			{ return PageDependencyStartAndNum >> MAX_GROUP_PARTS_BITS; }
	uint32 GetPageDependencyNum() const				{ return PageDependencyStartAndNum & MAX_GROUP_PARTS_MASK; }

	uint32 PageIndex;
	uint32 HierarchyNodeAndChildIndex;
	uint32 ClusterGroupPartStartIndex;
	uint32 PageDependencyStartAndNum;
};

class FClusterFixup
{
public:
	FClusterFixup() {}

	FClusterFixup( uint32 PageIndex, uint32 ClusterIndex, uint32 PageDependencyStart, uint32 PageDependencyNum )
	{
		check( PageIndex < ( 1 << ( 32 - MAX_CLUSTERS_PER_GROUP_BITS ) ) );
		check( ClusterIndex < MAX_CLUSTERS_PER_PAGE );
		PageAndClusterIndex = ( PageIndex << MAX_CLUSTERS_PER_PAGE_BITS ) | ClusterIndex;

		check(PageDependencyStart < MAX_RESOURCE_PAGES);
		check(PageDependencyNum <= MAX_GROUP_PARTS_MASK);
		PageDependencyStartAndNum = (PageDependencyStart << MAX_GROUP_PARTS_BITS) | PageDependencyNum;
	}
	
	uint32 GetPageIndex() const				{ return PageAndClusterIndex >> MAX_CLUSTERS_PER_PAGE_BITS; }
	uint32 GetClusterIndex() const			{ return PageAndClusterIndex & (MAX_CLUSTERS_PER_PAGE - 1u); }
	uint32 GetPageDependencyStart() const	{ return PageDependencyStartAndNum >> MAX_GROUP_PARTS_BITS; }
	uint32 GetPageDependencyNum() const		{ return PageDependencyStartAndNum & MAX_GROUP_PARTS_MASK; }

	uint32 PageAndClusterIndex;
	uint32 PageDependencyStartAndNum;
};

struct FPageDiskHeader
{
	uint32 GpuSize;
	uint32 NumClusters;
	uint32 NumRawFloat4s;
	uint32 NumTexCoords;
	uint32 DecodeInfoOffset;
	uint32 StripBitmaskOffset;
	uint32 VertexRefBitmaskOffset;
};

struct FClusterDiskHeader
{
	uint32 IndexDataOffset;
	uint32 VertexRefDataOffset;
	uint32 PositionDataOffset;
	uint32 AttributeDataOffset;
	uint32 NumPrevRefVerticesBeforeDwords;
	uint32 NumPrevNewVerticesBeforeDwords;
};

class FFixupChunk	//TODO: rename to something else
{
public:
	struct FHeader
	{
		uint16 NumClusters = 0;
		uint16 NumHierachyFixups = 0;
		uint16 NumClusterFixups = 0;
		uint16 Pad = 0;
	} Header;
	
	uint8 Data[ sizeof(FHierarchyFixup) * MAX_CLUSTERS_PER_PAGE + sizeof( FClusterFixup ) * MAX_CLUSTERS_PER_PAGE ];	// One hierarchy fixup per cluster and at most one cluster fixup per cluster.

	FClusterFixup&		GetClusterFixup( uint32 Index ) const { check( Index < Header.NumClusterFixups );  return ( (FClusterFixup*)( Data + Header.NumHierachyFixups * sizeof( FHierarchyFixup ) ) )[ Index ]; }
	FHierarchyFixup&	GetHierarchyFixup( uint32 Index ) const { check( Index < Header.NumHierachyFixups ); return ((FHierarchyFixup*)Data)[ Index ]; }
	uint32				GetSize() const { return sizeof( Header ) + Header.NumHierachyFixups * sizeof( FHierarchyFixup ) + Header.NumClusterFixups * sizeof( FClusterFixup ); }
};

struct FResources
{
	// Persistent State
	TArray< uint8 >					RootClusterPage;		// Root page is loaded on resource load, so we always have something to draw.
	FByteBulkData					StreamableClusterPages;	// Remaining pages are streamed on demand.
	TArray< uint16 >				ImposterAtlas;
	TArray< FPackedHierarchyNode >	HierarchyNodes;
	TArray< FPageStreamingState >	PageStreamingStates;
	TArray< uint32 >				PageDependencies;
	bool	bLZCompressed			= false;

	// Runtime State
	uint32	RuntimeResourceID		= 0xFFFFFFFFu;
	int32	HierarchyOffset			= INDEX_NONE;
	int32	RootPageIndex			= INDEX_NONE;
	
#if WITH_EDITOR
	// HACK: Need to cache this because Geometry Collection might serialize the same object more than once.
	// BulkData has to be kept alive for the duration of serialization and you are not allowed to update it more than once.
	bool							bHasDecompressedData = false;
	TArray<uint8>					DecompressedRootClusterPage;
	TArray<FPageStreamingState>		DecompressedPageStreamingStates;
	FByteBulkData					DecompressedStreamableClusterPages;
#endif
	
	ENGINE_API void InitResources();
	ENGINE_API void ReleaseResources();

	ENGINE_API void Serialize(FArchive& Ar, UObject* Owner);
};

class FSceneProxyBase : public FPrimitiveSceneProxy
{
public:
	struct FMaterialSection
	{
		UMaterialInterface* Material = nullptr;
#if WITH_EDITOR
		HHitProxy* HitProxy = nullptr;
#endif
	};

public:
	ENGINE_API SIZE_T GetTypeHash() const override;

	FSceneProxyBase(UPrimitiveComponent* Component)
		: FPrimitiveSceneProxy(Component)
	{
	}

	virtual ~FSceneProxyBase() = default;

	virtual bool IsNaniteMesh() const override
	{
		return true;
	}

	virtual bool IsAlwaysVisible() const override
	{
		return true;
	}

	static bool IsNaniteRenderable(FMaterialRelevance MaterialRelevance)
	{
		return MaterialRelevance.bOpaque &&
			!MaterialRelevance.bDecal &&
			!MaterialRelevance.bMasked &&
			!MaterialRelevance.bNormalTranslucency &&
			!MaterialRelevance.bSeparateTranslucency;
	}

	virtual bool CanBeOccluded() const override
	{
		// Disable slow occlusion paths(Nanite does its own occlusion culling)
		return false;
	}

	inline const TArray<FMaterialSection>& GetMaterialSections() const
	{
		return MaterialSections;
	}

	virtual const TArray<FPrimitiveInstance>* GetPrimitiveInstances() const
	{
		return &Instances;
	}

	virtual TArray<FPrimitiveInstance>* GetPrimitiveInstances()
	{
		return &Instances;
	}

	// Nanite always uses LOD 0, and performs custom LOD streaming.
	virtual uint8 GetCurrentFirstLODIdx_RenderThread() const override { return 0; }

protected:
	TArray<FMaterialSection> MaterialSections;
	TArray<FPrimitiveInstance> Instances;
};

class FSceneProxy : public FSceneProxyBase
{
public:
	FSceneProxy(UStaticMeshComponent* Component);
	FSceneProxy(UInstancedStaticMeshComponent* Component);
	FSceneProxy(UHierarchicalInstancedStaticMeshComponent* Component);

	virtual ~FSceneProxy() = default;

public:
	// FPrimitiveSceneProxy interface.
	virtual FPrimitiveViewRelevance	GetViewRelevance( const FSceneView* View ) const override;
#if WITH_EDITOR
	virtual HHitProxy*				CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy> >& OutHitProxies) override;
#endif
	virtual void					DrawStaticElements(FStaticPrimitiveDrawInterface* PDI) override;
	virtual void					GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

#if RHI_RAYTRACING
	virtual bool					IsRayTracingRelevant() const { return true; }
	virtual bool					IsRayTracingStaticRelevant() const { return false; }
	virtual void					GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<struct FRayTracingInstance>& OutRayTracingInstances) override;
#endif

	virtual uint32					GetMemoryFootprint() const override;

	virtual void GetLCIs(FLCIArray& LCIs) override
	{
		FLightCacheInterface* LCI = &MeshInfo;
		LCIs.Add(LCI);
	}

	virtual void GetDistancefieldAtlasData(FBox& LocalVolumeBounds, FVector2D& OutDistanceMinMax, FIntVector& OutBlockMin, FIntVector& OutBlockSize, bool& bOutBuiltAsIfTwoSided, float& SelfShadowBias, bool& bOutThrottled) const override;
	virtual void GetDistancefieldInstanceData(TArray<FMatrix>& ObjectLocalToWorldTransforms) const override;
	virtual bool HasDistanceFieldRepresentation() const override;

	virtual const FCardRepresentationData* GetMeshCardRepresentation() const override;

protected:
	virtual void CreateRenderThreadResources() override;

	class FMeshInfo : public FLightCacheInterface
	{
	public:
		FMeshInfo(const UStaticMeshComponent* InComponent);

		// FLightCacheInterface.
		virtual FLightInteraction GetInteraction(const FLightSceneProxy* LightSceneProxy) const override;

	private:
		TArray<FGuid> IrrelevantLights;
	};

	bool IsCollisionView(const FEngineShowFlags& EngineShowFlags, bool& bDrawSimpleCollision, bool& bDrawComplexCollision) const;

protected:
	FMeshInfo MeshInfo;

	FResources* Resources = nullptr;

	const FStaticMeshRenderData* RenderData;
	const FDistanceFieldVolumeData* DistanceFieldData;
	const FCardRepresentationData* CardRepresentationData;

	// TODO: Should probably calculate this on the materials array above instead of on the component
	//       Null and !Opaque are assigned default material unlike the component material relevance.
	FMaterialRelevance MaterialRelevance;

	uint32 bCastShadow : 1;
	uint32 bReverseCulling : 1;
	uint32 bHasMaterialErrors : 1;

	const UStaticMesh* StaticMesh = nullptr;

#if RHI_RAYTRACING
	TArray<FRayTracingGeometry*> RayTracingGeometries;
#endif

#if NANITE_ENABLE_DEBUG_RENDERING
	AActor* Owner;

	/** LightMap resolution used for VMI_LightmapDensity */
	int32 LightMapResolution;

	/** Body setup for collision debug rendering */
	UBodySetup* BodySetup;

	/** Collision trace flags */
	ECollisionTraceFlag CollisionTraceFlag;

	/** Collision Response of this component */
	FCollisionResponseContainer CollisionResponse;

	/** LOD used for collision */
	int32 LODForCollision;

	/** Draw mesh collision if used for complex collision */
	uint32 bDrawMeshCollisionIfComplex : 1;

	/** Draw mesh collision if used for simple collision */
	uint32 bDrawMeshCollisionIfSimple : 1;
#endif
};

/*
 * GPU side buffers containing Nanite resource data.
 */
class FGlobalResources : public FRenderResource
{
public:
	struct PassBuffers
	{
		TRefCountPtr<FRDGPooledBuffer> NodesBuffer;

		// Used for statistics
		TRefCountPtr<FRDGPooledBuffer> StatsRasterizeArgsSWHWBuffer;
		TRefCountPtr<FRDGPooledBuffer> StatsCandidateClustersArgsBuffer;

	#if NANITE_USE_SCRATCH_BUFFERS
		// Used for scratch memory (transient only)
		TRefCountPtr<FRDGPooledBuffer> ScratchCandidateClustersBuffer;
	#endif
	};

	// Used for statistics
	uint32 StatsRenderFlags = 0;
	uint32 StatsDebugFlags  = 0;

public:
	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

	ENGINE_API void	Update(FRDGBuilder& GraphBuilder); // Called once per frame before any Nanite rendering has occurred.

	ENGINE_API static uint32 GetMaxInstances();
	ENGINE_API static uint32 GetMaxClusters();
	ENGINE_API static uint32 GetMaxNodes();

	inline PassBuffers& GetMainPassBuffers() { return MainPassBuffers; }
	inline PassBuffers& GetPostPassBuffers() { return PostPassBuffers; }

	TRefCountPtr<FRDGPooledBuffer>& GetStatsBufferRef() { return StatsBuffer; }
	TRefCountPtr<FRDGPooledBuffer>& GetStructureBufferStride8() { return StructureBufferStride8; }

#if NANITE_USE_SCRATCH_BUFFERS
	TRefCountPtr<FRDGPooledBuffer>& GetPrimaryVisibleClustersBufferRef() { return PrimaryVisibleClustersBuffer; }
	TRefCountPtr<FRDGPooledBuffer>& GetScratchVisibleClustersBufferRef() { return ScratchVisibleClustersBuffer; }
	TRefCountPtr<FRDGPooledBuffer>& GetScratchOccludedInstancesBufferRef() { return ScratchOccludedInstancesBuffer; }
#endif

	FVertexFactory* GetVertexFactory() { return VertexFactory; }
	
private:
	PassBuffers MainPassBuffers;
	PassBuffers PostPassBuffers;

	class FVertexFactory* VertexFactory = nullptr;

	// Used for statistics
	TRefCountPtr<FRDGPooledBuffer> StatsBuffer;

	// Dummy structured buffer with stride8
	TRefCountPtr<FRDGPooledBuffer> StructureBufferStride8;

#if NANITE_USE_SCRATCH_BUFFERS
	TRefCountPtr<FRDGPooledBuffer> PrimaryVisibleClustersBuffer;
	// Used for scratch memory (transient only)
	TRefCountPtr<FRDGPooledBuffer> ScratchVisibleClustersBuffer;
	TRefCountPtr<FRDGPooledBuffer> ScratchOccludedInstancesBuffer;
#endif
};

extern ENGINE_API TGlobalResource< FGlobalResources > GGlobalResources;

} // namespace Nanite
