// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryCollection/GeometryCollection.h"
#include "ModelingTaskTypes.h"
#include "ModelingOperators.h"
#include "BackgroundModelingComputeSource.h"
#include "Misc/ScopedSlowTask.h"

namespace UE
{
namespace Fracture
{

// Base class for background operators that update geometry collections (e.g. to fracture in a background thread)
class FGeometryCollectionOperator : public UE::Geometry::TGenericDataOperator<FGeometryCollection>
{
public:
	int ResultGeometryIndex = -1;
	TUniquePtr<FGeometryCollection> CollectionCopy;

	FGeometryCollectionOperator(const FGeometryCollection& SourceCollection)
	{
		CollectionCopy = MakeUnique<FGeometryCollection>();
		CollectionCopy->CopyMatchingAttributesFrom(SourceCollection, nullptr);
	}

	virtual ~FGeometryCollectionOperator() = default;

	virtual int GetResultGeometryIndex()
	{
		return ResultGeometryIndex;
	}

	// Post-process the geometry collection on success -- by default, clears proximity data
	virtual void OnSuccess(FGeometryCollection& Collection)
	{
		// Invalidate proximity
		if (Collection.HasAttribute("Proximity", FGeometryCollection::GeometryGroup))
		{
			Collection.RemoveAttribute("Proximity", FGeometryCollection::GeometryGroup);
		}
	}
};

// Run a blocking geometry collection op, but with a responsive cancel option
template<class GeometryCollectionOpType>
int RunCancellableGeometryCollectionOp(FGeometryCollection& ToUpdate, TUniquePtr<GeometryCollectionOpType>&& NewOp, FText DefaultMessage, float DialogDelay = .5)
{
	FScopedSlowTask SlowTask(1, DefaultMessage);
	SlowTask.MakeDialogDelayed(DialogDelay, true);
	using FGeometryCollectionTask = UE::Geometry::TModelingOpTask<GeometryCollectionOpType>;
	using FExecuter = UE::Geometry::FAsyncTaskExecuterWithAbort<FGeometryCollectionTask>;
	TUniquePtr<FExecuter> BackgroundTask = MakeUnique<FExecuter>(MoveTemp(NewOp));
	BackgroundTask->StartBackgroundTask();

	bool bSuccess = false;
	while (true)
	{
		if (BackgroundTask->IsDone())
		{
			bSuccess = !BackgroundTask->GetTask().IsAborted();
			break;
		}
		if (SlowTask.ShouldCancel())
		{
			// Release ownership to the TDeleterTask that is spawned by CancelAndDelete()
			BackgroundTask.Release()->CancelAndDelete();
			break;
		}
		FPlatformProcess::Sleep(.2); // SlowTask::ShouldCancel will throttle any updates faster than .2 seconds
		SlowTask.TickProgress(); // Note: EnterProgressFrame will also call this, but we aren't getting progress updates here yet
		// TODO: Connect up a way to get progress updates from the task back to SlowTask
	}

	if (bSuccess)
	{
		check(BackgroundTask != nullptr && BackgroundTask->IsDone());
		TUniquePtr<GeometryCollectionOpType> Op = BackgroundTask->GetTask().ExtractOperator();
		TUniquePtr<FGeometryCollection> Result = Op->ExtractResult();
		if (!Result.IsValid())
		{
			return -1;
		}

		ToUpdate.CopyMatchingAttributesFrom(*Result, nullptr);
		Op->OnSuccess(ToUpdate);
		return Op->GetResultGeometryIndex();
	}

	return -1;
}

} // end namespace UE::Fracture
} // end namespace UE