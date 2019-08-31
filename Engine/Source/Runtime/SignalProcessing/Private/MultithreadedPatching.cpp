// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	{

	}


	int32 FPatchOutput::PopAudio(float* OutBuffer, int32 NumSamples, bool bUseLatestAudio)
	{
		if (bUseLatestAudio && InternalBuffer.Num() > (uint32) NumSamples)
		{
			InternalBuffer.SetNum(((uint32)NumSamples));
		}

		int32 PopResult = InternalBuffer.Pop(OutBuffer, NumSamples);
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

	int32 FPatchOutput::MixInAudio(float* OutBuffer, int32 NumSamples, bool bUseLatestAudio)
	{
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


	FPatchInput::FPatchInput(const FPatchOutputPtr& InOutput)
		: OutputHandle(InOutput)
	{

	}

	int32 FPatchInput::PushAudio(const float* InBuffer, int32 NumSamples)
	{
		FPatchOutputPtr OutputPtr = OutputHandle.Pin();
		if (!OutputPtr.IsValid())
		{
			return -1;
		}

		int32 SamplesPushed = OutputPtr->InternalBuffer.Push(InBuffer, NumSamples);

		return SamplesPushed;
	}

	void FPatchInput::SetGain(float InGain)
	{
		FPatchOutputPtr OutputPtr = OutputHandle.Pin();
		if (!OutputPtr.IsValid())
		{
			return;
		}

		OutputPtr->TargetGain = InGain;
	}

	bool FPatchInput::IsOutputStillActive()
	{
		return OutputHandle.IsValid();
	}

	FPatchMixer::FPatchMixer()
	{
	}


	FPatchInput FPatchMixer::AddNewPatch(int32 MaxLatencyInSamples, float InGain)
	{
		FScopeLock ScopeLock(&PendingNewPatchesCriticalSection);

		int32 NewPatchIndex = PendingNewPatches.Emplace(new FPatchOutput(MaxLatencyInSamples, InGain));
		return FPatchInput(PendingNewPatches[NewPatchIndex]);
	}

	void FPatchMixer::RemovePatch(const FPatchInput& PatchInput)
	{
		FPatchOutputPtr OutputPtr = PatchInput.OutputHandle.Pin();

		// If the output is already disconnected, early exit.
		if (!OutputPtr.IsValid())
		{
			return;
		}

		FScopeLock ScopeLock(&PatchDeletionCriticalSection);
		DisconnectedPatches.Add(OutputPtr->PatchID);
	}

	int32 FPatchMixer::PopAudio(float* OutBuffer, int32 OutNumSamples, bool bUseLatestAudio)
	{
		CleanUpDisconnectedPatches();
		ConnectNewPatches();

		FMemory::Memzero(OutBuffer, OutNumSamples * sizeof(float));
		int32 MaxPoppedSamples = 0;

		for (FPatchOutputPtr& OutputPtr : CurrentPatches)
		{
			const int32 NumPoppedSamples = OutputPtr->MixInAudio(OutBuffer, OutNumSamples, bUseLatestAudio);
			MaxPoppedSamples = FMath::Max(NumPoppedSamples, MaxPoppedSamples);
		}

		return MaxPoppedSamples;
	}

	void FPatchMixer::ConnectNewPatches()
	{
		// If AddNewPatch is called in a separate thread, wait until the next PopAudio call to do this work.
		if (PendingNewPatchesCriticalSection.TryLock())
		{
			// Todo: convert this to move semantics to avoid copying the shared pointer around.
			for (FPatchOutputPtr& Patch : PendingNewPatches)
			{
				CurrentPatches.Add(Patch);
			}

			PendingNewPatches.Reset();
			PendingNewPatchesCriticalSection.Unlock();
		}
	}

	void FPatchMixer::CleanUpDisconnectedPatches()
	{
		if (PatchDeletionCriticalSection.TryLock())
		{
			// Iterate through all of the PatchIDs we need to clean up.
			for (const int32& PatchID : DisconnectedPatches)
			{
				bool bPatchRemoved = false;

				// First, make sure that the patch isn't in the pending new patchs we haven't added yet:
				{
					FScopeLock ScopeLock(&PendingNewPatchesCriticalSection);
					for (int32 Index = 0; Index < PendingNewPatches.Num(); Index++)
					{
						checkSlow(CurrentPatches[Index].IsValid());

						if (PatchID == PendingNewPatches[Index]->PatchID)
						{
							PendingNewPatches.RemoveAtSwap(Index);
							bPatchRemoved = true;
							break;
						}
					}
				}

				if (bPatchRemoved)
				{
					continue;
				}

				// Next, we check out current patchs.
				for (int32 Index = 0; Index < CurrentPatches.Num(); Index++)
				{
					checkSlow(CurrentPatches[Index].IsValid());

					if (PatchID == CurrentPatches[Index]->PatchID)
					{
						CurrentPatches.RemoveAtSwap(Index);
						break;
					}
				}
			}

			DisconnectedPatches.Reset();

			PatchDeletionCriticalSection.Unlock();
		}
	}
}
