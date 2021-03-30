// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundWave.h"
#include "MetasoundPrimitives.h"
#include "IAudioCodec.h"
#include "IAudioCodecRegistry.h"
#include "Sound/SoundWave.h"
#include "DecoderInputFactory.h"
#include "AudioDeviceManager.h"
#include "AudioDevice.h"
#include "DSP/ParamInterpolator.h"

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
			bIsSeekable = InWave->IsSeekableStreaming();

			return false;
		}

		// initialize input/output data
		InputSampleRate = InWave->GetSampleRate();
		OutputSampleRate = InInitParams.OutputSampleRate;
		FsOutToInRatio = (OutputSampleRate / InputSampleRate);
		MaxPitchShiftRatio = FMath::Pow(2.0f, InInitParams.MaxPitchShiftMagnitudeAllowedInOctaves);
		MaxPitchShiftCents = InInitParams.MaxPitchShiftMagnitudeAllowedInOctaves * 1200.0f;
		StartTimeSeconds = InInitParams.StartTimeSeconds;

		NumChannels = InWave->GetNumChannels();
		DecodeBlockSizeInFrames = 64;
		DecodeBlockSizeInSamples = DecodeBlockSizeInFrames * NumChannels;

		// set Circular Buffer capacity
		OutputCircularBuffer.SetCapacity(InInitParams.OutputBlockSizeInFrames * NumChannels * (1.0f + FsOutToInRatio * MaxPitchShiftRatio) * 2);

		TotalNumFramesOutput = 0;
		TotalNumFramesDecoded = 0;

		// try to initialize decoders
		bool bSuccessful = InitializeDecodersInternal(InWave);
		bDecoderIsDone = !bSuccessful;

		// initialize SRC object (will be re-initialized for pitch shifting)
		Resampler.Init(Audio::EResamplingMethod::Linear, FsOutToInRatio, NumChannels);
		PitchShifter.Reset(NumChannels, InInitParams.InitialPitchShiftSemitones);

		return bSuccessful;
	}


	uint32 FSimpleDecoderWrapper::GenerateAudio(float* OutputDest, int32 NumOutputFrames, float PitchShiftInCents, bool bIsLooping)
	{
		const uint32 NumOutputSamples = NumOutputFrames * NumChannels;

		if (OutputCircularBuffer.Num() < NumOutputSamples)
		{

			// (multiply by two just to be sure we can handle SRC output size)
			const int32 MaxNumResamplerOutputFramesPerBlock = FMath::CeilToInt(FsOutToInRatio * DecodeBlockSizeInFrames) * 2;
			const int32 MaxNumResamplerOutputSamplesPerBlock = MaxNumResamplerOutputFramesPerBlock * NumChannels;

			PreSrcBuffer.Reset(DecodeBlockSizeInSamples);
			PreSrcBuffer.AddUninitialized(DecodeBlockSizeInSamples);

			PostSrcBuffer.Reset(MaxNumResamplerOutputSamplesPerBlock);
			PostSrcBuffer.AddUninitialized(MaxNumResamplerOutputSamplesPerBlock);

			Resampler.SetSampleRateRatio(FsOutToInRatio);
			PitchShifter.UpdatePitchShift(FMath::Clamp(PitchShiftInCents, -MaxPitchShiftCents, MaxPitchShiftCents) / 100.0f);


			// perform SRC and push to circular buffer until we have enough frames for the output
			while (Decoder && !(bDecoderIsDone || bDecoderHasLooped) && (OutputCircularBuffer.Num() < NumOutputSamples))
			{
				// get more audio from the decoder
				Audio::IDecoderOutput::FPushedAudioDetails Details;
				const Audio::IDecoder::EDecodeResult  DecodeResult = Decoder->Decode(bIsLooping);
				const int32 NumFramesDecoded = Output->PopAudio(PreSrcBuffer, Details) / NumChannels;
				int32 NumResamplerOutputFrames = 0;
				int32 Error = Resampler.ProcessAudio(PreSrcBuffer.GetData(), NumFramesDecoded, bDecoderIsDone, PostSrcBuffer.GetData(), MaxNumResamplerOutputFramesPerBlock, NumResamplerOutputFrames);
				ensure(Error == 0);

				bDecoderIsDone = DecodeResult == Audio::IDecoder::EDecodeResult::Finished;
				bDecoderHasLooped = DecodeResult == Audio::IDecoder::EDecodeResult::Looped;

				// perform linear pitch shift into OutputCircularBuffer
				if (NumResamplerOutputFrames > 0)
				{
					const TArrayView<float> BufferToPitchShift(PostSrcBuffer.GetData(), NumResamplerOutputFrames * NumChannels);
					PitchShifter.ProcessAudio(BufferToPitchShift, OutputCircularBuffer);
				}
			}
		}

		if (OutputCircularBuffer.Num() >= NumOutputSamples)
		{
			OutputCircularBuffer.Pop(OutputDest, NumOutputSamples);
		}
		else if (ensure(bDecoderHasLooped || bDecoderIsDone))
		{
			bDecoderHasLooped = false;

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

	void FSimpleDecoderWrapper::SeekToTime(const float InSeconds)
	{
		if (Input.IsValid() && bIsSeekable)
		{
			Input->SeekToTime(InSeconds);
		}
		if (!bIsSeekable)
		{
			ensureMsgf(false, TEXT("Attempting to seek on a sound wave that is not set to Seekable"));
		}
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
		Input->SeekToTime(StartTimeSeconds);

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
} // namespace Audio
