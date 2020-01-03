// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/Queue.h"
#include "Lumin/CAPIShims/LuminAPI.h"

class FMagicLeapMediaCodecPlayer;

enum class EMagicLeapMediaCodecInputWorkerTaskType : uint8
{
	None,
	Seek,
	SelectTrack
};

struct FMagicLeapMediaCodecInputWorkerTask
{
public:
	EMagicLeapMediaCodecInputWorkerTaskType TaskType;
	FTimespan SeekTime;
	int32 TrackIndex;

public:
	FMagicLeapMediaCodecInputWorkerTask()
	: TaskType(EMagicLeapMediaCodecInputWorkerTaskType::None)
	, SeekTime(FTimespan::Zero())
	, TrackIndex(0)
	{}

	FMagicLeapMediaCodecInputWorkerTask(EMagicLeapMediaCodecInputWorkerTaskType InTaskType, const FTimespan& InSeekTime)
	: TaskType(InTaskType)
	, SeekTime(InSeekTime)
	, TrackIndex(0)
	{}

	FMagicLeapMediaCodecInputWorkerTask(EMagicLeapMediaCodecInputWorkerTaskType InTaskType, int32 InTrackIndex, const FTimespan& InSeekTime)
	: TaskType(InTaskType)
	, SeekTime(InSeekTime)
	, TrackIndex(InTrackIndex)
	{}
};

class FMagicLeapMediaCodecInputWorker : public FRunnable
{
public:
	FMagicLeapMediaCodecInputWorker();
	virtual ~FMagicLeapMediaCodecInputWorker();

	void InitThread(FMagicLeapMediaCodecPlayer& InOwnerPlayer, MLHandle& InExtractorHandle, FCriticalSection& InCriticalSection, FCriticalSection& InGT_IT_Mutex, FCriticalSection& InRT_IT_Mutex);
	void DestroyThread();
	virtual uint32 Run() override;

	void WakeUp();
	void Seek(FTimespan SeekTime);
	void SelectTrack(int32 TrackIndex, const FTimespan& SeekTime);

	bool HasReachedInputEOS() const;

private:
	void ProcessInputSample_WorkerThread();
	bool Seek_WorkerThread(const FTimespan& SeekTime);
	void SelectTrack_WorkerThread(int32 TrackIndex, const FTimespan& SeekTime);

	FMagicLeapMediaCodecPlayer* OwnerPlayer;
	MLHandle* ExtractorHandle;
	FCriticalSection* CriticalSection;
	FCriticalSection* GT_IT_Mutex;
	FCriticalSection* RT_IT_Mutex;

	FRunnableThread* Thread;
	FEvent* Semaphore;
	FThreadSafeCounter StopTaskCounter;

	bool bReachedInputEndOfStream;

	TQueue<FMagicLeapMediaCodecInputWorkerTask, EQueueMode::Mpsc> IncomingTasks;
};
