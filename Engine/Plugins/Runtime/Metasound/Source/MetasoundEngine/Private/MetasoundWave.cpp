// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundWave.h"

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "ContentStreaming.h"
#include "DecoderInputFactory.h"
#include "DSP/ParamInterpolator.h"
#include "IAudioCodec.h"
#include "IAudioCodecRegistry.h"
#include "MetasoundPrimitives.h"
#include "Sound/SoundWave.h"

namespace Metasound
{

	FWaveAsset::FWaveAsset(const TUniquePtr<Audio::IProxyData>& InInitData)
	{
		if (InInitData.IsValid())
		{
			if (InInitData->CheckTypeCast<FSoundWaveProxy>())
			{
				// should we be getting handed a SharedPtr here?
				SoundWaveProxy = MakeShared<FSoundWaveProxy, ESPMode::ThreadSafe>(InInitData->GetAs<FSoundWaveProxy>());

				if (SoundWaveProxy.IsValid())
				{
					// TODO HACK: Prime the sound for playback.
					//
					// Preferably playback latency would be controlled externally.
					// With the current decoder and waveplayer implementation, the 
					// wave player does not know whether samples were actually decoded
					// or if the decoder is still waiting on the stream cache. Generally
					// this is not an issue except for looping. Looping requires counting
					// of decoded samples to get exact loop points. When the decoder 
					// returns zeroed audio (because the stream cache has not loaded
					// the requested chunk) the sample counting gets off. Currently
					// there is not route to expose that information to the wave 
					// player to correct the sample counting logic. 
					//
					// In hopes of mitigating the issue, the stream cache
					// is primed here in the hopes that the chunk is ready by the
					// time that the decoder attempts to decode audio.
					IStreamingManager::Get().GetAudioStreamingManager().RequestChunk(SoundWaveProxy, 1, [](EAudioChunkLoadResult) {});
				}
			}
		}
	}

	bool FWaveAsset::IsSoundWaveValid() const
	{
		return SoundWaveProxy.IsValid();
	}
}

namespace Audio
{
	FSimpleDecoderWrapper::FSimpleDecoderWrapper(const InitParams& InInitParams)
	: OutputSampleRate(InInitParams.OutputSampleRate)
	, MaxPitchShiftCents(InInitParams.MaxPitchShiftMagnitudeAllowedInOctaves * 1200.0f)
	, MaxPitchShiftRatio(FMath::Pow(2.0f, InInitParams.MaxPitchShiftMagnitudeAllowedInOctaves))
	, DecodeBlockSizeInFrames(64)
	, OutputBlockSizeInFrames(InInitParams.OutputBlockSizeInFrames)
	{
		check(OutputBlockSizeInFrames > 0);
	}

	bool FSimpleDecoderWrapper::SetWave(const FSoundWaveProxyPtr& InWave, float InStartTimeSeconds, float InInitialPitchShiftSemitones)
	{
		Wave = InWave;

		bool bSuccessful = true;

		if (Wave.IsValid())
		{
			// validate data
			if (!ensureAlwaysMsgf(Wave->IsStreaming(), TEXT("Metasounds does not support Force Inline (sound must be streaming)")))
			{
				Wave.Reset();
				Input.Reset();
				Output.Reset();
				Decoder.Reset();
				bDecoderIsDone = false;

				return false;
			}

			// initialize input/output data
			InputSampleRate = Wave->GetSampleRate();
			FsOutToInRatio = (OutputSampleRate / InputSampleRate);

			NumChannels = Wave->GetNumChannels();
			DecodeBlockSizeInSamples = DecodeBlockSizeInFrames * NumChannels;

			// set Circular Buffer capacity
			int32 Capacity = FMath::Max(1, static_cast<int32>(OutputBlockSizeInFrames * NumChannels * (1.0f + FsOutToInRatio * MaxPitchShiftRatio) * 2));
			OutputCircularBuffer.Reserve(Capacity, true /* bRetainExistingSamples */);


			// try to initialize decoders
			bSuccessful = InitializeDecodersInternal(Wave, InStartTimeSeconds);
			bDecoderIsDone = !bSuccessful;

			Resampler.Init(Audio::EResamplingMethod::Linear, FsOutToInRatio, NumChannels);
			PitchShifter.Reset(NumChannels, InInitialPitchShiftSemitones);
		}

		return bSuccessful;
	}

	bool FSimpleDecoderWrapper::SeekToTime(const float InSeconds)
	{
		if (Wave.IsValid())
		{
			// try to initialize decoders
			bool bSuccessful = InitializeDecodersInternal(Wave, InSeconds);
			bDecoderIsDone = !bSuccessful;

			return bSuccessful;
		}

		return false;
	}
	

	uint32 FSimpleDecoderWrapper::GenerateAudio(float* OutputDest, int32 NumOutputFrames, int32& OutNumFramesConsumed, float PitchShiftInCents, bool bIsLooping)
	{
		const uint32 NumOutputSamples = NumOutputFrames * NumChannels;

		OutNumFramesConsumed = 0;

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
				OutNumFramesConsumed += NumFramesDecoded;
				int32 NumResamplerOutputFrames = 0;
				int32 Error = Resampler.ProcessAudio(PreSrcBuffer.GetData(), NumFramesDecoded, bDecoderIsDone, PostSrcBuffer.GetData(), MaxNumResamplerOutputFramesPerBlock, NumResamplerOutputFrames);
				ensure(Error == 0);

				bDecoderIsDone = DecodeResult == Audio::IDecoder::EDecodeResult::Finished;
				bDecoderHasLooped = DecodeResult == Audio::IDecoder::EDecodeResult::Looped;

				if (!PostSrcBuffer.Num() || !NumResamplerOutputFrames)
				{
					continue;
				}
				int32 OutputFrames = FMath::Min((int32)PostSrcBuffer.Num(), (int32)(NumResamplerOutputFrames * NumChannels));

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

	bool FSimpleDecoderWrapper::InitializeDecodersInternal(const FSoundWaveProxyPtr& InWave, float InStartTimeSeconds)
	{
		if (!ensure(InWave.IsValid()))
		{
			return false;
		}

		// Input:
		FName OldFormat = InWave->GetRuntimeFormat();
		// TODO: Why is this shared? Doesn't look like it needs to be shared. 
		Input = MakeShareable(Audio::CreateBackCompatDecoderInput(OldFormat, InWave).Release());
		Input->SeekToTime(InStartTimeSeconds);

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
