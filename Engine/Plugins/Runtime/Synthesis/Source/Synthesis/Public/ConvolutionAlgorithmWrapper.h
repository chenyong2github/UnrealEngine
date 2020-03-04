// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once


#include "CoreMinimal.h"
#include "Components/AudioComponent.h"
#include "DSP/ConvolutionAlgorithm.h"

namespace Audio
{

	class FConvolutionAlgorithmWrapper;


	// ========================================================================
	// FConvolutionAlgorithmBuilder:
	//
	// Async Task class used by FConvolutionAlgorithmWrapper.
	// Giving new IRs to a convolution algorithm object can be expensive (FFT and copies)
	// This allows us to do this work asynchronously
	// ========================================================================
	class SYNTHESIS_API ConvolutionAlgorithmBuilder : public FNonAbandonableTask
	{
	public:
		ConvolutionAlgorithmBuilder(FConvolutionAlgorithmWrapper* InAlgorithmWrapperPtr)
			: AlgorithmWrapperPtr(InAlgorithmWrapperPtr)
		{
		}

	private:
		void DoWork();

		FConvolutionAlgorithmWrapper* AlgorithmWrapperPtr;

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(ConvolutionAlgorithmBuilder, STATGROUP_ThreadPoolAsyncTasks);
		}

		friend class FAsyncTask<ConvolutionAlgorithmBuilder>;
	}; // class ConvolutionAlgorithmBuilder


	// ========================================================================
	// FConvolutionAlgorithmWrapper:
	//
	// Wrapper around IConvolutionAlgorithm
	// Automatically handles re-building the underlying ConvolutionAlgorithm object asynchronosuly
	// Deals with thread saftey (methods are safe to call from any thread)
	// ========================================================================
	class SYNTHESIS_API FConvolutionAlgorithmWrapper
	{
	public:
		FConvolutionAlgorithmWrapper();

		FConvolutionAlgorithmWrapper(const FConvolutionSettings& InSettings, AlignedFloatBuffer InitialIrData);

		virtual ~FConvolutionAlgorithmWrapper();

		// meant to be called from audio render thread
		virtual void ProcessAudio(int32 NumInputChannels, const AlignedFloatBuffer& InputInterleavedAudioBuffer, int32 NumOutputChannels, AlignedFloatBuffer& OutputInterleavedAudioBuffer);

		// Can be called on any thread. Will do heavy lifting on Game Thread (ProcessAudio will perform pass-through in the meantime)
		void UpdateConvolutionObject(const FConvolutionSettings& InNewSettings, const AlignedFloatBuffer* OptionalInNewIrDataPtr = nullptr);

	#pragma region Getters
		void Reset();

		// Hardware Acceleration
		bool IsHardwareAccelerationAllowed() const;

		// block size
		int32 GetBlockSizeInSamples() const;

		// Number of input channels
		int32 GetNumInputChannels() const;

		// Number of output channels
		int32 GetNumOutputChannels() const;

		// Number of impulse response channels
		int32 GetNumImpulseResponseChannels() const;

		// maximum number of impulse response channels
		int32 GetMaxNumImpulseResponseChannels() const;
	#pragma endregion
	
	private:
		// spawns a task on the game thread. bIsPreparedToRenderAudio will be false until the new convolution object is ready
		void AsyncRebuildConvolutionObject(const AlignedFloatBuffer* OptionalInNewIrDataPtr = nullptr);
	
		void AsyncRebuildConvolutionObjectInternal();
	
		bool CurrentSettingsAreRenderable();

	#pragma region AudioBufferHelpers
		// audio buffer-related helpers
		static void GetAccessToBufferAs2DArray(float* InBufferPtr, const int32 NumChannels, const int32 NumFrames, TArray<float*>& Out2DArray);

		static void InterleaveBuffer(float* Dest, const float* Src, const int32 NumSamples, const int32 NumChannels);

		static void DeInterleaveBuffer(float* Dest, const float* Src, const int32 NumSamples, const int32 NumChannels);

		static void ReverseBufferInPlace(float* InOutBuffer, const int32 NumSamples, const int32 NumChannels, bool bIsInterleaved);

		static void ReverseInterleavedBufferInPlace(float* InOutBuffer, const int32 NumSamples, const int32 NumChannels);

		static void ReverseDeinterleavedBufferInPlace(float* InOutBuffer, const int32 NumSamples, const int32 NumChannels);
	#pragma endregion

		TUniquePtr<IConvolutionAlgorithm> CurrentConvolutionAlgorithm;

		TUniquePtr<FAsyncTask<ConvolutionAlgorithmBuilder>> TaskPtr;

		FConvolutionSettings CurrentSettings;

		// cache this so we can rebuild the algorithm (for non-IR reasons) without depending on a UObject
		AlignedFloatBuffer CachedDeinterleavedIR;

		// data is passed to the convolution algorithm as 2D arrays
		TArray<float*> TempInput2DFloatArray;

		// data is recieved from the convolution algorithm as 2D arrays
		TArray<float*> TempOuput2DFloatArray;

		// scratch buffer used to de-interleave input audio
		AlignedFloatBuffer ScratchInputBuffer;

		// scratch buffer used to interleave output audio
		AlignedFloatBuffer ScratchOutputBuffer;

		// critical section used to protect audio rendering
		FCriticalSection ChangingSettingsCritSec;

		// allow async worker to rebuild dsp object
		friend class ConvolutionAlgorithmBuilder;

	}; // class FConvolutionAlgorithmWrapper

} // namespace Audio
