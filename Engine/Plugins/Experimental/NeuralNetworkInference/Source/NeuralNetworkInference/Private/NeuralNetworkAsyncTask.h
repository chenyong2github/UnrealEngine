// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Async/AsyncWork.h"

// RHI includes must happen before onnxruntime_cxx_api.h (both files include Windows.h)
#include "HAL/CriticalSection.h"
#include "RHI.h"
#include "DynamicRHI.h"

#include "ThirdPartyWarningDisabler.h"
NNI_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT
#ifdef WITH_UE_AND_ORT_SUPPORT
#include "core/session/onnxruntime_cxx_api.h"
#endif //WITH_UE_AND_ORT_SUPPORT
NNI_THIRD_PARTY_INCLUDES_END

DECLARE_DELEGATE(FOnAsyncRunCompleted);

#ifdef WITH_UE_AND_ORT_SUPPORT

class FNeuralNetworkAsyncTask : public FNonAbandonableTask
{
	friend class FAsyncTask<FNeuralNetworkAsyncTask>;

public:
	FNeuralNetworkAsyncTask(FOnAsyncRunCompleted& InOutOnAsyncRunCompletedDelegate, std::atomic<bool>& bInOutIsBackgroundThreadRunning, FCriticalSection& InOutResoucesCriticalSection,
		Ort::Session& InOutSession, TArray<Ort::Value>& OutOutputOrtTensors, const TArray<Ort::Value>& InInputOrtTensors, const TArray<const char*>& InInputTensorNames, const TArray<const char*>& InOutputTensorNames)
		// Async-related
		: OnAsyncRunCompletedDelegate(InOutOnAsyncRunCompletedDelegate), bIsBackgroundThreadRunning(bInOutIsBackgroundThreadRunning), ResoucesCriticalSection(InOutResoucesCriticalSection)
		// ORT-related
		, Session(InOutSession), InputOrtTensors(InInputOrtTensors), InputTensorNames(InInputTensorNames), OutputOrtTensors(OutOutputOrtTensors), OutputTensorNames(InOutputTensorNames)
	{ }

	void SetSynchronousMode(const ENeuralNetworkSynchronousMode InSyncMode)
	{
		const FScopeLock ResourcesLock(&ResoucesCriticalSection);
		SyncMode = InSyncMode;
	}

protected:
	void DoWork()
	{
		TUniquePtr<FScopeLock> ResourcesLock;
		if (SyncMode == ENeuralNetworkSynchronousMode::Asynchronous)
		{
			ResourcesLock = MakeUnique<FScopeLock>(&ResoucesCriticalSection);
		}
		
		Session.Run(Ort::RunOptions{ nullptr },
			InputTensorNames.GetData(), InputOrtTensors.GetData(), InputTensorNames.Num(),
			OutputTensorNames.GetData(), OutputOrtTensors.GetData(), OutputTensorNames.Num());
		
		if (SyncMode == ENeuralNetworkSynchronousMode::Asynchronous)
		{
			OnAsyncRunCompletedDelegate.ExecuteIfBound();
			bIsBackgroundThreadRunning = false;
		}
	}

	// This next section of code needs to be here. Not important as to why.
	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FNeuralNetworkAsyncTask, STATGROUP_ThreadPoolAsyncTasks);
	}

private:
	/** Variables that could change on each inference run */
	ENeuralNetworkSynchronousMode SyncMode;

	/** Async variables that could only change during construction */
	FOnAsyncRunCompleted& OnAsyncRunCompletedDelegate;
	std::atomic<bool>& bIsBackgroundThreadRunning;
	FCriticalSection& ResoucesCriticalSection;

	/** ORT variables that could only change during construction */
	Ort::Session& Session;
	const TArray<Ort::Value>& InputOrtTensors;
	const TArray<const char*>& InputTensorNames;
	TArray<Ort::Value>& OutputOrtTensors;
	const TArray<const char*>& OutputTensorNames;
};

#endif //WITH_UE_AND_ORT_SUPPORT
