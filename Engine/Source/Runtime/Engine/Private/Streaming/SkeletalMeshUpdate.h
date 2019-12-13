// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
SkeletalMeshUpdate.h: Helpers to stream in and out skeletal mesh LODs.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/SkeletalMesh.h"
#include "Serialization/BulkData.h"

/**
* A context used to update or proceed with the next update step.
* The mesh and render data references could be stored in the update object
* but are currently kept outside to avoid lifetime management within the object.
*/
struct FSkelMeshUpdateContext
{
	typedef int32 EThreadType;

	FSkelMeshUpdateContext(USkeletalMesh* InMesh, EThreadType InCurrentThread);

	FSkelMeshUpdateContext(UStreamableRenderAsset* InMesh, EThreadType InCurrentThread);

	UStreamableRenderAsset* GetRenderAsset() const
	{
		return Mesh;
	}

	EThreadType GetCurrentThread() const
	{
		return CurrentThread;
	}

	/** The mesh to update, this must be the same one as the one used when creating the FSkeletalMeshUpdate object. */
	USkeletalMesh* Mesh;
	/** The current render data of this mesh. */
	FSkeletalMeshRenderData* RenderData;
	/** The thread on which the context was created. */
	EThreadType CurrentThread;
};

extern template class TRenderAssetUpdate<FSkelMeshUpdateContext>;

/**
* This class provides a framework for loading and unloading the LODs of skeletal meshes.
* Each thread essentially calls Tick() until the job is done.
* The object can be safely deleted when IsCompleted() returns true.
*/
class FSkeletalMeshUpdate : public TRenderAssetUpdate<FSkelMeshUpdateContext>
{
public:
	FSkeletalMeshUpdate(USkeletalMesh* InMesh, int32 InRequestedMips);

	virtual ~FSkeletalMeshUpdate() {}

	virtual void Abort()
	{
		TRenderAssetUpdate<FSkelMeshUpdateContext>::Abort();
	}

#if WITH_EDITOR
	/** Returns whether DDC of this mesh needs to be regenerated.  */
	virtual bool DDCIsInvalid() const { return false; }
#endif

protected:
	/** Cached index of current first LOD that will be replaced by PendingFirstMip */
	int32 CurrentFirstLODIdx;
};

class FSkeletalMeshStreamIn : public FSkeletalMeshUpdate
{
public:
	FSkeletalMeshStreamIn(USkeletalMesh* InMesh, int32 InRequestedMips);

	virtual ~FSkeletalMeshStreamIn();

protected:
	/** Correspond to the buffers in FSkeletalMeshLODRenderData */
	struct FIntermediateBuffers
	{
		FVertexBufferRHIRef TangentsVertexBuffer;
		FVertexBufferRHIRef TexCoordVertexBuffer;
		FVertexBufferRHIRef PositionVertexBuffer;
		FVertexBufferRHIRef ColorVertexBuffer;
		FVertexBufferRHIRef SkinWeightVertexBuffer;
		FVertexBufferRHIRef ClothVertexBuffer;
		FIndexBufferRHIRef IndexBuffer;
		FIndexBufferRHIRef AdjacencyIndexBuffer;
		TArray<TPair<FName, FVertexBufferRHIRef>> AltSkinWeightVertexBuffers;

		void CreateFromCPUData_RenderThread(USkeletalMesh* Mesh, FSkeletalMeshLODRenderData& LODResource);
		void CreateFromCPUData_Async(USkeletalMesh* Mesh, FSkeletalMeshLODRenderData& LODResource);

		void SafeRelease();

		/** Transfer ownership of buffers to a LOD resource */
		template <uint32 MaxNumUpdates>
		void TransferBuffers(FSkeletalMeshLODRenderData& LODResource, TRHIResourceUpdateBatcher<MaxNumUpdates>& Batcher);

		void CheckIsNull() const;
	};

	/** Create buffers with new LOD data on render or pooled thread */
	void CreateBuffers_RenderThread(const FContext& Context);
	void CreateBuffers_Async(const FContext& Context);

	/** Discard newly streamed-in CPU data */
	void DiscardNewLODs(const FContext& Context);

	/** Apply the new buffers (if not cancelled) and finish the update process. When cancelled, the intermediate buffers simply gets discarded. */
	void DoFinishUpdate(const FContext& Context);

	/** Discard streamed-in CPU data and intermediate RHI buffers */
	void DoCancel(const FContext& Context);

	/** The intermediate buffers created in the update process. */
	FIntermediateBuffers IntermediateBuffersArray[MAX_MESH_LOD_COUNT];

private:
	template <bool bRenderThread>
	void CreateBuffers_Internal(const FContext& Context);
};

class FSkeletalMeshStreamOut : public FSkeletalMeshUpdate
{
public:
	FSkeletalMeshStreamOut(USkeletalMesh* InMesh, int32 InRequestedMips);

	virtual ~FSkeletalMeshStreamOut() {}

private:
	void DoConditionalMarkComponentsDirty(const FContext& Context);

	/** Release RHI buffers and update SRVs */
	void DoReleaseBuffers(const FContext& Context);

	/** */
	void DoCancel(const FContext& Context);

	uint32 StartFrameNumber;
};

class FSkeletalMeshStreamIn_IO : public FSkeletalMeshStreamIn
{
public:
	FSkeletalMeshStreamIn_IO(USkeletalMesh* InMesh, int32 InRequestedMips, bool bHighPrio);

	virtual ~FSkeletalMeshStreamIn_IO() {}

	virtual void Abort() override;

protected:
	class FCancelIORequestsTask : public FNonAbandonableTask
	{
	public:
		FCancelIORequestsTask(FSkeletalMeshStreamIn_IO* InPendingUpdate)
			: PendingUpdate(InPendingUpdate)
		{}

		void DoWork();

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FCancelIORequestsTask_SkeletalMesh, STATGROUP_ThreadPoolAsyncTasks);
		}

	private:
		FSkeletalMeshStreamIn_IO* PendingUpdate;
	};

	typedef FAutoDeleteAsyncTask<FCancelIORequestsTask> FAsyncCancelIORequestsTask;
	friend class FCancelIORequestsTask;

	/** Figure out the full name of the .bulk file */
	FString GetIOFilename(const FContext& Context);

	/** Set a callback called when IORequest is completed or cancelled */
	void SetAsyncFileCallback(const FContext& Context);

	/** Create a new async IO request to read in LOD data */
	void SetIORequest(const FContext& Context, const FString& IOFilename);

	/** Release IORequest and IOFileHandle. IORequest will be cancelled if still inflight */
	void ClearIORequest(const FContext& Context);

	/** Serialize data of new LODs to corresponding FStaticMeshLODResources */
	void SerializeLODData(const FContext& Context);

	/** Called by FAsyncCancelIORequestsTask to cancel inflight IO request if any */
	void CancelIORequest();

	class IBulkDataIORequest* IORequest;
	FBulkDataIORequestCallBack AsyncFileCallback;
	bool bHighPrioIORequest;
};

template <bool bRenderThread>
class TSkeletalMeshStreamIn_IO : public FSkeletalMeshStreamIn_IO
{
public:
	TSkeletalMeshStreamIn_IO(USkeletalMesh* InMesh, int32 InRequestedMips, bool bHighPrio);

	virtual ~TSkeletalMeshStreamIn_IO() {}

protected:
	void DoInitiateIO(const FContext& Context);

	void DoSerializeLODData(const FContext& Context);

	void DoCreateBuffers(const FContext& Context);

	void DoCancelIO(const FContext& Context);
};

typedef TSkeletalMeshStreamIn_IO<true> FSkeletalMeshStreamIn_IO_RenderThread;
typedef TSkeletalMeshStreamIn_IO<false> FSkeletalMeshStreamIn_IO_Async;

#if WITH_EDITOR
class FSkeletalMeshStreamIn_DDC : public FSkeletalMeshStreamIn
{
public:
	FSkeletalMeshStreamIn_DDC(USkeletalMesh* InMesh, int32 InRequestedMips);

	virtual ~FSkeletalMeshStreamIn_DDC() {}

	virtual bool DDCIsInvalid() const override { return bDerivedDataInvalid; }

protected:
	void LoadNewLODsFromDDC(const FContext& Context);

	bool bDerivedDataInvalid;
};

template <bool bRenderThread>
class TSkeletalMeshStreamIn_DDC : public FSkeletalMeshStreamIn_DDC
{
public:
	TSkeletalMeshStreamIn_DDC(USkeletalMesh* InMesh, int32 InRequestedMips);

	virtual ~TSkeletalMeshStreamIn_DDC() {}

private:
	/** Load new LOD buffers from DDC and queue a task to create RHI buffers on RT */
	void DoLoadNewLODsFromDDC(const FContext& Context);

	/** Create RHI buffers for newly streamed-in LODs and queue a task to rename references on RT */
	void DoCreateBuffers(const FContext& Context);
};

typedef TSkeletalMeshStreamIn_DDC<true> FSkeletalMeshStreamIn_DDC_RenderThread;
typedef TSkeletalMeshStreamIn_DDC<false> FSkeletalMeshStreamIn_DDC_Async;
#endif