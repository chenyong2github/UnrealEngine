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
			const int32 EndIndex = (TaskIndex == NumTasks - 1) ? NumIterations : (StartIndex + IterationsPerTask);

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

	void AsyncPointFilterProcessing(FPCGContext* Context, int32 NumIterations, TArray<FPCGPoint>& InFilterPoints, TArray<FPCGPoint>& OutFilterPoints, const TFunction<bool(int32, FPCGPoint&, FPCGPoint&)>& PointFunc)
	{
		const int32 MinIterationsPerTask = 256;
		AsyncPointFilterProcessing(Context ? Context->NumAvailableTasks : 1, MinIterationsPerTask, NumIterations, InFilterPoints, OutFilterPoints, PointFunc);
	}

	void AsyncPointFilterProcessing(int32 NumAvailableTasks, int32 MinIterationsPerTask, int32 NumIterations, TArray<FPCGPoint>& InFilterPoints, TArray<FPCGPoint>& OutFilterPoints, const TFunction<bool(int32, FPCGPoint&, FPCGPoint&)>& PointFunc)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(IPCGElement::AsyncPointFilterProcessing);
		check(NumAvailableTasks > 0 && MinIterationsPerTask > 0 && NumIterations >= 0);
		// Get number of available threads from the context
		const int32 NumTasks = FMath::Min(NumAvailableTasks, FMath::Max(1, NumIterations / MinIterationsPerTask));
		const int32 IterationsPerTask = NumIterations / NumTasks;

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(IPCGElement::AsyncPointProcessing::AllocatingArray);
			// Pre-reserve the out points array
			InFilterPoints.SetNum(NumIterations);
			OutFilterPoints.SetNum(NumIterations);
		}

		// Setup [current, last, nb points] data per dispatch
		// Execute
		TArray<TFuture<TPair<int32, int32>>> AsyncTasks;
		AsyncTasks.Reserve(NumTasks);

		// Launch the async tasks
		for (int32 TaskIndex = 0; TaskIndex < NumTasks; ++TaskIndex)
		{
			const int32 StartIndex = TaskIndex * IterationsPerTask;
			const int32 EndIndex = (TaskIndex == NumTasks - 1) ? NumIterations : (StartIndex + IterationsPerTask);

			AsyncTasks.Emplace(Async(EAsyncExecution::ThreadPool, [&PointFunc, StartIndex, EndIndex, &InFilterPoints, &OutFilterPoints]()
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(IPCGElement::AsyncPointProcessing::InnerLoop);
					int32 NumPointsInWritten = 0;
					int32 NumPointsOutWritten = 0;

					for (int32 Index = StartIndex; Index < EndIndex; ++Index)
					{
						if (PointFunc(Index, InFilterPoints[StartIndex + NumPointsInWritten], OutFilterPoints[StartIndex + NumPointsOutWritten]))
						{
							++NumPointsInWritten;
						}
						else
						{
							++NumPointsOutWritten;
						}
					}

					return TPair<int32, int32>(NumPointsInWritten, NumPointsOutWritten);
				}));
		}

		// Wait/Gather results & collapse points
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(IPCGElement::AsyncPointFilterProcessing::WaitAndCollapseArray);
			int InFilterRangeIndex = 0;
			int OutFilterRangeIndex = 0;

			for (int32 AsyncIndex = 0; AsyncIndex < AsyncTasks.Num(); ++AsyncIndex)
			{
				const int32 StartPointsIndex = AsyncIndex * IterationsPerTask;

				TFuture<TPair<int32, int32>>& AsyncTask = AsyncTasks[AsyncIndex];
				AsyncTask.Wait();
				TPair<int32, int32> NumberOfPointsAdded = AsyncTask.Get();

				// Move in-filter points
				{
					int NumInFilterPoints = NumberOfPointsAdded.Key;

					if (StartPointsIndex != InFilterRangeIndex)
					{
						for (int32 MoveIndex = 0; MoveIndex < NumInFilterPoints; ++MoveIndex)
						{
							InFilterPoints[InFilterRangeIndex + MoveIndex] = MoveTemp(InFilterPoints[StartPointsIndex + MoveIndex]);
						}
					}

					InFilterRangeIndex += NumInFilterPoints;
				}

				// Move out-filter points
				{
					int NumOutFilterPoints = NumberOfPointsAdded.Value;

					if (StartPointsIndex != OutFilterRangeIndex)
					{
						for (int32 MoveIndex = 0; MoveIndex < NumOutFilterPoints; ++MoveIndex)
						{
							OutFilterPoints[OutFilterRangeIndex + MoveIndex] = MoveTemp(OutFilterPoints[StartPointsIndex + MoveIndex]);
						}
					}

					OutFilterRangeIndex += NumOutFilterPoints;
				}
			}

			InFilterPoints.SetNum(InFilterRangeIndex);
			OutFilterPoints.SetNum(OutFilterRangeIndex);
		}
	}

	void AsyncMultiPointProcessing(FPCGContext* Context, int32 NumIterations, TArray<FPCGPoint>& OutPoints, const TFunction<TArray<FPCGPoint>(int32)>& PointFunc)
	{
		const int32 MinIterationsPerTask = 256;
		AsyncMultiPointProcessing(Context ? Context->NumAvailableTasks : 1, MinIterationsPerTask, NumIterations, OutPoints, PointFunc);
	}

	void AsyncMultiPointProcessing(int32 NumAvailableTasks, int32 MinIterationsPerTask, int32 NumIterations, TArray<FPCGPoint>& OutPoints, const TFunction<TArray<FPCGPoint>(int32)>& PointFunc)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(IPCGElement::AsyncMultiPointProcessing);
		check(NumAvailableTasks > 0 && MinIterationsPerTask > 0 && NumIterations >= 0);
		// Get number of available threads from the context
		const int32 NumTasks = FMath::Min(NumAvailableTasks, FMath::Max(1, NumIterations / MinIterationsPerTask));
		const int32 IterationsPerTask = NumIterations / NumTasks;

		TArray<TFuture<TArray<FPCGPoint>>> AsyncTasks;
		AsyncTasks.Reserve(NumTasks);

		// Launch the async tasks
		for (int32 TaskIndex = 0; TaskIndex < NumTasks; ++TaskIndex)
		{
			const int32 StartIndex = TaskIndex * IterationsPerTask;
			const int32 EndIndex = (TaskIndex == NumTasks - 1) ? NumIterations : (StartIndex + IterationsPerTask);

			AsyncTasks.Emplace(Async(EAsyncExecution::ThreadPool, [&PointFunc, StartIndex, EndIndex]()
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(IPCGElement::AsyncMultiPointProcessing::InnerLoop);
				TArray<FPCGPoint> OutPoints;

				for (int32 Index = StartIndex; Index < EndIndex; ++Index)
				{
					OutPoints.Append(PointFunc(Index));
				}

				return OutPoints;
			}));
		}

		// Wait/Gather results & collapse points
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(IPCGElement::AsyncMultiPointProcessing::WaitAndCollapseArray);
			for (TFuture<TArray<FPCGPoint>>& AsyncTask : AsyncTasks)
			{
				AsyncTask.Wait();
				OutPoints.Append(AsyncTask.Get());
			}
		}
	}
}