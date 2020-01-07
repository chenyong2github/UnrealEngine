// Copyright Epic Games, Inc. All Rights Reserved.

#include "DSP/MultithreadedPatching.h"
#include "DSP/BufferVectorOperations.h"


namespace Audio
{
	TAtomic<int32> FPatchOutput::PatchIDCounter(0);

	FPatchOutput::FPatchOutput(int32 MaxCapacity, float InGain /*= 1.0f*/)
		: InternalBuffer(MaxCapacity)
		, TargetGain(InGain)
		, PreviousGain(InGain)
		, PatchID(++PatchIDCounter)
		, NumAliveInputs(0)
	{

	}


	FPatchOutput::FPatchOutput()
		: InternalBuffer(0)
		, TargetGain(0.0f)
		, PreviousGain(0.0f)
		, PatchID(INDEX_NONE)
		, NumAliveInputs(0)
	{
	}

	int32 FPatchOutput::PopAudio(float* OutBuffer, int32 NumSamples, bool bUseLatestAudio)
	{
		if (IsInputStale())
		{
			return -1;
		}

		if (bUseLatestAudio && InternalBuffer.Num() > (uint32) NumSamples)
		{
			InternalBuffer.SetNum(((uint32)NumSamples));
		}

		int32 PopResult = InternalBuffer.Pop(OutBuffer, NumSamples);

		// Apply gain stage.
		if (FMath::IsNearlyEqual(TargetGain, PreviousGain))
		{
			MultiplyBufferByConstantInPlace(OutBuffer, PopResult, PreviousGain);
		}
		else
		{
			FadeBufferFast(OutBuffer, PopResult, PreviousGain, TargetGain);
			PreviousGain = TargetGain;
		}
		
		return PopResult;
	}

	bool FPatchOutput::IsInputStale() const
	{
		return NumAliveInputs == 0;
	}

	int32 FPatchOutput::MixInAudio(float* OutBuffer, int32 NumSamples, bool bUseLatestAudio)
	{
		if (IsInputStale())
		{
			return -1;
		}

		MixingBuffer.SetNumUninitialized(NumSamples, false);
		int32 PopResult = 0;
		
		if (bUseLatestAudio && InternalBuffer.Num() > (uint32)NumSamples)
		{
			InternalBuffer.SetNum(((uint32)NumSamples));
			PopResult = InternalBuffer.Peek(MixingBuffer.GetData(), NumSamples);
		}
		else
		{
			PopResult = InternalBuffer.Pop(MixingBuffer.GetData(), NumSamples);
		}

		if (FMath::IsNearlyEqual(TargetGain, PreviousGain))
		{
			MixInBufferFast(MixingBuffer.GetData(), OutBuffer, PopResult, PreviousGain);
		}
		else
		{
			MixInBufferFast(MixingBuffer.GetData(), OutBuffer, PopResult, PreviousGain, TargetGain);
			PreviousGain = TargetGain;
		}

		return PopResult;
	}


	int32 FPatchOutput::GetNumSamplesAvailable()
	{
		return InternalBuffer.Num();
	}

	FPatchInput::FPatchInput(const FPatchOutputStrongPtr& InOutput)
		: OutputHandle(InOutput)
		, PushCallsCounter(0)
	{
		if (OutputHandle.IsValid())
		{
			OutputHandle->NumAliveInputs++;
		}
	}

	FPatchInput::FPatchInput()
		: PushCallsCounter(0)
	{
	}

	FPatchInput::FPatchInput(const FPatchInput& Other)
		: FPatchInput(Other.OutputHandle)
	{
	}

	FPatchInput& FPatchInput::operator=(const FPatchInput& Other)
	{
		OutputHandle = Other.OutputHandle;
		PushCallsCounter = 0;
		
		if (OutputHandle.IsValid())
		{
			OutputHandle->NumAliveInputs++;
		}

		return *this;
	}

	FPatchInput::~FPatchInput()
	{
		if (OutputHandle.IsValid())
		{
			OutputHandle->NumAliveInputs--;
		}
	}

	int32 FPatchInput::PushAudio(const float* InBuffer, int32 NumSamples)
	{
		if (!OutputHandle.IsValid())
		{
			return -1;
		}

		int32 SamplesPushed = OutputHandle->InternalBuffer.Push(InBuffer, NumSamples);

		// Every so often, we check to see if the output handle has been destroyed and clean it up.
		static const int32 NumPushCallsUntilCleanupCheck = 256;
		
		PushCallsCounter = (PushCallsCounter + 1) % NumPushCallsUntilCleanupCheck;
		if (PushCallsCounter == 0 && OutputHandle.IsUnique())
		{
			// Delete the output.
			OutputHandle.Reset();
		}

		return SamplesPushed;
	}

	void FPatchInput::SetGain(float InGain)
	{
		if (!OutputHandle.IsValid())
		{
			return;
		}

		OutputHandle->TargetGain = InGain;
	}

	bool FPatchInput::IsOutputStillActive()
	{
		return OutputHandle.IsUnique() || OutputHandle.IsValid();
	}

	FPatchMixer::FPatchMixer()
	{
	}


	FPatchInput FPatchMixer::AddNewInput(int32 MaxLatencyInSamples, float InGain)
	{
		FScopeLock ScopeLock(&PendingNewInputsCriticalSection);

		int32 NewPatchIndex = PendingNewInputs.Emplace(new FPatchOutput(MaxLatencyInSamples, InGain));
		return FPatchInput(PendingNewInputs[NewPatchIndex]);
	}

	void FPatchMixer::RemovePatch(const FPatchInput& PatchInput)
	{
		// If the output is already disconnected, early exit.
		if (!PatchInput.OutputHandle.IsValid())
		{
			return;
		}

		FScopeLock ScopeLock(&InputDeletionCriticalSection);
		DisconnectedInputs.Add(PatchInput.OutputHandle->PatchID);
	}

	int32 FPatchMixer::PopAudio(float* OutBuffer, int32 OutNumSamples, bool bUseLatestAudio)
	{
		FScopeLock ScopeLock(&CurrentPatchesCriticalSection);

		CleanUpDisconnectedPatches();
		ConnectNewPatches();

		FMemory::Memzero(OutBuffer, OutNumSamples * sizeof(float));
		int32 MaxPoppedSamples = 0;

		for (FPatchOutputStrongPtr& OutputPtr : CurrentInputs)
		{
			const int32 NumPoppedSamples = OutputPtr->MixInAudio(OutBuffer, OutNumSamples, bUseLatestAudio);
			MaxPoppedSamples = FMath::Max(NumPoppedSamples, MaxPoppedSamples);
		}

		return MaxPoppedSamples;
	}

	int32 FPatchMixer::Num()
	{
		FScopeLock ScopeLock(&CurrentPatchesCriticalSection);
		return CurrentInputs.Num();
	}

	int32 FPatchMixer::MaxNumberOfSamplesThatCanBePopped()
	{
		FScopeLock ScopeLock(&CurrentPatchesCriticalSection);
		ConnectNewPatches();

		// Iterate through our inputs and see which input has the least audio buffered.
		uint32 SmallestNumSamplesBuffered = TNumericLimits<uint32>::Max();

		for (FPatchOutputStrongPtr& Output : CurrentInputs)
		{
			if (Output.IsValid())
			{
				SmallestNumSamplesBuffered = FMath::Min(SmallestNumSamplesBuffered, Output->InternalBuffer.Num());
			}
		}

		if (SmallestNumSamplesBuffered == TNumericLimits<uint32>::Max())
		{
			return -1;
		}
		else
		{
			// If this check is hit, we need to either change this function to return an int64 or find a different way to notify the caller that all outputs have been disconeccted.
			check(SmallestNumSamplesBuffered <= ((uint32)TNumericLimits<int32>::Max()));
			return SmallestNumSamplesBuffered;
		}
	}

	void FPatchMixer::ConnectNewPatches()
	{
		FScopeLock ScopeLock(&PendingNewInputsCriticalSection);

		// If AddNewPatch is called in a separate thread, wait until the next PopAudio call to do this work.
		// Todo: convert this to move semantics to avoid copying the shared pointer around.
		for (FPatchOutputStrongPtr& Patch : PendingNewInputs)
		{
			CurrentInputs.Add(Patch);
		}

		PendingNewInputs.Reset();
	}

	void FPatchMixer::CleanUpDisconnectedPatches()
	{
		FScopeLock PendingInputDeletionScopeLock(&InputDeletionCriticalSection);

		// Iterate through all of the PatchIDs we need to clean up.
		for (const int32& PatchID : DisconnectedInputs)
		{
			bool bInputRemoved = false;

			// First, make sure that the patch isn't in the pending new patchs we haven't added yet:
			{
				FScopeLock PendingNewInputsScopeLock(&PendingNewInputsCriticalSection);
				for (int32 Index = 0; Index < PendingNewInputs.Num(); Index++)
				{
					checkSlow(CurrentInputs[Index].IsValid());

					if (PatchID == PendingNewInputs[Index]->PatchID)
					{
						PendingNewInputs.RemoveAtSwap(Index);
						bInputRemoved = true;
						break;
					}
				}
			}

			if (bInputRemoved)
			{
				continue;
			}

			// Next, we check out current patchs.
			for (int32 Index = 0; Index < CurrentInputs.Num(); Index++)
			{
				checkSlow(CurrentInputs[Index].IsValid());

				if (PatchID == CurrentInputs[Index]->PatchID)
				{
					CurrentInputs.RemoveAtSwap(Index);
					break;
				}
			}
		}

		DisconnectedInputs.Reset();
	}

	FPatchSplitter::FPatchSplitter()
	{
	}

	FPatchSplitter::~FPatchSplitter()
	{
	}

	Audio::FPatchOutputStrongPtr FPatchSplitter::AddNewPatch(int32 MaxLatencyInSamples, float InGain)
	{
		// Allocate a new FPatchOutput, then store a weak pointer to it in our PendingOutputs array to be added in our next call to PushAudio.
		FPatchOutputStrongPtr StrongOutputPtr = MakeShareable(new FPatchOutput(MaxLatencyInSamples * 2, InGain));

		{
			FScopeLock ScopeLock(&PendingOutputsCriticalSection);
			PendingOutputs.Emplace(StrongOutputPtr);
		}

		return StrongOutputPtr;
	}

	int32 FPatchSplitter::Num()
	{
		FScopeLock ScopeLock(&ConnectedOutputsCriticalSection);
		AddPendingPatches();
		return ConnectedOutputs.Num();
	}

	int32 FPatchSplitter::MaxNumberOfSamplesThatCanBePushed()
	{
		FScopeLock ScopeLock(&ConnectedOutputsCriticalSection);
		AddPendingPatches();

		// Iterate over our outputs and get the smallest remainder of all of our circular buffers.
		uint32 SmallestRemainder = TNumericLimits<uint32>::Max();

		for (FPatchInput& Input : ConnectedOutputs)
		{
			if (Input.OutputHandle.IsValid())
			{
				SmallestRemainder = FMath::Min(SmallestRemainder, Input.OutputHandle->InternalBuffer.Remainder());
			}
		}

		if (SmallestRemainder == TNumericLimits<uint32>::Max())
		{
			return -1;
		}
		else
		{
			// If we hit this check, we need to either return an int64 or use some other method to notify the caller that all outputs are disconnected.
			check(SmallestRemainder <= ((uint32)TNumericLimits<int32>::Max()));
			return SmallestRemainder;
		}
	}

	void FPatchSplitter::AddPendingPatches()
	{
		FScopeLock ScopeLock(&PendingOutputsCriticalSection);
		ConnectedOutputs.Append(PendingOutputs);
		PendingOutputs.Reset();
	}

	int32 FPatchSplitter::PushAudio(const float* InBuffer, int32 InNumSamples)
	{
		AddPendingPatches();

		FScopeLock ScopeLock(&ConnectedOutputsCriticalSection);

		int32 MinimumSamplesPushed = TNumericLimits<int32>::Max();

		// Iterate through our array of connected outputs from the end, removing destroyed outputs as we go.
		for (int32 Index = ConnectedOutputs.Num() - 1; Index >= 0; Index--)
		{
			int32 NumSamplesPushed = ConnectedOutputs[Index].PushAudio(InBuffer, InNumSamples);
			if (NumSamplesPushed >= 0)
			{
				MinimumSamplesPushed = FMath::Min(MinimumSamplesPushed, NumSamplesPushed);
			}
			else
			{
				// If this output has been destroyed, remove it from our array of connected outputs.
				ConnectedOutputs.RemoveAtSwap(Index);
			}
		}

		// If we weren't able to push audio to any of our outputs, return -1.
		if (MinimumSamplesPushed == TNumericLimits<int32>::Max())
		{
			MinimumSamplesPushed = -1;
		}

		return MinimumSamplesPushed;
	}

	FPatchMixerSplitter::FPatchMixerSplitter()
	{
	}

	FPatchMixerSplitter::~FPatchMixerSplitter()
	{
	}

	Audio::FPatchOutputStrongPtr FPatchMixerSplitter::AddNewOutput(int32 MaxLatencyInSamples, float InGain)
	{
		return Splitter.AddNewPatch(MaxLatencyInSamples, InGain);
	}

	Audio::FPatchInput FPatchMixerSplitter::AddNewInput(int32 MaxLatencyInSamples, float InGain)
	{
		return Mixer.AddNewInput(MaxLatencyInSamples, InGain);
	}

	void FPatchMixerSplitter::RemovePatch(const FPatchInput& TapInput)
	{
		Mixer.RemovePatch(TapInput);
	}

	void FPatchMixerSplitter::ProcessAudio()
	{
		int32 NumSamplesToForward = FMath::Min(Mixer.MaxNumberOfSamplesThatCanBePopped(), Splitter.MaxNumberOfSamplesThatCanBePushed());
		
		if (NumSamplesToForward <= 0)
		{
			// Likely there are either no inputs or no outputs connected, or one of the inputs has not pushed any audio yet. Early exit.
			return;
		}

		IntermediateBuffer.Reset();
		IntermediateBuffer.AddUninitialized(NumSamplesToForward);

		// Mix down inputs:
		int32 PopResult = Mixer.PopAudio(IntermediateBuffer.GetData(), NumSamplesToForward, false);
		check(PopResult == NumSamplesToForward);
		
		OnProcessAudio(TArrayView<const float>(IntermediateBuffer));

		// Push audio to outputs:
		int32 PushResult = Splitter.PushAudio(IntermediateBuffer.GetData(), NumSamplesToForward);
		check(PushResult == NumSamplesToForward);
	}
}
