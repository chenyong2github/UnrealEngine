// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConvolutionReverb.h"

#include "CoreMinimal.h"
#include "SynthesisModule.h"
#include "DSP/ConvolutionAlgorithm.h"
#include "DSP/BufferVectorOperations.h"

namespace Audio
{
	const FConvolutionReverbSettings FConvolutionReverbSettings::DefaultSettings;
	FConvolutionReverbSettings::FConvolutionReverbSettings()
	:	NormalizationVolume(-24.f)
	,	RearChannelBleed(0.f)
	,	bRearChannelFlip(false)
	{}


	FConvolutionReverb::FConvolutionReverb(TUniquePtr<IConvolutionAlgorithm> InAlgorithm, const FConvolutionReverbSettings& InSettings)
	:	Settings()
	,	ConvolutionAlgorithm(nullptr)
	,	OutputGain(1.f)
	,	bIsSurroundOutput(false)
	,	bHasSurroundBleed(false)
	,	ExpectedNumFramesPerCallback(0)
	,	NumInputSamplesPerBlock(0)
	,	NumOutputSamplesPerBlock(0)
	{
		SetSettings(InSettings);
		SetConvolutionAlgorithm(MoveTemp(InAlgorithm));
		UpdateBlockBuffers();
	}

	void FConvolutionReverb::SetSettings(const FConvolutionReverbSettings& InSettings)
	{
		Settings = InSettings;

		Update();
	}

	const FConvolutionReverbSettings& FConvolutionReverb::GetSettings() const
	{
		return Settings;
	}

	void FConvolutionReverb::SetConvolutionAlgorithm(TUniquePtr<IConvolutionAlgorithm> InAlgorithm)
	{
		ConvolutionAlgorithm = MoveTemp(InAlgorithm);

		Update();
	}

	void FConvolutionReverb::ProcessAudio(int32 NumInputChannels, AlignedFloatBuffer& InputAudio, int32 NumOutputChannels, AlignedFloatBuffer& OutputAudio)
	{
		check(InputBlockBuffer.IsValid());
		check(OutputBlockBuffer.IsValid());

		OutputAudio.Reset();

		const int32 NumInputSamples = InputAudio.Num();
		const int32 NumFrames = NumInputSamples / NumInputChannels;
		check((NumInputSamples % NumFrames) == 0);
		const int32 NumOutputSamples = NumFrames * NumOutputChannels;

		if (NumFrames != ExpectedNumFramesPerCallback)
		{
			ExpectedNumFramesPerCallback = NumFrames;
			// If the number of frames processed per a call changes, then
			// block buffers need to be updated. 
			UpdateBlockBuffers();
		}

		if (NumFrames < 1)
		{
			return;
		}

		if (NumOutputSamples > 0)
		{
			OutputAudio.AddZeroed(NumOutputSamples);
		}

		// If we don't have a valid algorithm. We cannot do any processing. 
		if (!ConvolutionAlgorithm.IsValid())
		{
			// Early exit due to invalid algorithm pointer
			return;
		}

		// Our convolution algorithm should have equal number of input and output channels.
		if (NumInputChannels != ConvolutionAlgorithm->GetNumAudioInputs())
		{
			UE_LOG(LogSynthesis, Error, TEXT("Convolution num input channel mismatch. Expected %d, got %d"), ConvolutionAlgorithm->GetNumAudioInputs(), NumInputChannels);
			return;
		}
		else if (NumOutputChannels != ConvolutionAlgorithm->GetNumAudioOutputs())
		{
			UE_LOG(LogSynthesis, Error, TEXT("Convolution num output channel mismatch. Expected %d, got %d"), ConvolutionAlgorithm->GetNumAudioOutputs(), NumOutputChannels);
			return;
		}

		// Process all available audio that completes a block.
		InputBlockBuffer->AddSamples(InputAudio.GetData(), NumInputSamples);

		while (InputBlockBuffer->GetNumAvailable() >= NumInputSamplesPerBlock)
		{
			const float* InputBlock = InputBlockBuffer->InspectSamples(NumInputSamplesPerBlock);

			if (nullptr != InputBlock)
			{
				ProcessAudioBlock(InputBlock, NumInputChannels, InterleavedOutputBlock, NumOutputChannels);
				OutputBlockBuffer->AddSamples(InterleavedOutputBlock.GetData(), InterleavedOutputBlock.Num());
			}

			InputBlockBuffer->RemoveSamples(NumInputSamplesPerBlock);
		}

		// Pop off all audio that's ready to go.
		int32 NumToPop = FMath::Min(OutputBlockBuffer->GetNumAvailable(), NumOutputSamples);

		if (NumToPop > 0)
		{
			int32 OutputCopyOffset = NumOutputSamples - NumToPop;
			const float* OutputPtr = OutputBlockBuffer->InspectSamples(NumToPop);
			if (nullptr != OutputPtr)
			{
				FMemory::Memcpy(&OutputAudio.GetData()[OutputCopyOffset], OutputPtr, NumToPop * sizeof(float));
			}
			OutputBlockBuffer->RemoveSamples(NumToPop);
		}
	}

	void FConvolutionReverb::ProcessAudioBlock(const float* InputAudio, int32 InNumInputChannels, AlignedFloatBuffer& OutputAudio, int32 InNumOutputChannels)
	{
		check(nullptr != InputAudio);
		check(ConvolutionAlgorithm.IsValid());

		const int32 NumFrames = ConvolutionAlgorithm->GetNumSamplesInBlock();
		const int32 NumInputSamples = InNumInputChannels * NumFrames;
		const int32 NumOutputSamples = InNumOutputChannels * NumFrames;

		TArrayView<const float> InputView(InputAudio, NumInputSamples);

		// De-interleave Input buffer into scratch input buffer
		DeinterleaveBuffer(InputDeinterleaveBuffers, InputView, InNumInputChannels);

		// setup pointers for convolution in case any reallocation has happened.
		for (int32 i = 0; i < InNumInputChannels; i++)
		{
			InputBufferPtrs[i] = InputDeinterleaveBuffers[i].GetData();
		}

		for (int32 i = 0; i < InNumOutputChannels; i++)
		{
			OutputBufferPtrs[i] = OutputDeinterleaveBuffers[i].GetData();
		}

		// get next buffer of convolution output (store in scratch output buffer)
		ConvolutionAlgorithm->ProcessAudioBlock(InputBufferPtrs.GetData(), OutputBufferPtrs.GetData());

		// add surround bleed
		if (bIsSurroundOutput && bHasSurroundBleed)
		{
			const int32 OutputChannelOffset = InNumOutputChannels - 2;

			if (!Settings.bRearChannelFlip)
			{
				MixInBufferFast(OutputDeinterleaveBuffers[0], OutputDeinterleaveBuffers[OutputChannelOffset], Settings.RearChannelBleed);
				MixInBufferFast(OutputDeinterleaveBuffers[1], OutputDeinterleaveBuffers[OutputChannelOffset + 1], Settings.RearChannelBleed);
			}
			else
			{
				MixInBufferFast(OutputDeinterleaveBuffers[1], OutputDeinterleaveBuffers[OutputChannelOffset], Settings.RearChannelBleed);
				MixInBufferFast(OutputDeinterleaveBuffers[0], OutputDeinterleaveBuffers[OutputChannelOffset + 1], Settings.RearChannelBleed);
			}
		}
			
		// re-interleave scratch output buffer into final output buffer
		InterleaveBuffer(OutputAudio, OutputDeinterleaveBuffers, InNumOutputChannels);

		// Apply final gain
		Audio::MultiplyBufferByConstantInPlace(OutputAudio, OutputGain);
	}

	int32 FConvolutionReverb::GetNumInputChannels() const
	{
		if (!ConvolutionAlgorithm.IsValid())
		{
			return 0;
		}
		return ConvolutionAlgorithm->GetNumAudioInputs();
	}

	int32 FConvolutionReverb::GetNumOutputChannels() const
	{
		if (!ConvolutionAlgorithm.IsValid())
		{
			return 0;
		}
		return ConvolutionAlgorithm->GetNumAudioOutputs();
	}

	void FConvolutionReverb::Update()
	{
		OutputGain = Settings.NormalizationVolume;

		int32 NumOutputChannels = 0;
		int32 NumInputChannels = 0;
		int32 NumFrames = 0;

		NumInputSamplesPerBlock = 0;
		NumOutputSamplesPerBlock = 0;

		if (ConvolutionAlgorithm.IsValid())
		{
			NumOutputChannels = ConvolutionAlgorithm->GetNumAudioOutputs();
			NumInputChannels = ConvolutionAlgorithm->GetNumAudioInputs();
			NumFrames = ConvolutionAlgorithm->GetNumSamplesInBlock();

			NumInputSamplesPerBlock = NumInputChannels * NumFrames;
			NumOutputSamplesPerBlock = NumOutputChannels * NumFrames;

			while (InputDeinterleaveBuffers.Num() < NumInputChannels)
			{
				InputDeinterleaveBuffers.Emplace();
			}

			while (OutputDeinterleaveBuffers.Num() < NumOutputChannels)
			{
				OutputDeinterleaveBuffers.Emplace();
			}

			InputBufferPtrs.SetNumZeroed(NumInputChannels);
			OutputBufferPtrs.SetNumZeroed(NumOutputChannels);

			for (int32 i = 0; i < NumInputChannels; i++)
			{
				AlignedFloatBuffer& Buffer = InputDeinterleaveBuffers[i];
				Buffer.Reset();
				if (NumFrames > 0)
				{
					Buffer.AddUninitialized(NumFrames);
				}
				InputBufferPtrs[i] = Buffer.GetData();
			}

			for (int32 i = 0; i < NumOutputChannels; i++)
			{
				AlignedFloatBuffer& Buffer = OutputDeinterleaveBuffers[i];
				Buffer.Reset();
				if (NumFrames > 0)
				{
					Buffer.AddUninitialized(NumFrames);
				}
				OutputBufferPtrs[i] = Buffer.GetData();
			}
		}

		bIsSurroundOutput = (NumOutputChannels >= 4);
		bHasSurroundBleed = FMath::Abs(Settings.RearChannelBleed) > 0.00101f; // > -59.9db
		
		// Modify the output level if we are using surround bleed.
		// The gain scalar is applied to compensate for louder output
		// if our surround output signal is folded down to stereo
		if (bIsSurroundOutput && bHasSurroundBleed && Settings.bRearChannelFlip)
		{
			OutputGain *= FMath::Sqrt(1.0f/2.0f);
		}
		else if (bIsSurroundOutput && bHasSurroundBleed)
		{
			OutputGain *= 0.5f;
		}

		UpdateBlockBuffers();
	}

	void FConvolutionReverb::UpdateBlockBuffers()
	{
		int32 AlgoBlockSize = 0;
		int32 NumInputChannels = 0;
		int32 NumOutputChannels = 0;

		if (ConvolutionAlgorithm.IsValid())
		{
			NumInputChannels = ConvolutionAlgorithm->GetNumAudioInputs();
			NumOutputChannels = ConvolutionAlgorithm->GetNumAudioOutputs();
			AlgoBlockSize = ConvolutionAlgorithm->GetNumSamplesInBlock();
		}

		int32 ChunkSize = FMath::Max(ExpectedNumFramesPerCallback, AlgoBlockSize);
		ChunkSize = FMath::Max(128, ChunkSize);

		int32 InputCapacity = FMath::Max(8192, 2 * ChunkSize * NumInputChannels);
		int32 InputMaxInspect = FMath::Max(1024, AlgoBlockSize * NumInputChannels);

		InputBlockBuffer = MakeUnique<FAlignedBlockBuffer>(InputCapacity, InputMaxInspect);

		int32 OutputCapacity = FMath::Max(8192, 2 * ChunkSize * NumOutputChannels);
		int32 OutputMaxInspect = FMath::Max(1024, ExpectedNumFramesPerCallback * NumOutputChannels);

		OutputBlockBuffer = MakeUnique<FAlignedBlockBuffer>(OutputCapacity, OutputMaxInspect);
	}

	void FConvolutionReverb::InterleaveBuffer(AlignedFloatBuffer& OutBuffer, const TArray<AlignedFloatBuffer>& InputBuffers, const int32 NumChannels)
	{
		check(InputBuffers.Num() >= NumChannels);

		OutBuffer.Reset();

		if (InputBuffers.Num() < 1)
		{
			return;
		}

		int32 NumFrames = InputBuffers[0].Num();
		int32 NumOutputSamples = NumFrames * NumChannels;

		if (NumOutputSamples > 0)
		{
			OutBuffer.AddUninitialized(NumOutputSamples);
		}

		float* OutData = OutBuffer.GetData();
		for (int32 i = 0; i < NumChannels; ++i)
		{
			check(InputBuffers[i].Num() == NumFrames);

			const float* InData = InputBuffers[i].GetData();

			int32 OutPos = i;

			for (int32 j = 0; j < NumFrames; j++)
			{
				OutData[OutPos] = InData[j];
				OutPos += NumChannels;
			}
		}
	}

	void FConvolutionReverb::DeinterleaveBuffer(TArray<AlignedFloatBuffer>& OutputBuffers, TArrayView<const float> InputBuffer, const int32 NumChannels)
	{
		check(OutputBuffers.Num() >= NumChannels);

		int32 NumFrames = InputBuffer.Num();

		if (NumChannels > 0)
		{
			NumFrames /= NumChannels;
		}

		const float* InputData = InputBuffer.GetData();

		for (int32 i = 0; i < NumChannels; i++)
		{
			AlignedFloatBuffer& OutBuffer = OutputBuffers[i];

			check(OutBuffer.Num() == NumFrames);

			float* OutData = OutBuffer.GetData();

			int32 InputPos = i;

			for (int32 j = 0; j < NumFrames; j++)
			{
				OutData[j] = InputData[InputPos];
				InputPos += NumChannels;
			}
		}
	}
}
