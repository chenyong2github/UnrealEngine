// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Async/AsyncWork.h"
#include "NeuralNetwork.h"

#include "ThirdPartyWarningDisabler.h"
NNI_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT
#ifdef WITH_UE_AND_ORT_SUPPORT
#include "core/session/onnxruntime_cxx_api.h"
#endif //WITH_UE_AND_ORT_SUPPORT
NNI_THIRD_PARTY_INCLUDES_END

DECLARE_DELEGATE(FOnAsyncRunCompleted);

struct FNeuralNetworkAsyncSyncData
{
	FNeuralNetworkAsyncSyncData() {}

	FNeuralNetworkAsyncSyncData(FOnAsyncRunCompleted& InOutOnAsyncRunCompletedDelegate, std::atomic<bool>& bInIsBackgroundThreadRunning, FCriticalSection& InResoucesCriticalSection) 
		: OnAsyncRunCompletedDelegate(&InOutOnAsyncRunCompletedDelegate), bIsBackgroundThreadRunning(&bInIsBackgroundThreadRunning), ResoucesCriticalSection(&InResoucesCriticalSection)
	{ }

	FOnAsyncRunCompleted* OnAsyncRunCompletedDelegate;
	std::atomic<bool>* bIsBackgroundThreadRunning;
	FCriticalSection* ResoucesCriticalSection;
};

#ifdef WITH_UE_AND_ORT_SUPPORT

struct FNeuralNetworkAsyncOrtVariables
{
	FNeuralNetworkAsyncOrtVariables() {}

	FNeuralNetworkAsyncOrtVariables(Ort::Session* InOutSession, const TSharedPtr<Ort::RunOptions>& InRunOptions, const TArray<Ort::Value>& InInputOrtTensors, const TArray<const char*>& InInputTensorNames,	TArray<Ort::Value>& InOutputOrtTensors,	TArray<const char*>& InOutputTensorNames)
		: Session(InOutSession), RunOptions(InRunOptions), InputOrtTensors(&InInputOrtTensors), InputTensorNames(&InInputTensorNames), OutputOrtTensors(&InOutputOrtTensors), OutputTensorNames(&InOutputTensorNames)
	{ }

	Ort::Session* Session;
	TSharedPtr<Ort::RunOptions> RunOptions;
	const TArray<Ort::Value>* InputOrtTensors; 
	const TArray<const char*>* InputTensorNames;
	TArray<Ort::Value>* OutputOrtTensors; 
	TArray<const char*>* OutputTensorNames;
};


class FNeuralNetworkAsyncTask : public FNonAbandonableTask
{
	friend class FAsyncTask<FNeuralNetworkAsyncTask>;

public:
	FNeuralNetworkAsyncTask(const FNeuralNetworkAsyncSyncData& InSyncData, const FNeuralNetworkAsyncOrtVariables& InOrtData) 
		: SyncData(InSyncData), OrtData(InOrtData)
	{ }

	void SetSynchronousMode(const ENeuralNetworkSynchronousMode InSyncMode)
	{
		const FScopeLock ResourcesLock(SyncData.ResoucesCriticalSection);
		SyncMode = InSyncMode;
	}
protected:
	void DoWork()
	{
		TUniquePtr<FScopeLock> ResourcesLock;
		if (SyncMode == ENeuralNetworkSynchronousMode::Asynchronous)
		{
			ResourcesLock = MakeUnique<FScopeLock>(SyncData.ResoucesCriticalSection);
		}
		
		OrtData.Session->Run(
			*(OrtData.RunOptions.Get()),
			OrtData.InputTensorNames->GetData(),
			OrtData.InputOrtTensors->GetData(),
			OrtData.InputTensorNames->Num(),
			OrtData.OutputTensorNames->GetData(),
			OrtData.OutputOrtTensors->GetData(),
			OrtData.OutputTensorNames->Num()
		);
		
		if (SyncMode == ENeuralNetworkSynchronousMode::Asynchronous)
		{
			SyncData.OnAsyncRunCompletedDelegate->ExecuteIfBound();
			SyncData.bIsBackgroundThreadRunning->store(false);
		}
	}
	// This next section of code needs to be here.  Not important as to why.

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FNeuralNetworkAsyncTask, STATGROUP_ThreadPoolAsyncTasks);
	}

private:
	ENeuralNetworkSynchronousMode SyncMode;
	const FNeuralNetworkAsyncSyncData& SyncData;
	const FNeuralNetworkAsyncOrtVariables& OrtData;
};

#endif //WITH_UE_AND_ORT_SUPPORT
