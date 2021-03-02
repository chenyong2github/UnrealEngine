// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundWave.h"
#include "MetasoundPrimitives.h"
#include "IAudioCodec.h"
#include "IAudioCodecRegistry.h"
#include "Sound/SoundWave.h"
#include "DecoderInputFactory.h"
#include "AudioDeviceManager.h"
#include "AudioDevice.h"

namespace Audio
{
	bool FSimpleDecoderWrapper::Initialize(const InitParams& InInitParams, const FSoundWaveProxyPtr& InWave)
	{
		// validate data
		if (!ensure(InWave.IsValid()) || !InWave->IsStreaming())
		{
			ensureAlwaysMsgf(InWave->IsStreaming(), TEXT("Metasounds does not support Force Inline (sound must be streaming)"));

			Input.Reset();
			Output.Reset();
			Decoder.Reset();
			bDecoderIsDone = false;

			return false;
		}

		// initialize input/output data
		InputSampleRate = InWave->GetSampleRate();
		OutputSampleRate = InInitParams.OutputSampleRate;
		FsOutToInRatio = (OutputSampleRate / InputSampleRate);
		MaxPitchShiftRatio = FMath::Pow(2.f, InInitParams.MaxPitchShiftMagnitudeAllowedInOctaves);
		MaxPitchShiftCents = InInitParams.MaxPitchShiftMagnitudeAllowedInOctaves * 1200.f;

		NumChannels = InWave->GetNumChannels();
		DecodeBlockSizeInFrames = 64;
		DecodeBlockSizeInSamples = DecodeBlockSizeInFrames * NumChannels;

		// set Circular Buffer capacity
		OutputCircularBuffer.SetCapacity(InInitParams.OutputBlockSizeInFrames * NumChannels * (1.f + FsOutToInRatio * MaxPitchShiftRatio) * 2);

		TotalNumFramesOutput = 0;
		TotalNumFramesDecoded = 0;

		// try to initialize decoders
		bool bSuccessful = InitializeDecodersInternal(InWave);
		bDecoderIsDone = !bSuccessful;

		// initialize SRC object (will be re-initialized for pitch shifting)
		Resampler.Init(Audio::EResamplingMethod::Linear, 1.f / FsOutToInRatio, NumChannels);

		return bSuccessful;
	}

	uint32 FSimpleDecoderWrapper::GenerateAudio(float* OutputDest, int32 NumOutputFrames, float PitchShiftInCents)
	{
		const uint32 NumOutputSamples = NumOutputFrames * NumChannels;

		if (OutputCircularBuffer.Num() < NumOutputSamples)
		{
			const float SampleRateRatio = GetSampleRateRatio(PitchShiftInCents);

			// (multiply by two just to be safe we can handle SRC output)
			const int32 MaxNumResamplerOutputFramesPerBlock = FMath::CeilToInt(SampleRateRatio * DecodeBlockSizeInFrames) * 2;
			const int32 MaxNumResamplerOutputSamplesPerBlock = MaxNumResamplerOutputFramesPerBlock * NumChannels;

			PreSrcBuffer.Reset(DecodeBlockSizeInSamples);
			PreSrcBuffer.AddUninitialized(DecodeBlockSizeInSamples);

			PostSrcBuffer.Reset(MaxNumResamplerOutputSamplesPerBlock);
			PostSrcBuffer.AddUninitialized(MaxNumResamplerOutputSamplesPerBlock);

			// combine pitch shift and SRC ratios
			Resampler.SetSampleRateRatio(SampleRateRatio);

			// perform SRC and push to circular buffer until we have enough frames for the output
			while (Decoder && !bDecoderIsDone && (OutputCircularBuffer.Num() < NumOutputSamples))
			{
				// get more audio from the decoder
				Audio::IDecoderOutput::FPushedAudioDetails Details;
				bDecoderIsDone = (Decoder->Decode() == Audio::IDecoder::EDecodeResult::Finished);
				const int32 NumFramesDecoded = Output->PopAudio(PreSrcBuffer, Details) / NumChannels;
				int32 NumResamplerOutputFrames = 0;
				int32 Error = Resampler.ProcessAudio(PreSrcBuffer.GetData(), NumFramesDecoded, bDecoderIsDone, PostSrcBuffer.GetData(), MaxNumResamplerOutputFramesPerBlock, NumResamplerOutputFrames);
				ensure(Error == 0);
				OutputCircularBuffer.Push(PostSrcBuffer.GetData(), NumResamplerOutputFrames * NumChannels);
			}
		}

		if (OutputCircularBuffer.Num() >= NumOutputSamples)
		{
			OutputCircularBuffer.Pop(OutputDest, NumOutputSamples);
		}
		else if (ensure(bDecoderIsDone))
		{
			const int32 NumSamplesToPop = OutputCircularBuffer.Num();
			const int32 NumSamplesRemaining = NumOutputSamples - NumSamplesToPop;
			OutputCircularBuffer.Pop(OutputDest, OutputCircularBuffer.Num());
			FMemory::Memzero(&OutputDest[NumSamplesToPop], sizeof(float) * NumSamplesRemaining);
			return NumSamplesToPop;
		}
		else
		{
			ensureMsgf(false, TEXT("Something went wrong with decoding."));
			bDecoderIsDone = true;
			return 0;
		}

		return NumOutputSamples; // update once we are aware of partial decode on last buffer
	}

	bool FSimpleDecoderWrapper::InitializeDecodersInternal(const FSoundWaveProxyPtr& Wave)
	{
		if (!ensure(Wave.IsValid()))
		{
			return false;
		}

		// Input:
		FName OldFormat = Wave->GetRuntimeFormat();
		Input = MakeShareable(Audio::CreateBackCompatDecoderInput(OldFormat, Wave).Release());

		if (!Input)
		{
			return false;
		}

		// acquire codec:
		ICodecRegistry::FCodecPtr Codec = ICodecRegistry::Get().FindCodecByParsingInput(Input.Get());
		if (!Codec)
		{
			return false;
		}

		// specify requirements
		IDecoderOutput::FRequirements Reqs
		{
			Float32_Interleaved,
			static_cast<int32>(DecodeBlockSizeInFrames),
			static_cast<int32>(OutputSampleRate)
		};

		// Output:
		Output = IDecoderOutput::Create(Reqs);

		// Decoder:
		Decoder = Codec->CreateDecoder(Input.Get(), Output.Get());

		// return true if all the components were successfully create
		return Input.IsValid() && Output.IsValid() && Decoder.IsValid();
	}

	float FSimpleDecoderWrapper::GetSampleRateRatio(float PitchShiftInCents) const
	{
		PitchShiftInCents = FMath::Clamp(PitchShiftInCents, -MaxPitchShiftCents, MaxPitchShiftCents);

		// combine pitch shift due to actual pitch shift and SRC
		return FsOutToInRatio / FMath::Pow(2.f, PitchShiftInCents / 1200.f);
	}

} // namespace Audio
