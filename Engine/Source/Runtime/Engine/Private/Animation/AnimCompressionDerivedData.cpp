// Copyright Epic Games, Inc. All Rights Reserved.
#include "Animation/AnimCompressionDerivedData.h"
#include "Animation/AnimCompressionDerivedDataPublic.h"
#include "CoreMinimal.h"
#include "DerivedDataCacheInterface.h"
#include "Stats/Stats.h"
#include "Animation/AnimSequence.h"
#include "Serialization/MemoryWriter.h"
#include "AnimationUtils.h"
#include "AnimEncoding.h"
#include "Animation/AnimCompress.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "AnimationCompression.h"
#include "UObject/Package.h"

#if WITH_EDITOR

DECLARE_CYCLE_STAT(TEXT("Anim Compression (Derived Data)"), STAT_AnimCompressionDerivedData, STATGROUP_Anim);

FDerivedDataAnimationCompression::FDerivedDataAnimationCompression(const TCHAR* InTypeName, const FString& InAssetDDCKey, TSharedPtr<FAnimCompressContext> InCompressContext)
	: TypeName(InTypeName)
	, AssetDDCKey(InAssetDDCKey)
	, CompressContext(InCompressContext)
{

}

FDerivedDataAnimationCompression::~FDerivedDataAnimationCompression()
{
}

const TCHAR* FDerivedDataAnimationCompression::GetVersionString() const
{
	// This is a version string that mimics the old versioning scheme. If you
	// want to bump this version, generate a new guid using VS->Tools->Create GUID and
	// return it here. Ex.
	return TEXT("0F1CECE507FE4F89A374B4C8E7B55052");
}

bool FDerivedDataAnimationCompression::Build( TArray<uint8>& OutDataArray )
{
	const double CompressionStartTime = FPlatformTime::Seconds();

	check(DataToCompressPtr.IsValid());
	FCompressibleAnimData& DataToCompress = *DataToCompressPtr.Get();
	FCompressedAnimSequence OutData;

	SCOPE_CYCLE_COUNTER(STAT_AnimCompressionDerivedData);
	UE_LOG(LogAnimationCompression, Log, TEXT("Building Anim DDC data for %s"), *DataToCompress.FullName);

	FCompressibleAnimDataResult CompressionResult;

	bool bCompressionSuccessful = false;
	{
		DataToCompress.Update(OutData);

		const bool bBoneCompressionOk = FAnimationUtils::CompressAnimBones(DataToCompress, CompressionResult);
		const bool bCurveCompressionOk = FAnimationUtils::CompressAnimCurves(DataToCompress, OutData);

#if DO_CHECK
		FString CompressionName = DataToCompress.BoneCompressionSettings->GetFullName();
		const TCHAR* AAC = CompressContext.Get()->bAllowAlternateCompressor ? TEXT("true") : TEXT("false");
		const TCHAR* OutputStr = CompressContext.Get()->bOutput ? TEXT("true") : TEXT("false");
#endif

		bCompressionSuccessful = bBoneCompressionOk && bCurveCompressionOk;

		ensureMsgf(bCompressionSuccessful, TEXT("Anim Compression failed for Sequence '%s' with compression scheme '%s': compressed data empty\n\tAnimIndex: %i\n\tMaxAnim:%i\n\tAllowAltCompressor:%s\n\tOutput:%s"), 
											*DataToCompress.FullName,
											*CompressionName,
											CompressContext.Get()->AnimIndex,
											CompressContext.Get()->MaxAnimations,
											AAC,
											OutputStr);
	}

	if (bCompressionSuccessful)
	{
		const double CompressionEndTime = FPlatformTime::Seconds();
		const double CompressionTime = CompressionEndTime - CompressionStartTime;

		CompressContext->GatherPostCompressionStats(OutData, DataToCompress.BoneData, DataToCompress.AnimFName, CompressionTime, true);

		OutData.CompressedByteStream = MoveTemp(CompressionResult.CompressedByteStream);
		OutData.CompressedDataStructure = MoveTemp(CompressionResult.AnimData);
		OutData.BoneCompressionCodec = CompressionResult.Codec;

		FMemoryWriter Ar(OutDataArray, true);
		OutData.SerializeCompressedData(Ar, true, nullptr, DataToCompress.Skeleton, DataToCompress.BoneCompressionSettings, DataToCompress.CurveCompressionSettings); //Save out compressed
	}

	return bCompressionSuccessful;
}

const uint64 GigaBytes = 1024 * 1024 * 1024;
const uint64 MAX_ASYNC_COMPRESSION_MEM_USAGE = 2 * GigaBytes;
const int32 MAX_ACTIVE_COMPRESSIONS = 2;

FAsyncCompressedAnimationsManagement* GAsyncCompressedAnimationsTracker = nullptr;

FAsyncCompressedAnimationsManagement& FAsyncCompressedAnimationsManagement::Get()
{
	static FAsyncCompressedAnimationsManagement SingletonInstance;
	GAsyncCompressedAnimationsTracker = &SingletonInstance;
	return SingletonInstance;
}

void FAsyncCompressedAnimationsManagement::OnActiveCompressionFinished(int32 ActiveAnimIndex)
{
	FDerivedDataCacheInterface& DerivedDataCache = GetDerivedDataCacheRef();

	FActiveAsyncCompressionTask& Task = ActiveAsyncCompressionTasks[ActiveAnimIndex];

	TArray<uint8> OutData;
	bool bBuiltLocally = false;
	if (DerivedDataCache.GetAsynchronousResults(Task.AsyncHandle, OutData, &bBuiltLocally))
	{
		Task.Sequence->ApplyCompressedData(Task.CacheKey, Task.bPerformFrameStripping, OutData);
	}
	else
	{
		UE_LOG(LogAnimationCompression, Fatal, TEXT("Failed to get async compressed animation data for anim '%s'"), *Task.Sequence->GetName());
		Task.Sequence->ApplyCompressedData(FString(), false, OutData); // Clear active compression on Sequence
	}
	ActiveMemoryUsage -= Task.TaskSize;
	ActiveAsyncCompressionTasks.RemoveAtSwap(ActiveAnimIndex, 1, false);
}

void FAsyncCompressedAnimationsManagement::Tick(float DeltaTime)
{
	const double MaxProcessingTime = 0.1; // try not to hang the editor too much
	const double EndTime = FPlatformTime::Seconds() + MaxProcessingTime;
	const double StartTime = FPlatformTime::Seconds();

	FDerivedDataCacheInterface& DerivedDataCache = GetDerivedDataCacheRef();

	for (int32 ActiveAnim = ActiveAsyncCompressionTasks.Num() - 1; ActiveAnim >= 0; --ActiveAnim)
	{
		const FActiveAsyncCompressionTask& Task = ActiveAsyncCompressionTasks[ActiveAnim];

		if (DerivedDataCache.PollAsynchronousCompletion(Task.AsyncHandle))
		{
			OnActiveCompressionFinished(ActiveAnim);
		}

		if (FPlatformTime::Seconds() > EndTime)
		{
			return; // Finish for this tick
		}
	}

	const bool bHasQueuedTasks = QueuedAsyncCompressionWork.Num() > 0;

	while (ActiveAsyncCompressionTasks.Num() < MAX_ACTIVE_COMPRESSIONS)
	{
		if (QueuedAsyncCompressionWork.Num() == 0)
		{
			break;
		}
		FQueuedAsyncCompressionWork NewTask = QueuedAsyncCompressionWork.Pop(false);
		StartAsyncWork(NewTask.Compressor, NewTask.Anim, NewTask.Compressor.GetMemoryUsage(), NewTask.bPerformFrameStripping);
	}

	if (bHasQueuedTasks && QueuedAsyncCompressionWork.Num() == 0)
	{
		QueuedAsyncCompressionWork.Empty(); //free memory
	}
}

void FAsyncCompressedAnimationsManagement::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (const FActiveAsyncCompressionTask& Task : ActiveAsyncCompressionTasks)
	{
		Task.DataToCompress->AddReferencedObjects(Collector);
	}

	for (const FQueuedAsyncCompressionWork QueuedTask : QueuedAsyncCompressionWork)
	{
		QueuedTask.Compressor.GetCompressibleData()->AddReferencedObjects(Collector);
	}
}

TStatId FAsyncCompressedAnimationsManagement::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FAsyncCompressedAnimationsTracker, STATGROUP_Tickables);
}

bool FAsyncCompressedAnimationsManagement::RequestAsyncCompression(FDerivedDataAnimationCompression& Compressor, UAnimSequence* Anim, const bool bPerformFrameStripping, TArray<uint8>& OutData)
{
	const uint64 NewTaskSize = Compressor.GetMemoryUsage();

	bool bWasAsync = true;

	if (ActiveMemoryUsage + NewTaskSize >= MAX_ASYNC_COMPRESSION_MEM_USAGE)
	{
		Tick(0.f); // Try to free up some memory
	}

	const bool bCanRunASync = (ActiveMemoryUsage + NewTaskSize < MAX_ASYNC_COMPRESSION_MEM_USAGE);
	const bool bForceSync = false; //debugging override

	if (bCanRunASync && !bForceSync)
	{
		//Queue Async
		ActiveMemoryUsage += NewTaskSize;

		if(ActiveAsyncCompressionTasks.Num() < MAX_ACTIVE_COMPRESSIONS)
		{
			StartAsyncWork(Compressor, Anim, NewTaskSize, bPerformFrameStripping);
		}
		else
		{
			QueuedAsyncCompressionWork.Emplace(Compressor, Anim, bPerformFrameStripping);
		}
	}
	else
	{
		//Do in place
		GetDerivedDataCacheRef().GetSynchronous(&Compressor, OutData);
		bWasAsync = false;
	}
	return bWasAsync;
}

void FAsyncCompressedAnimationsManagement::StartAsyncWork(FDerivedDataAnimationCompression& Compressor, UAnimSequence* Anim, const uint64 NewTaskSize, const bool bPerformFrameStripping)
{
	const FString CacheKey = Compressor.GetPluginSpecificCacheKeySuffix();
	FCompressibleAnimPtr SourceData = Compressor.GetCompressibleData();
	uint32 AsyncHandle = GetDerivedDataCacheRef().GetAsynchronous(&Compressor);
	ActiveAsyncCompressionTasks.Emplace(Anim, SourceData, CacheKey, NewTaskSize, AsyncHandle, bPerformFrameStripping);
}

bool FAsyncCompressedAnimationsManagement::WaitOnActiveCompression(UAnimSequence* Anim)
{
	for (int32 ActiveIndex = 0; ActiveIndex < ActiveAsyncCompressionTasks.Num(); ++ActiveIndex)
	{
		FActiveAsyncCompressionTask& Task = ActiveAsyncCompressionTasks[ActiveIndex];
		if (Task.Sequence == Anim)
		{
			GetDerivedDataCacheRef().WaitAsynchronousCompletion(Task.AsyncHandle);
			OnActiveCompressionFinished(ActiveIndex);
			return true; // Done
		}
	}
	return false;
}

bool FAsyncCompressedAnimationsManagement::WaitOnExistingCompression(UAnimSequence* Anim, const bool bCancelIfNotStarted)
{
	if(!WaitOnActiveCompression(Anim))
	{
		//Check if we have a queued task
		for (int32 QueuedIndex = 0; QueuedIndex < QueuedAsyncCompressionWork.Num(); ++QueuedIndex)
		{
			FQueuedAsyncCompressionWork& Task = QueuedAsyncCompressionWork[QueuedIndex];
			if (Task.Anim == Anim)
			{
				if (!bCancelIfNotStarted)
				{
					StartAsyncWork(Task.Compressor, Task.Anim, Task.Compressor.GetMemoryUsage(), Task.bPerformFrameStripping);
				}
				else
				{
					delete &Task.Compressor;
				}
				QueuedAsyncCompressionWork.RemoveAtSwap(QueuedIndex, 1, false);
			}
		}
		return bCancelIfNotStarted ? false : WaitOnActiveCompression(Anim);
	}
	return true;
}

#endif	//WITH_EDITOR
