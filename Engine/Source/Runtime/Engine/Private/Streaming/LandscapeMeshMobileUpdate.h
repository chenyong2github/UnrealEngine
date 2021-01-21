// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
LandscapeMeshMobileUpdate.h: Helpers to stream in and out mobile landscape vertex buffers.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RenderAssetUpdate.h"
#include "Async/AsyncFileHandle.h"
#include "Serialization/BulkData.h"

class UStreamableRenderAsset;
class ULandscapeLODStreamingProxy;
struct FLandscapeMobileRenderData;

struct FLandscapeMeshMobileUpdateContext
{
	typedef int32 EThreadType;

	FLandscapeMeshMobileUpdateContext(const ULandscapeLODStreamingProxy* InLandscapeProxy, EThreadType InCurrentThread);

	FLandscapeMeshMobileUpdateContext(const UStreamableRenderAsset* InLandscapeProxy, EThreadType InCurrentThread);

	EThreadType GetCurrentThread() const
	{
		return CurrentThread;
	}

	const ULandscapeLODStreamingProxy* LandscapeProxy;
	FLandscapeMobileRenderData* RenderData;
	EThreadType CurrentThread;
};

extern template class TRenderAssetUpdate<FLandscapeMeshMobileUpdateContext>;

class FLandscapeMeshMobileUpdate : public TRenderAssetUpdate<FLandscapeMeshMobileUpdateContext>
{
public:
	FLandscapeMeshMobileUpdate(ULandscapeLODStreamingProxy* InLandscapeProxy);

	virtual void Abort()
	{
		TRenderAssetUpdate<FLandscapeMeshMobileUpdateContext>::Abort();
	}

protected:

	virtual ~FLandscapeMeshMobileUpdate() {}
};

class FLandscapeMeshMobileStreamIn : public FLandscapeMeshMobileUpdate
{
public:
	FLandscapeMeshMobileStreamIn(ULandscapeLODStreamingProxy* InLandscapeProxy);

	virtual ~FLandscapeMeshMobileStreamIn();

protected:
	void ExpandResources(const FContext& Context);
	void DiscardNewLODs(const FContext& Context);
	void DoFinishUpdate(const FContext& Context);
	void DoCancel(const FContext& Context);

	FBufferRHIRef IntermediateVertexBuffer;
	void* StagingLODDataArray[MAX_MESH_LOD_COUNT];
	int64 StagingLODDataSizes[MAX_MESH_LOD_COUNT];
};

class FLandscapeMeshMobileStreamOut : public FLandscapeMeshMobileUpdate
{
public:
	ENGINE_API FLandscapeMeshMobileStreamOut(ULandscapeLODStreamingProxy* InLandscapeProxy);

	virtual ~FLandscapeMeshMobileStreamOut() {}

private:
	void ShrinkResources(const FContext& Context);
};

class FLandscapeMeshMobileStreamIn_IO : public FLandscapeMeshMobileStreamIn
{
public:
	FLandscapeMeshMobileStreamIn_IO(ULandscapeLODStreamingProxy* InLandscapeProxy, bool bHighPrio);

	virtual ~FLandscapeMeshMobileStreamIn_IO() {}

	virtual void Abort() override;

protected:
	class FCancelIORequestsTask : public FNonAbandonableTask
	{
	public:
		FCancelIORequestsTask(FLandscapeMeshMobileStreamIn_IO* InPendingUpdate)
			: PendingUpdate(InPendingUpdate)
		{}

		void DoWork();

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FCancelIORequestsTask_LandscapeMeshMobile, STATGROUP_ThreadPoolAsyncTasks);
		}

	private:
		TRefCountPtr<FLandscapeMeshMobileStreamIn_IO> PendingUpdate;
	};

	typedef FAutoDeleteAsyncTask<FCancelIORequestsTask> FAsyncCancelIORequestsTask;
	friend class FCancelIORequestsTask;

	bool HasPendingIORequests() const;

	void GetIOPackagePath(const FContext& Context, FPackagePath& OutPackagePath, EPackageSegment& OutPackageSegment);
	void SetAsyncFileCallback(const FContext& Context);
	void SetIORequest(const FContext& Context, const FPackagePath& PackagePath, EPackageSegment PackageSegment);
	void GetIORequestResults(const FContext& Context);
	void ClearIORequest(const FContext& Context);
	void CancelIORequest();

	class IBulkDataIORequest* IORequests[MAX_MESH_LOD_COUNT];
	FBulkDataIORequestCallBack AsyncFileCallback;
	bool bHighPrioIORequest;
};

class FLandscapeMeshMobileStreamIn_IO_AsyncReallocate : public FLandscapeMeshMobileStreamIn_IO
{
public:
	ENGINE_API FLandscapeMeshMobileStreamIn_IO_AsyncReallocate(ULandscapeLODStreamingProxy* InLandscapeProxy, bool bHighPrio);

	virtual ~FLandscapeMeshMobileStreamIn_IO_AsyncReallocate() {}

protected:
	void DoInitiateIO(const FContext& Context);

	void DoGetIORequestResults(const FContext& Context);

	void DoExpandResources(const FContext& Context);

	void DoCancelIO(const FContext& Context);
};

#if WITH_EDITOR
class FLandscapeMeshMobileStreamIn_GPUDataOnly : public FLandscapeMeshMobileStreamIn
{
public:
	ENGINE_API FLandscapeMeshMobileStreamIn_GPUDataOnly(ULandscapeLODStreamingProxy* InLandscapeProxy);

	virtual ~FLandscapeMeshMobileStreamIn_GPUDataOnly() {}

protected:
	void GetStagingData(const FContext& Context);

	void DoGetStagingData(const FContext& Context);

	void DoExpandResources(const FContext& Context);
};
#endif
