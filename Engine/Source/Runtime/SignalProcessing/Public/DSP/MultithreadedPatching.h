// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DSP/BufferVectorOperations.h"
#include "DSP/Dsp.h"

namespace Audio
{
	/**
	 * This is a buffer that is owned by the FPatchMixer.
	 */
	struct SIGNALPROCESSING_API FPatchOutput
	{
	public:
		FPatchOutput(int32 MaxCapacity, float InGain = 1.0f);

		/** Copies the minimum of NumSamples or however many samples are available into OutBuffer. Returns the number of samples copied. */
		int32 PopAudio(float* OutBuffer, int32 NumSamples, bool bUseLatestAudio);

		/** Sums the minimum of NumSamples or however many samples are available into OutBuffer. Returns the number of samples summed into OutBuffer. */
		int32 MixInAudio(float* OutBuffer, int32 NumSamples, bool bUseLatestAudio);

		friend class FPatchInput;
		friend class FPatchMixer;
	private:

		// Internal buffer.
		TCircularAudioBuffer<float> InternalBuffer;

		// For MixInAudio, audio is popped off of InternalBuffer onto here, and then mixed into OutBuffer in MixInAudio.
		AlignedFloatBuffer MixingBuffer;

		// This is applied in PopAudio or MixInAudio.
		TAtomic<float> TargetGain;
		float PreviousGain;

		// This is used to breadcrumb the FPatchOutput when we want to delete it.
		int32 PatchID;

		static TAtomic<int32> PatchIDCounter;
	};

	/** Patch outputs are owned by the FPatchMixer, and are pinned by the FPatchInput. */
	typedef TSharedPtr<FPatchOutput, ESPMode::ThreadSafe> FPatchOutputPtr;

	/**
	 * Handle to a patch. Should only be used by a single thread.
	 */
	class SIGNALPROCESSING_API FPatchInput
	{
	public:
		FPatchInput(const FPatchOutputPtr& InOutput);

		/** pushes audio from InBuffer to the corresponding FPatchOutput.
		 *  Returns how many samples were able to be pushed, or -1 if the output was disconnected.
		 */
		int32 PushAudio(const float* InBuffer, int32 NumSamples);

		void SetGain(float InGain);

		/** Returns false if this output was removed, either because someone called FPatchMixer::RemoveTap with this FPatchInput, or the FPatchMixer was destroyed. */
		bool IsOutputStillActive();

		friend class FPatchMixer;

	private:
		/** Weak pointer to our destination buffer. */
		TWeakPtr<FPatchOutput, ESPMode::ThreadSafe> OutputHandle;
	};

	/**
	 * This class is used for retrieving and mixing down audio from multiple threads.
	 * Important to note that this is MPSC: while multiple threads can enqueue audio on an instance of FPatchMixer using instances of FPatchInput,
	 * only one thread can call PopAudio safely.
	 */
	class SIGNALPROCESSING_API FPatchMixer
	{
	public:
		/** Constructor. */
		FPatchMixer();

		/** Adds a new input to the tap collector. Calling this is thread safe, but individual instances of FPatchInput are only safe to be used from one thread. */
		FPatchInput AddNewPatch(int32 MaxLatencyInSamples, float InGain);

		/** Removes a tap from the tap collector. Calling this is thread safe, though FPatchOutput will likely not be deleted until the next call of PopAudio. */
		void RemovePatch(const FPatchInput& TapInput);

		/** Mixes all inputs into a single buffer. This should only be called from a single thread. Returns the number of non-silent samples popped to OutBuffer. */
		int32 PopAudio(float* OutBuffer, int32 OutNumSamples, bool bUseLatestAudio);

	private:
		/** Called within PopAudio. Flushes the PendingNewPatches array into CurrentPatches. During this function, AddNewPatch is blocked. */
		void ConnectNewPatches();

		/** Called within PopAudio. Removes PendingTapsToDelete from CurrentPatches and ConnectNewPatches. During this function, RemoveTap and AddNewPatch are blocked. */
		void CleanUpDisconnectedPatches();

		/** New taps are added here in AddNewPatch, and then are moved to CurrentPatches in ConnectNewPatches. */
		TArray<FPatchOutputPtr> PendingNewPatches;
		/** Contended by AddNewPatch, ConnectNewPatches and CleanUpDisconnectedTaps. */
		FCriticalSection PendingNewPatchesCriticalSection;

		/** Patch IDs of individual audio taps that will be removed on the next call of CleanUpDisconnectedPatches. */
		TArray<int32> DisconnectedPatches;
		/** Contended by RemoveTap, AddNewPatch, and ConnectNewPatches. */
		FCriticalSection PatchDeletionCriticalSection;

		/** Only accessed within PopAudio. Indirect array of taps that are mixed in during PopAudio. */
		TArray<FPatchOutputPtr> CurrentPatches;
	};
}
