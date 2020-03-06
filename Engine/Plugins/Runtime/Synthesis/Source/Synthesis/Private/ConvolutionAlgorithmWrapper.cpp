// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConvolutionAlgorithmWrapper.h"
#include "Async/Async.h"
#include "Misc/ScopeTryLock.h"

namespace Audio
{

	void ConvolutionAlgorithmBuilder::DoWork()
	{
		check(AlgorithmWrapperPtr);
		AlgorithmWrapperPtr->AsyncRebuildConvolutionObjectInternal();
	}

	// Default constructor. Object is not prepared to render audio
	FConvolutionAlgorithmWrapper::FConvolutionAlgorithmWrapper()
	{
	}

	FConvolutionAlgorithmWrapper::FConvolutionAlgorithmWrapper(const FConvolutionSettings & InSettings, AlignedFloatBuffer InitialIrData)
		: CurrentSettings(InSettings)
		, CachedDeinterleavedIR(InitialIrData)
	{
	}

	FConvolutionAlgorithmWrapper::~FConvolutionAlgorithmWrapper()
	{
		if (TaskPtr)
		{
			TaskPtr->EnsureCompletion();
			TaskPtr = nullptr;
		}
	}

	void FConvolutionAlgorithmWrapper::ProcessAudio(int32 NumInputChannels, const AlignedFloatBuffer& InputInterleavedAudioBuffer, int32 NumOutputChannels, AlignedFloatBuffer& OutputInterleavedAudioBuffer)
	{
		const int32 NumFramesInThisCallback = InputInterleavedAudioBuffer.Num() / NumInputChannels;

		// see if something changed between callbacks which requires updating the Convolution Algorithm callback
		const bool bSomethingChanged = NumInputChannels != CurrentSettings.NumInputChannels		// input channel config changed...
									|| NumOutputChannels != CurrentSettings.NumOutputChannels	// ...or output channel config changed
									|| NumFramesInThisCallback != CurrentSettings.BlockNumSamples;			// ...or callback size changed

		// not waiting for mutex
		FScopeTryLock ScopeLock(&ChangingSettingsCritSec);

		if (bSomethingChanged)
		{
			CurrentSettings.BlockNumSamples = NumFramesInThisCallback;
			CurrentSettings.NumInputChannels = NumInputChannels;
			CurrentSettings.NumOutputChannels = NumOutputChannels;

			UpdateConvolutionObject(CurrentSettings);
		}

		// Passing-through audio to output: Either something changed or we couldn't obtain the mutex (dsp obj being rebuilt)
		if (bSomethingChanged || !ScopeLock.IsLocked() || !CurrentConvolutionAlgorithm.IsValid())
		{
			const int32 MinNumChannels = FMath::Min(NumInputChannels, NumOutputChannels);
			const int32 NumSamples = InputInterleavedAudioBuffer.Num();
			const int32 NumFrames = NumSamples / NumInputChannels;
			const float* InputPtr = InputInterleavedAudioBuffer.GetData();
			float* OutputPtr = OutputInterleavedAudioBuffer.GetData();

			if (NumInputChannels == NumOutputChannels)
			{
				FMemory::Memcpy(OutputPtr, InputPtr, NumSamples * sizeof(float));
			}
			else
			{
				// input and output channel config are different
				// todo: better mix-down than channel to channel copy?
				for (int32 CurrFrame = 0; CurrFrame < NumFrames; ++CurrFrame)
				{
					for (int32 CurrChan = 0; CurrChan < MinNumChannels; ++CurrChan)
					{
						OutputPtr[CurrFrame * NumOutputChannels + CurrChan] = InputPtr[CurrFrame * NumInputChannels + CurrChan];
					}
				}
			}

			return; // end pass-through
		}

		// Prepare scratch buffers (intermediate data is de-interleaved)
		int32 NumInputSamples = InputInterleavedAudioBuffer.Num();
		int32 NumOutputSamples = OutputInterleavedAudioBuffer.Num();

		ScratchInputBuffer.Reset();
		ScratchInputBuffer.AddDefaulted(NumInputSamples);

		ScratchOutputBuffer.Reset();
		ScratchOutputBuffer.AddDefaulted(NumOutputSamples);

		// get pointers to buffers
		const float* InputBufferPtr = InputInterleavedAudioBuffer.GetData();
		float* OutputBufferPtr = OutputInterleavedAudioBuffer.GetData();
		float* ScratchInputBufferPtr = ScratchInputBuffer.GetData();
		float* ScratchOutputBufferPtr = ScratchOutputBuffer.GetData();

		// De-interleave Input buffer into scratch input buffer
		DeInterleaveBuffer(ScratchInputBufferPtr, InputBufferPtr, NumInputSamples, NumInputChannels);

		// Convolution Algorithm object expects 2D arrays
		GetAccessToBufferAs2DArray(ScratchInputBufferPtr, NumInputChannels, NumFramesInThisCallback, TempInput2DFloatArray);
		GetAccessToBufferAs2DArray(ScratchOutputBufferPtr, NumOutputChannels, NumFramesInThisCallback, TempOuput2DFloatArray);

		// get next buffer of convolution output (store in scratch output buffer)
		CurrentConvolutionAlgorithm->ProcessAudioBlock(TempInput2DFloatArray.GetData(), TempOuput2DFloatArray.GetData());

		// re-interleave scratch output buffer into final output buffer
		InterleaveBuffer(OutputBufferPtr, ScratchOutputBufferPtr, NumOutputSamples, NumOutputChannels);
	}

	void FConvolutionAlgorithmWrapper::UpdateConvolutionObject(const FConvolutionSettings& InNewSettings, const AlignedFloatBuffer* OptionalInNewIrData)
	{
		// early exit if nothing is changing
		if (CurrentSettings == InNewSettings && !OptionalInNewIrData)
		{
			return;
		}

		CurrentSettings = InNewSettings;
		AsyncRebuildConvolutionObject(OptionalInNewIrData);
	}


#pragma region AudioBufferHelpers
	void FConvolutionAlgorithmWrapper::GetAccessToBufferAs2DArray(float * InBufferPtr, const int32 NumChannels, const int32 NumFrames, TArray<float*>& Out2DArray)
	{
		// Obtain pointers to first sample in each de-interleaved channel
		Out2DArray.Reset();
		Out2DArray.AddUninitialized(NumChannels);

		float** OutIrPtrs = Out2DArray.GetData();
		int32 ChannelOffset = 0;

		for (int32 i = 0; i < NumChannels; ++i)
		{
			OutIrPtrs[i] = (InBufferPtr + ChannelOffset);
			ChannelOffset += NumFrames;
		}
	}

	void FConvolutionAlgorithmWrapper::InterleaveBuffer(float* Dest, const float* Src, const int32 NumSamples, const int32 NumChannels)
	{
		const int32 NumFrames = NumSamples / NumChannels;
		for (int32 i = 0; i < NumSamples; ++i)
		{
			const int32 CurrFrame = i / NumChannels;
			const int32 CurrChannel = i % NumChannels;
			const int32 DeinterleavedIndex = CurrChannel * NumFrames + CurrFrame;
			Dest[i] = Src[DeinterleavedIndex];
		}
	}

	void FConvolutionAlgorithmWrapper::DeInterleaveBuffer(float* Dest, const float* Src, const int32 NumSamples, const int32 NumChannels)
	{
		const int32 NumFrames = NumSamples / NumChannels;
		for (int32 i = 0; i < NumSamples; ++i)
		{
			const int32 CurrFrame = i / NumChannels;
			const int32 CurrChannel = i % NumChannels;
			const int32 CurrOutputIndex = CurrChannel * NumFrames + CurrFrame;
			Dest[CurrOutputIndex] = Src[i];
		}
	}

	void FConvolutionAlgorithmWrapper::ReverseBufferInPlace(float* InOutBuffer, const int32 NumSamples, const int32 NumChannels, bool bIsInterleaved)
	{
		if (bIsInterleaved)
		{
			FConvolutionAlgorithmWrapper::ReverseInterleavedBufferInPlace(InOutBuffer, NumSamples, NumChannels);
		}
		else
		{
			FConvolutionAlgorithmWrapper::ReverseDeinterleavedBufferInPlace(InOutBuffer, NumSamples, NumChannels);
		}
	}

	void FConvolutionAlgorithmWrapper::ReverseInterleavedBufferInPlace(float* InOutBuffer, const int32 NumSamples, const int32 NumChannels)
	{
		const int32 NumFrames = NumSamples / NumChannels;
		const int32 HalfNumFrames = NumFrames >> 1;
		const int32 FrameSizeInBytes = NumChannels * sizeof(float);

		float* BufferStart = InOutBuffer;
		float* BufferEnd = BufferStart + (NumFrames - 1) * NumChannels;

		for (int32 i = 0; i < HalfNumFrames; ++i)
		{
			FMemory::Memswap(BufferStart, BufferEnd, FrameSizeInBytes);
			BufferStart += NumChannels;
			BufferEnd -= NumChannels;
		}
	}

	void FConvolutionAlgorithmWrapper::ReverseDeinterleavedBufferInPlace(float* InOutBuffer, const int32 NumSamples, const int32 NumChannels)
	{
		const int32 NumFrames = NumSamples / NumChannels;
		const int32 HalfNumFrames = NumFrames >> 1;

		for (int32 Chan = 0; Chan < NumChannels; ++Chan)
		{
			float* ChanStart = InOutBuffer + (Chan * NumFrames);
			float* ChanEnd = ChanStart + (NumFrames - 1);

			for (int32 i = 0; i < HalfNumFrames; ++i)
			{
				FMemory::Memswap(ChanStart++, ChanEnd--, sizeof(float));
			}
		}
	}
#pragma endregion

#pragma region Getters
	void FConvolutionAlgorithmWrapper::Reset()
	{
		FScopeLock ScopeLock(&ChangingSettingsCritSec);

		if (CurrentConvolutionAlgorithm)
		{
			CurrentConvolutionAlgorithm = nullptr;
		}
	}

	bool Audio::FConvolutionAlgorithmWrapper::IsHardwareAccelerationAllowed() const
	{
		return CurrentSettings.bEnableHardwareAcceleration;
	}

	int32 Audio::FConvolutionAlgorithmWrapper::GetBlockSizeInSamples() const
	{
		return CurrentSettings.BlockNumSamples;
	}

	int32 Audio::FConvolutionAlgorithmWrapper::GetNumInputChannels() const
	{
		return CurrentSettings.NumInputChannels;
	}

	int32 FConvolutionAlgorithmWrapper::GetNumOutputChannels() const
	{
		return CurrentSettings.NumOutputChannels;
	}

	int32 FConvolutionAlgorithmWrapper::GetNumImpulseResponseChannels() const
	{
		return CurrentSettings.NumImpulseResponses;
	}

	int32 FConvolutionAlgorithmWrapper::GetMaxNumImpulseResponseChannels() const
	{
		return CurrentSettings.MaxNumImpulseResponseSamples;
	}
#pragma endregion

	void FConvolutionAlgorithmWrapper::AsyncRebuildConvolutionObject(const AlignedFloatBuffer* OptionalInNewIrData)
	{
		FScopeLock ScopeLock(&ChangingSettingsCritSec);
		// if we have new IR data, we need to make our own copy for processing
		// if we have no new IR data, and the channel config on the new data doesn't match,
		//	we need to shutdown the current algorithm until the new IR is ready
		if (OptionalInNewIrData)
		{
			CachedDeinterleavedIR = *OptionalInNewIrData; // 'spensive :(
		}
		else
		{
			Reset();
		}

		if (!CurrentSettingsAreRenderable())
		{
			return;
		}

		// start async task
		if (TaskPtr)
		{
			TaskPtr->EnsureCompletion();
		}

		TaskPtr = MakeUnique<FAsyncTask<ConvolutionAlgorithmBuilder>>(this);
		TaskPtr->StartBackgroundTask();
	}

	void FConvolutionAlgorithmWrapper::AsyncRebuildConvolutionObjectInternal()
	{
		FScopeLock ScopeLock(&ChangingSettingsCritSec);

		const int32 NumIrFrames = CachedDeinterleavedIR.Num() / CurrentSettings.NumImpulseResponses;
		const int32 NumIrChannels = CurrentSettings.NumImpulseResponses;

		CurrentConvolutionAlgorithm = FConvolutionFactory::NewConvolutionAlgorithm(CurrentSettings);

		// If fails, prior creation function warns why algorithm failed to be created.
		if (CurrentConvolutionAlgorithm.IsValid())
		{
			if (CachedDeinterleavedIR.Num())
			{
				GetAccessToBufferAs2DArray(CachedDeinterleavedIR.GetData(), NumIrChannels, NumIrFrames, TempInput2DFloatArray);

				check(TempInput2DFloatArray.Num() == CurrentSettings.NumImpulseResponses);

				// Hand over impulse responses to convolution reverb object
				for (int32 i = 0; i < CurrentSettings.NumImpulseResponses; ++i)
				{
					CurrentConvolutionAlgorithm->SetImpulseResponse(i, TempInput2DFloatArray[i], CurrentSettings.MaxNumImpulseResponseSamples);
				}

				// Set up (identity) 3D gain matrix
				// todo: Make the gain matrix accessible to the sound designer in a meaningful way
				const int32 MinNumChannels = FMath::Min3(CurrentSettings.NumInputChannels, CurrentSettings.NumOutputChannels, CurrentSettings.NumImpulseResponses);

				for (int i = 0; i < MinNumChannels; ++i)
				{
					CurrentConvolutionAlgorithm->SetMatrixGain(i, i, i, 1.0f);
				}
			}
		}
	}

	bool FConvolutionAlgorithmWrapper::CurrentSettingsAreRenderable()
	{
		return CurrentSettings.BlockNumSamples > 0
			&& CurrentSettings.MaxNumImpulseResponseSamples > 0
			&& CurrentSettings.NumImpulseResponses > 0
			&& CurrentSettings.NumInputChannels > 0
			&& CurrentSettings.NumOutputChannels > 0;
	}
} // namespace Audio