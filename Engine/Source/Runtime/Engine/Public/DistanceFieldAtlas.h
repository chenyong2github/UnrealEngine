// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DistanceFieldAtlas.h
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Containers/LockFreeList.h"
#include "ProfilingDebugging/ResourceSize.h"
#include "Engine/EngineTypes.h"
#include "UObject/GCObject.h"
#include "RenderResource.h"
#include "RenderingThread.h"
#include "TextureLayout3d.h"
#include "Templates/UniquePtr.h"

class FDistanceFieldVolumeData;
class UStaticMesh;
class UTexture2D;

template <class T> class TLockFreePointerListLIFO;

/** Represents a distance field volume texture for a single UStaticMesh. */
class ENGINE_API FDistanceFieldVolumeTexture
{
public:
	FDistanceFieldVolumeTexture(const class FDistanceFieldVolumeData& InVolumeData) :
		VolumeData(InVolumeData),
		AtlasAllocationMin(FIntVector(-1, -1, -1)),
		SizeInAtlas(FIntVector::ZeroValue),
		bReferencedByAtlas(false),
		bThrottled(false),
		StaticMesh(NULL)
	{}

	~FDistanceFieldVolumeTexture();

	/** Called at load time on game thread */
	void Initialize(UStaticMesh* InStaticMesh);

	/** Called before unload on game thread */
	void Release();

	FIntVector GetAllocationMin() const
	{
		return AtlasAllocationMin;
	}

	FIntVector GetAllocationSizeInAtlas() const
	{
		return SizeInAtlas;
	}

	FIntVector GetAllocationSize() const;

	int32 GetAllocationVolume() const
	{
		return GetAllocationSize().X * GetAllocationSize().Y * GetAllocationSize().Z;
	}

	bool IsValidDistanceFieldVolume() const;

	bool Throttled() const
	{
		return bThrottled;
	}

	UStaticMesh* GetStaticMesh() const
	{
		return StaticMesh;
	}

private:
	const FDistanceFieldVolumeData& VolumeData;
	FIntVector AtlasAllocationMin;
	FIntVector SizeInAtlas;

	bool bReferencedByAtlas : 1;
	/** bThrottled prevents any objects using the texture from being uploaded to the scene buffer until upload of the texture to distance field atlas is complete */
	bool bThrottled         : 1;
	UStaticMesh* StaticMesh;

	friend class FDistanceFieldVolumeTextureAtlas;
};

/** Global volume texture atlas that collects all static mesh resource distance fields. */
class ENGINE_API FDistanceFieldVolumeTextureAtlas : public FRenderResource
{
public:
	FDistanceFieldVolumeTextureAtlas();

	void InitializeIfNeeded();

	virtual void ReleaseRHI() override
	{
		VolumeTextureUAVRHI.SafeRelease();
		VolumeTextureRHI.SafeRelease();
	}

	int32 GetSizeX() const { return VolumeTextureRHI->GetSizeX(); }
	int32 GetSizeY() const { return VolumeTextureRHI->GetSizeY(); }
	int32 GetSizeZ() const { return VolumeTextureRHI->GetSizeZ(); }

	FString GetSizeString() const;

	void ListMeshDistanceFields() const;

	/** Add an allocation to the atlas. */
	void AddAllocation(FDistanceFieldVolumeTexture* Texture);

	/** Remove an allocation from the atlas. This must be done prior to deleting the FDistanceFieldVolumeTexture object. */
	void RemoveAllocation(FDistanceFieldVolumeTexture* Texture);

	/** Reallocates the volume texture if necessary and uploads new allocations. */
	void UpdateAllocations(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type InFeatureLevel);

	int32 GetGeneration() const { return Generation; }

	EPixelFormat Format;
	FTexture3DRHIRef VolumeTextureRHI;
	FUnorderedAccessViewRHIRef VolumeTextureUAVRHI;

private:
	/** Manages the atlas layout. */
	FTextureLayout3d BlockAllocator;

	/** Allocations that are waiting to be added until the next update. */
	TArray<FDistanceFieldVolumeTexture*> PendingAllocations;

	/** Allocations that have already been added, stored in case we need to realloc. */
	TArray<FDistanceFieldVolumeTexture*> CurrentAllocations;

	/** Allocations that have failed, stored in case they could fit next time a mesh is evicted from atlas. */
	TArray<FDistanceFieldVolumeTexture*> FailedAllocations;
		
	/** Incremented when the atlas is reallocated, so dependencies know to update. */
	int32 Generation;

	bool bInitialized;

	/** Number of pixel used in atlas distance field */
	uint32 AllocatedPixels;
	
	/** Number of pixel that have failed to be allocated in atlas */
	uint32 FailedAllocatedPixels;

	/** Max position used in distance field */
	uint32 MaxUsedAtlasX;
	uint32 MaxUsedAtlasY;
	uint32 MaxUsedAtlasZ;
};

extern ENGINE_API TGlobalResource<FDistanceFieldVolumeTextureAtlas> GDistanceFieldVolumeTextureAtlas;

class ENGINE_API FHeightFieldTextureAtlas : public FRenderResource
{
public:
	void InitializeIfNeeded();

	void AddAllocation(UTexture2D* Texture);

	void RemoveAllocation(UTexture2D* Texture);

	void UpdateAllocations(FRHICommandListImmediate& RHICmdList, ERHIFeatureLevel::Type InFeatureLevel);

	uint32 GetAllocationHandle(UTexture2D* Texture) const;

	FVector4 GetAllocationScaleBias(uint32 Handle) const;

	FRHITexture2D* GetAtlasTexture() const
	{
		return AtlasTextureRHI;
	}

	uint32 GetSizeX() const
	{
		return AddrSpaceAllocator.DimInTexels;
	}

	uint32 GetSizeY() const
	{
		return AddrSpaceAllocator.DimInTexels;
	}

	uint32 GetGeneration() const
	{
		return Generation;
	}

private:
	uint32 CalculateDownSampleLevel(uint32 SizeX, uint32 SizeY) const;

	class FSubAllocator
	{
	public:
		void Init(uint32 InTileSize, uint32 InBorderSize, uint32 InDimInTiles);

		uint32 Alloc(uint32 SizeX, uint32 SizeY);

		void Free(uint32 Handle);

		FVector4 GetScaleBias(uint32 Handle) const;

		FIntPoint GetStartOffset(uint32 Handle) const;

	private:
		struct FSubAllocInfo
		{
			uint32 Level;
			uint32 QuadIdx;
			FVector4 UVScaleBias;
		};
		
		uint32 TileSize;
		uint32 BorderSize;
		uint32 TileSizeWithBorder;
		uint32 DimInTiles;
		uint32 DimInTilesShift;
		uint32 DimInTilesMask;
		uint32 DimInTexels;
		uint32 MaxNumTiles;

		float TexelSize;
		float TileScale;

		// 0: Free, 1: Allocated
		TBitArray<> MarkerQuadTree;
		TArray<uint32, TInlineAllocator<8>> LevelOffsets;

		TSparseArray<FSubAllocInfo> SubAllocInfos;

		friend class FHeightFieldTextureAtlas;
	};

	struct FAllocation
	{
		UTexture2D* SourceTexture;
		uint32 RefCount;
		uint32 Handle;

		FAllocation();

		FAllocation(UTexture2D* InTexture);

		bool operator==(const FAllocation& Other) const
		{
			return SourceTexture == Other.SourceTexture;
		}

		friend uint32 GetTypeHash(const FAllocation& Key)
		{
			return GetTypeHash(Key.SourceTexture);
		}
	};

	FSubAllocator AddrSpaceAllocator;

	TSet<FAllocation> PendingAllocations;
	TSet<FAllocation> FailedAllocations;
	TSet<FAllocation> CurrentAllocations;
	TArray<UTexture2D*> PendingStreamingTextures;

	FTexture2DRHIRef AtlasTextureRHI;
	FUnorderedAccessViewRHIRef AtlasUAVRHI;

	uint32 MaxDownSampleLevel;
	uint32 Generation;
};

extern ENGINE_API TGlobalResource<FHeightFieldTextureAtlas> GHeightFieldTextureAtlas;

/** Distance field data payload and output of the mesh build process. */
class ENGINE_API FDistanceFieldVolumeData : public FDeferredCleanupInterface
{
public:

	/** 
	 * FP16 Signed distance field volume stored in local space.  
	 * This has to be kept around after the inital upload to GPU memory to support reallocs of the distance field atlas, so it is compressed.
	 */
	TArray<uint8> CompressedDistanceFieldVolume;

	/** Dimensions of DistanceFieldVolume. */
	FIntVector Size;

	/** Local space bounding box of the distance field volume. */
	FBox LocalBoundingBox;

	FVector2D DistanceMinMax;

	/** Whether the mesh was closed and therefore a valid distance field was supported. */
	bool bMeshWasClosed;

	/** Whether the distance field was built assuming that every triangle is a frontface. */
	bool bBuiltAsIfTwoSided;

	/** Whether the mesh was a plane with very little extent in Z. */
	bool bMeshWasPlane;

	FDistanceFieldVolumeTexture VolumeTexture;

	FDistanceFieldVolumeData() :
		Size(FIntVector(0, 0, 0)),
		LocalBoundingBox(ForceInit),
		DistanceMinMax(FVector2D(0, 0)),
		bMeshWasClosed(true),
		bBuiltAsIfTwoSided(false),
		bMeshWasPlane(false),
		VolumeTexture(*this)
	{}

	void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) const
	{
		CumulativeResourceSize.AddDedicatedSystemMemoryBytes(sizeof(*this));
		CumulativeResourceSize.AddDedicatedSystemMemoryBytes(CompressedDistanceFieldVolume.GetAllocatedSize());
	}

	SIZE_T GetResourceSizeBytes() const
	{
		FResourceSizeEx ResSize;
		GetResourceSizeEx(ResSize);
		return ResSize.GetTotalMemoryBytes();
	}

#if WITH_EDITORONLY_DATA

	void CacheDerivedData(const FString& InDDCKey, UStaticMesh* Mesh, UStaticMesh* GenerateSource, float DistanceFieldResolutionScale, bool bGenerateDistanceFieldAsIfTwoSided);

#endif

	friend FArchive& operator<<(FArchive& Ar,FDistanceFieldVolumeData& Data)
	{
		// Note: this is derived data, no need for versioning (bump the DDC guid)
		Ar << Data.CompressedDistanceFieldVolume << Data.Size << Data.LocalBoundingBox << Data.DistanceMinMax << Data.bMeshWasClosed << Data.bBuiltAsIfTwoSided << Data.bMeshWasPlane;
		return Ar;
	}
};

/** A task to build a distance field for a single mesh */
class FAsyncDistanceFieldTask
{
public:
	FAsyncDistanceFieldTask();

	TArray<EBlendMode> MaterialBlendModes;
	UStaticMesh* StaticMesh;
	UStaticMesh* GenerateSource;
	float DistanceFieldResolutionScale;
	bool bGenerateDistanceFieldAsIfTwoSided;
	FString DDCKey;
	FDistanceFieldVolumeData* GeneratedVolumeData;
};

/** Class that manages asynchronous building of mesh distance fields. */
class FDistanceFieldAsyncQueue : public FGCObject
{
public:

	ENGINE_API FDistanceFieldAsyncQueue();

	ENGINE_API virtual ~FDistanceFieldAsyncQueue();

	/** Adds a new build task. (Thread-Safe) */
	ENGINE_API void AddTask(FAsyncDistanceFieldTask* Task);

	/** Blocks the main thread until the async build of the specified mesh is complete. */
	ENGINE_API void BlockUntilBuildComplete(UStaticMesh* StaticMesh, bool bWarnIfBlocked);

	/** Blocks the main thread until all async builds complete. */
	ENGINE_API void BlockUntilAllBuildsComplete();

	/** Called once per frame, fetches completed tasks and applies them to the scene. */
	ENGINE_API void ProcessAsyncTasks();

	/** Exposes UObject references used by the async build. */
	ENGINE_API virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	/** Returns name of class for reference tracking */
	ENGINE_API virtual FString GetReferencerName() const override;

	/** Blocks until it is safe to shut down (worker threads are idle). */
	ENGINE_API void Shutdown();

	int32 GetNumOutstandingTasks() const
	{
		return ReferencedTasks.Num();
	}

private:

	/** Builds a single task with the given threadpool.  Called from the worker thread. */
	void Build(FAsyncDistanceFieldTask* Task, class FQueuedThreadPool& ThreadPool);

	/** Thread that will build any tasks in TaskQueue and exit when there are no more. */
	class TUniquePtr<class FBuildDistanceFieldThreadRunnable> ThreadRunnable;

	/** Game-thread managed list of tasks in the async system. */
	TArray<FAsyncDistanceFieldTask*> ReferencedTasks;

	/** Tasks that have not yet started processing yet. */
	// consider changing this from FIFO to Unordered, which may be faster
	TLockFreePointerListLIFO<FAsyncDistanceFieldTask> TaskQueue;

	/** Tasks that have completed processing. */
	// consider changing this from FIFO to Unordered, which may be faster
	TLockFreePointerListLIFO<FAsyncDistanceFieldTask> CompletedTasks;

	class IMeshUtilities* MeshUtilities;

	FCriticalSection CriticalSection;

	friend class FBuildDistanceFieldThreadRunnable;
};

/** Global build queue. */
extern ENGINE_API FDistanceFieldAsyncQueue* GDistanceFieldAsyncQueue;

extern ENGINE_API FString BuildDistanceFieldDerivedDataKey(const FString& InMeshKey);
