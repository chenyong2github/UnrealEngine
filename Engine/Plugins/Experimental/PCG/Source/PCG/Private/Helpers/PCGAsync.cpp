// Copyright Epic Games, Inc. All Rights Reserved.

#include "Helpers/PCGAsync.h"
#include "PCGPoint.h"
#include "PCGContext.h"

#include "Async/Async.h"

namespace FPCGAsync
{
	void AsyncPointProcessing(FPCGContext* Context, int32 NumIterations, TArray<FPCGPoint>& OutPoints, const TFunction<bool(int32, FPCGPoint&)>& PointFunc)
	{
		const int32 MinIterationsPerTask = 256;
		AsyncPointProcessing(Context ? Context->NumAvailableTasks : 1, MinIterationsPerTask, NumIterations, OutPoints, PointFunc);
	}

	void AsyncPointProcessing(int32 NumAvailableTasks, int32 MinIterationsPerTask, int32 NumIterations, TArray<FPCGPoint>& OutPoints, const TFunction<bool(int32, FPCGPoint&)>& PointFunc)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(IPCGElement::AsyncPointProcessing);
		check(NumAvailableTasks > 0 && MinIterationsPerTask > 0 && NumIterations >= 0);
		// Get number of available threads from the context
		const int32 NumTasks = FMath::Min(NumAvailableTasks, FMath::Max(1, NumIterations / MinIterationsPerTask));
		const int32 IterationsPerTask = NumIterations / NumTasks;

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(IPCGElement::AsyncPointProcessing::AllocatingArray);
			// Pre-reserve the out points array
			OutPoints.SetNum(NumIterations);
		}

		// Setup [current, last, nb points] data per dispatch
		// Execute
		TArray<TFuture<int32>> AsyncTasks;
		AsyncTasks.Reserve(NumTasks);

		// Launch the async tasks
		for (int32 TaskIndex = 0; TaskIndex < NumTasks; ++TaskIndex)
		{
			const int32 StartIndex = TaskIndex * IterationsPerTask;
			const int32 EndIndex = (TaskIndex == NumTasks - 1 ? NumIterations : StartIndex + IterationsPerTask);

			AsyncTasks.Emplace(Async(EAsyncExecution::ThreadPool, [&PointFunc, StartIndex, EndIndex, &OutPoints]()
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(IPCGElement::AsyncPointProcessing::InnerLoop);
				int32 NumPointsWritten = 0;

				for (int32 Index = StartIndex; Index < EndIndex; ++Index)
				{
					if (PointFunc(Index, OutPoints[StartIndex + NumPointsWritten]))
					{
						++NumPointsWritten;
					}
				}

				return NumPointsWritten;
			}));
		}

		// Wait/Gather results & collapse points
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(IPCGElement::AsyncPointProcessing::WaitAndCollapseArray);
			int RangeIndex = 0;
			for (int32 AsyncIndex = 0; AsyncIndex < AsyncTasks.Num(); ++AsyncIndex)
			{
				const int32 StartPointsIndex = AsyncIndex * IterationsPerTask;

				TFuture<int32>& AsyncTask = AsyncTasks[AsyncIndex];
				AsyncTask.Wait();
				int32 NumberOfPointsAdded = AsyncTask.Get();

				// Move points from [StartsPointIndex, StartsPointIndex + NumberPointsAdded] to [RangeIndex, RangeIndex + NumberPointsAdded]
				if (StartPointsIndex != RangeIndex)
				{
					for (int32 MoveIndex = 0; MoveIndex < NumberOfPointsAdded; ++MoveIndex)
					{
						OutPoints[RangeIndex + MoveIndex] = MoveTemp(OutPoints[StartPointsIndex + MoveIndex]);
					}
				}

				RangeIndex += NumberOfPointsAdded;
			}

			OutPoints.SetNum(RangeIndex);
		}
	}
}