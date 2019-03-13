// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HighlightRecorder.h"
#include "WmfMp4Writer.h"
#include "GameplayMediaEncoderSample.h"

#include "Misc/Paths.h"

#include "Engine/GameEngine.h"
#include "RenderingThread.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "ProfilingDebugging/CsvProfiler.h"

#include "HAL/PlatformTime.h"
#include "HAL/PlatformFilemanager.h"
#include "UnrealEngine.h"
#include "VideoRecordingSystem.h"

DEFINE_LOG_CATEGORY(WMF);
DEFINE_LOG_CATEGORY(HighlightRecorder);

WINDOWSPLATFORMFEATURES_START

//////////////////////////////////////////////////////////////////////////
// console commands for testing

FAutoConsoleCommand HighlightRecorderStart(TEXT("HighlightRecorder.Start"), TEXT("Starts recording of highlight clip, optional parameter: max duration (float, 30 seconds by default)"), FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FHighlightRecorder::Start));
FAutoConsoleCommand HighlightRecorderStop(TEXT("HighlightRecorder.Stop"), TEXT("Stops recording of highlight clip"), FConsoleCommandDelegate::CreateStatic(&FHighlightRecorder::StopCmd));
FAutoConsoleCommand HighlightRecorderPause(TEXT("HighlightRecorder.Pause"), TEXT("Pauses recording of highlight clip"), FConsoleCommandDelegate::CreateStatic(&FHighlightRecorder::PauseCmd));
FAutoConsoleCommand HighlightRecorderResume(TEXT("HighlightRecorder.Resume"), TEXT("Resumes recording of highlight clip"), FConsoleCommandDelegate::CreateStatic(&FHighlightRecorder::ResumeCmd));
FAutoConsoleCommand HighlightRecorderSave(TEXT("HighlightRecorder.Save"), TEXT("Saves highlight clip, optional parameters: filename (\"test.mp4\" by default) and max duration (float, secs, duration of ring buffer by default)"), FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FHighlightRecorder::SaveCmd));

//////////////////////////////////////////////////////////////////////////

FHighlightRecorder* FHighlightRecorder::Singleton = nullptr;

CSV_DECLARE_CATEGORY_EXTERN(WindowsVideoRecordingSystem);

//////////////////////////////////////////////////////////////////////////

FHighlightRecorder::FHighlightRecorder()
{
	check(Singleton == nullptr);
}

FHighlightRecorder::~FHighlightRecorder()
{
	Stop();
	UE_LOG(HighlightRecorder, Log, TEXT("destroyed"));
}

bool FHighlightRecorder::Start(double RingBufferDurationSecs)
{
	CSV_SCOPED_TIMING_STAT(WindowsVideoRecordingSystem, HighlightRecorder_Start);

	if (State != EState::Stopped)
	{
		UE_LOG(HighlightRecorder, Error, TEXT("cannot start recording, invalid state: %d"), static_cast<int32>(State.Load()));
		return false;
	}

	RingBuffer.Reset();
	RingBuffer.SetMaxDuration(FTimespan::FromSeconds(RingBufferDurationSecs));

	RecordingStartTime = FTimespan::FromSeconds(FPlatformTime::Seconds());
	PauseTimestamp = 0;
	TotalPausedDuration = 0;
	NumPushedFrames = 0;

	if (!FGameplayMediaEncoder::Get()->RegisterListener(this))
	{
		return false;
	}

	State = EState::Recording;

	UE_LOG(HighlightRecorder, Log, TEXT("recording started, ring buffer %.2f secs"), RingBufferDurationSecs);

	return true;
}

bool FHighlightRecorder::Pause(bool bPause)
{
	CSV_SCOPED_TIMING_STAT(WindowsVideoRecordingSystem, HighlightRecorder_Pause);

	if (State == EState::Stopped)
	{
		UE_LOG(HighlightRecorder, Error, TEXT("cannot pause/resume recording, recording is stopped"));
		return false;
	}

	if (bPause && PauseTimestamp == 0.0)
	{
		PauseTimestamp = GetRecordingTime();
		State = EState::Paused;
		UE_LOG(HighlightRecorder, Log, TEXT("paused"));
	}
	else if (!bPause && PauseTimestamp != 0.0)
	{
		FTimespan LastPausedDuration = GetRecordingTime() - PauseTimestamp;
		TotalPausedDuration += LastPausedDuration;
		PauseTimestamp = 0;
		FPlatformMisc::MemoryBarrier();
		State = EState::Recording;
		UE_LOG(HighlightRecorder, Log, TEXT("resumed after %.3f s"), LastPausedDuration.GetTotalSeconds());
	}

	return true;
}

void FHighlightRecorder::Stop()
{
	CSV_SCOPED_TIMING_STAT(WindowsVideoRecordingSystem, HighlightRecorder_Stop);

	FGameplayMediaEncoder::Get()->UnregisterListener(this);
	State = EState::Stopped;

	UE_LOG(HighlightRecorder, Log, TEXT("recording stopped"));
}

FTimespan FHighlightRecorder::GetRecordingTime() const
{
	return FTimespan::FromSeconds(FPlatformTime::Seconds()) - RecordingStartTime - TotalPausedDuration;
}

void FHighlightRecorder::OnMediaSample(const FGameplayMediaEncoderSample& Sample)
{
	// We might be paused, so don't do anything
	if (State != EState::Recording)
	{
		return;
	}

	// Only start pushing video frames once we receive a key frame
	if (NumPushedFrames == 0 && Sample.GetType() == EMediaType::Video)
	{
		if (!Sample.IsVideoKeyFrame())
		{
			return;
		}

		++NumPushedFrames;
	}

	RingBuffer.Push(Sample);
}

bool FHighlightRecorder::SaveHighlight(const TCHAR* Filename, FDoneCallback InDoneCallback, double MaxDurationSecs)
{
	CSV_SCOPED_TIMING_STAT(WindowsVideoRecordingSystem, HighlightRecorder_Save);

	if (State == EState::Stopped)
	{
		UE_LOG(HighlightRecorder, Error, TEXT("cannot save clip when recording is stopped"));
		return false;
	}

	if (bSaving)
	{
		UE_LOG(HighlightRecorder, Error, TEXT("saving is busy with the previous clip"));
		return false;
	}

	UE_LOG(HighlightRecorder, Log, TEXT("start saving to %s, max duration %.3f"), Filename, MaxDurationSecs);

	FString LocalFilename = Filename;

	bSaving = true;
	DoneCallback = MoveTemp(InDoneCallback);

	{
		CSV_SCOPED_TIMING_STAT(WindowsVideoRecordingSystem, HighlightRecorder_SaveThreadCreation);
		BackgroundProcessor.Reset(new FThread(TEXT("Highlight Saving"), [this, LocalFilename, MaxDurationSecs]()
		{
			SaveHighlightInBackground(LocalFilename, MaxDurationSecs);
		}));
	}

	return true;
}

// the bool result is solely for convenience (CHECK_HR) and is ignored as it's a thread function and
// nobody checks it's result. Actual result is notified by the callback.
bool FHighlightRecorder::SaveHighlightInBackground(const FString& Filename, double MaxDurationSecs)
{
	CSV_SCOPED_TIMING_STAT(WindowsVideoRecordingSystem, HighlightRecorder_SaveInBackground);

	double T0 = FPlatformTime::Seconds();

	bool bRes = true;

	TArray<FGameplayMediaEncoderSample> Samples = RingBuffer.GetCopy();

	do // once
	{
		if (!InitialiseMp4Writer(Filename))
		{
			bRes = false;
			break;
		}

		int SampleIndex;
		FTimespan StartTime;
		if (!GetSavingStart(Samples, FTimespan::FromSeconds(MaxDurationSecs), SampleIndex, StartTime))
		{
			bRes = false;
			break;
		}

		checkf(Samples[SampleIndex].IsVideoKeyFrame(), TEXT("t %.3f d %.3f"), Samples[SampleIndex].GetTime().GetTotalSeconds(), Samples[SampleIndex].GetDuration().GetTotalSeconds());

		if (SampleIndex == Samples.Num())
		{
			UE_LOG(HighlightRecorder, Error, TEXT("no samples to save to .mp4"));
			bRes = false;
			break;
		}

		UE_LOG(HighlightRecorder, Verbose, TEXT("writting %d samples to .mp4, %.3f s, starting from %.3f s, index %d"), Samples.Num() - SampleIndex, (Samples.Last().GetTime() - StartTime + Samples.Last().GetDuration()).GetTotalSeconds(), StartTime.GetTotalSeconds(), SampleIndex);

		// get samples starting from `StartTime` and push them into Mp4Writer
		for (; SampleIndex != Samples.Num() && !bStopSaving; ++SampleIndex)
		{
			FGameplayMediaEncoderSample& Sample = Samples[SampleIndex];
			Sample.SetTime(Sample.GetTime() - StartTime);
			if (!Mp4Writer->Write(Sample))
			{
				bRes = false;
				break;
			}
		}
	} while (false);

	if (bRes)
	{
		if (!Mp4Writer->Finalize())
		{
			bRes = false;
		}
	}

	double PassedSecs = FPlatformTime::Seconds() - T0;
	UE_LOG(HighlightRecorder, Log, TEXT("saving to %s %s, took %.3f msecs"), *Filename, bRes ? TEXT("succeeded") : TEXT("failed"), PassedSecs);

	bSaving = false;

	DoneCallback(bRes);

	return bRes;
}

bool FHighlightRecorder::InitialiseMp4Writer(const FString& Filename)
{
	FString VideoCaptureDir = FPaths::VideoCaptureDir();
	auto& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*VideoCaptureDir))
	{
		bool bRes = PlatformFile.CreateDirectory(*VideoCaptureDir);
		if (!bRes)
		{
			UE_LOG(HighlightRecorder, Error, TEXT("Can't create directory %s"), *VideoCaptureDir);
			return false;
		}
	}

	FString FullFilename = PlatformFile.ConvertToAbsolutePathForExternalAppForWrite(*(VideoCaptureDir + Filename));

	Mp4Writer.Reset(new FWmfMp4Writer);

	if (!Mp4Writer->Initialize(*FullFilename))
	{
		return false;
	}

	TRefCountPtr<IMFMediaType> AudioType;
	if (!FGameplayMediaEncoder::Get()->GetAudioOutputType(AudioType))
	{
		return false;
	}

	DWORD StreamIndex;
	if (!Mp4Writer->CreateStream(AudioType, StreamIndex))
	{
		return false;
	}

	if (StreamIndex != static_cast<decltype(StreamIndex)>(EMediaType::Audio))
	{
		UE_LOG(HighlightRecorder, Error, TEXT("Invalid audio stream index: %d"), StreamIndex);
		return false;
	}

	TRefCountPtr<IMFMediaType> VideoType;
	if (!FGameplayMediaEncoder::Get()->GetVideoOutputType(VideoType))
	{
		return false;
	}

	if (!Mp4Writer->CreateStream(VideoType, StreamIndex))
	{
		return false;
	}

	if (StreamIndex != static_cast<decltype(StreamIndex)>(EMediaType::Video))
	{
		UE_LOG(HighlightRecorder, Error, TEXT("Invalid video stream index: %d"), StreamIndex);
		return false;
	}

	if (!Mp4Writer->Start())
	{
		return false;
	}

	return true;
}

// finds index and timestamp of the first sample that should be written to .mp4
bool FHighlightRecorder::GetSavingStart(const TArray<FGameplayMediaEncoderSample>& Samples, FTimespan MaxDuration, int& StartIndex, FTimespan& StartTime) const
// the first sample in .mp4 file should have timestamp 0 and all other timestamps should be relative to the
// first one
// 1) if `MaxDurationSecs` > actual ring buffer duration (last sample timestamp - first) -> we need to save all
// samples from the ring buffer. saving start time = first sample timestamp
// 2) if `MaxDurationSecs` < actual ring buffer duration -> we need to start from the first video key-frame with
// timestamp > than ("cur time" - "max duration to save")
{
	// convert max duration to absolute time
	StartTime = GetRecordingTime() - MaxDuration;

	if (Samples.Num() == 0)
	{
		StartIndex = 0;
		return true;
	}

	FTimespan FirstTimestamp = Samples[0].GetTime();

	if (FirstTimestamp > StartTime)
	{
		StartTime = FirstTimestamp;
		StartIndex = 0;
		return true;
	}

	int i = 0;
	bool bFound = false;
	for (; i != Samples.Num(); ++i)
	{
		FTimespan Time = Samples[i].GetTime();
		if (Time >= StartTime && Samples[i].IsVideoKeyFrame())
		{
			// correct StartTime to match timestamp of the first sample to be written
			StartTime = Time;
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		UE_LOG(HighlightRecorder, Error, TEXT("No samples to write to .mp4, max duration: %.3f"), MaxDuration.GetTotalSeconds());
		return false;
	}

	StartIndex = i;

	return true;
}

WINDOWSPLATFORMFEATURES_END

