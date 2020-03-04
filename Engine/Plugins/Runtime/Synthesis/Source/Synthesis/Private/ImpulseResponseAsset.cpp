// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImpulseResponseAsset.h"
#include "Async/Async.h"


void ImpulseResponesConverter::DoWork()
{
	IrAssetPtr->PrepareIRsForConvolutionAlgorithmInternal(TargetSampleRate);
	IrAssetPtr->SetSubscriberFlags(true);
}


UImpulseResponse::UImpulseResponse()
: TaskPtr{ nullptr }
, NumChannels(0)
, NumFrames(0)
, AssetSampleRate(0)
{
}

UImpulseResponse::~UImpulseResponse()
{
	// stop our task if its in flight
	if (TaskPtr)
	{
		TaskPtr->EnsureCompletion();
	}

	// set all subscriber flags to false
	// (this is probably unnecessary, since they accesses this object via WeakPtr)
	SetSubscriberFlags(false);
}

void UImpulseResponse::SubscribeFlagToIrChanges(FThreadSafeBool* ExternalThreadSafeBool)
{
	FScopeLock Lock(&SubscriberFlagsCritSec);

	FThreadSafeBool** Flags = SubscriberFlags.GetData();

	// try to replace null entry in array
	const int32 NumFlags = SubscriberFlags.Num();
	for (int32 i = 0; i < NumFlags; ++i)
	{
		if (!Flags[i])
		{
			Flags[i] = ExternalThreadSafeBool;
			return;
		}
	}

	// array was full
	SubscriberFlags.Add(ExternalThreadSafeBool);
}

void UImpulseResponse::UnsubscribeFlagFromIrChanges(FThreadSafeBool* ExternalThreadSafeBool)
{
	FScopeLock Lock(&SubscriberFlagsCritSec);

	FThreadSafeBool** Flags = SubscriberFlags.GetData();

	// search for entry in array
	const int32 NumFlags = SubscriberFlags.Num();
	for (int32 i = 0; i < NumFlags; ++i)
	{
		if (Flags[i] == ExternalThreadSafeBool)
		{
			Flags[i] = nullptr;
			return;
		}
	}

	// TODO: Log wargning (trying to unsubscribe flag that was never subscribed)
}

void UImpulseResponse::SetSubscriberFlags(bool bInDataIsReady)
{
	FScopeLock Lock(&SubscriberFlagsCritSec);

	// This access should be safe, since mutation of the Map is locked by a mutex
	// and subscribers should call UnsubscribeFlagFromIrChanges() in their destructor
	for (FThreadSafeBool* TSBool : SubscriberFlags)
	{
		if (TSBool)
		{
			TSBool->AtomicSet(bInDataIsReady);
		}
	}
}

void UImpulseResponse::PrepareIRsForConvolutionAlgorithm(const int32 InSampleRate)
{
	if (IsDataPrepared(InSampleRate))
	{
		SetSubscriberFlags(true);
	}
	else
	{
		// if the task is in flight, let it finish
		if (TaskPtr)
		{
			TaskPtr->EnsureCompletion();
		}

		TaskPtr = MakeUnique<FAsyncTask<ImpulseResponesConverter>>(this, InSampleRate);
		TaskPtr->StartBackgroundTask();
	}
}

void UImpulseResponse::PrepareIRsForConvolutionAlgorithmInternal(const int32 InSampleRate)
{
	const float StepSize = static_cast<float>(InSampleRate) / AssetSampleRate;
	const int32 FinalBufferSize = static_cast<int32>(StepSize * IRData.Num());

	if ((DeinterleavedIR.Num() != FinalBufferSize) || (InSampleRate != AssetSampleRate))
	{
		PerformAssetIrSRC(InSampleRate);
		CurrentSampleRate = InSampleRate;
	}
}

bool UImpulseResponse::IsDataPrepared(const int32 InSampleRate)
{
	// Are we preparing the data asynchronously now?
	if (TaskPtr && !TaskPtr->IsDone())
	{
		return false;
	}
	else if (TaskPtr) // we are holding on to a finished task
	{
		TaskPtr.Reset();
	}

	// Is the current data already prepared
	return (CurrentSampleRate == InSampleRate) && (IRData.Num() == DeinterleavedIR.Num());
}

void UImpulseResponse::PerformAssetIrSRC(int32 InSampleRate)
{
	const float StepSize = AssetSampleRate / static_cast<float>(InSampleRate);
	const int32 AssetBufferSize = IRData.Num();
	const int32 FinalBufferSize = static_cast<int32>(StepSize * AssetBufferSize);
	const int32 FinalNumFrames = static_cast<int32>(StepSize * NumFrames);
	float SampleA, SampleB;

	DeinterleavedIR.Reset();
	DeinterleavedIR.AddUninitialized(FinalBufferSize);

	if (InSampleRate == static_cast<int32>(AssetSampleRate))
	{
		// no src needed, only copy
		FMemory::Memcpy(DeinterleavedIR.GetData(), IRData.GetData(), IRData.Num() * sizeof(float));
		return;
	}

	// SRC: (need to test this with an IR that has a different SR than the FAudioDevice)
	// for each channel...
	for (int32 Chan = 0; Chan < NumChannels; ++Chan)
	{
		const int32 InputChanOffset = NumFrames * Chan;
		const int32 OutputChanOffset = FinalNumFrames * Chan;

		// for each output sample...
		float CurrIndex = 0.0f;
		for (int32 i = 0; i < FinalNumFrames; ++i)
		{
			const float Alpha = FMath::Fmod(CurrIndex, 1.0f);
			const int32 WholeThisIndex = FMath::FloorToInt(CurrIndex);
			int32 WholeNextIndex = WholeThisIndex + 1;

			// check for interpolation between last and first frames
			SampleA = IRData[InputChanOffset + WholeThisIndex];

			if (WholeNextIndex != AssetBufferSize)
			{
				SampleB = IRData[InputChanOffset + WholeNextIndex];
			}
			else
			{
				SampleB = 0.0f;
			}

			DeinterleavedIR[OutputChanOffset + i] = FMath::Lerp(SampleA, SampleB, Alpha);

			CurrIndex += StepSize;
		}
	}
}