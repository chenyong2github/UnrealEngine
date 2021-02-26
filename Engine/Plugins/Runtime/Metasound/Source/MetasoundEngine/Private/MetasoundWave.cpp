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
		FsInToFsOutRatio = (InputSampleRate / OutputSampleRate);
		MaxPitchShiftRatio = FMath::Pow(2.f, InInitParams.MaxPitchShiftMagnitudeAllowedInOctaves);
		MaxPitchShiftCents = InInitParams.MaxPitchShiftMagnitudeAllowedInOctaves * 1200.f;

		NumChannels = InWave->GetNumChannels();
		DecodeBlockSizeInFrames = InInitParams.OutputBlockSizeInFrames;
		DecodeBlockSizeInSamples = DecodeBlockSizeInFrames * NumChannels;

		// set Circular Buffer capacity
		CircularDecoderOutputBuffer.SetCapacity(DecodeBlockSizeInSamples * (1.f + FsInToFsOutRatio * MaxPitchShiftRatio));

		TotalNumFramesOutput = 0;
		TotalNumFramesDecoded = 0;
		
		// try to initialize decoders
		bool bSuccessful = InitializeDecodersInternal(InWave);
		bDecoderIsDone = !bSuccessful;

		// initialize SRC object (will be re-initialized for pitch shifting)
		Resampler.Init(Audio::EResamplingMethod::Linear, 1.f / FsInToFsOutRatio, NumChannels);

		return bSuccessful;
	}

	uint32 FSimpleDecoderWrapper::GenerateAudio(float* OutputDest, int32 NumOutputFrames, float PitchShiftInCents)
	{
		PitchShiftInCents = FMath::Clamp(PitchShiftInCents, -MaxPitchShiftCents, MaxPitchShiftCents);
		const float PitchShiftRatio = FMath::Pow(2.f, PitchShiftInCents / 1200.f);
		const float TotalInSamplesOutSamplesRatio = PitchShiftRatio * FsInToFsOutRatio;

		const uint32 NumOutputSamples = NumOutputFrames * NumChannels;
		uint32 NumSamplesToDecode = NumChannels * FMath::CeilToInt(NumOutputFrames * TotalInSamplesOutSamplesRatio + 1.f);

		// zero the output
		FMemory::Memzero(OutputDest, NumOutputSamples * sizeof(float));

		// Decode audio until we have enough to satisfy the output (post SRC)
		while (!bDecoderIsDone && (CircularDecoderOutputBuffer.Num() < NumSamplesToDecode))
		{
			// get more audio from the decoder
			Audio::IDecoderOutput::FPushedAudioDetails Details;
			bDecoderIsDone = (Decoder->Decode() == Audio::IDecoder::EDecodeResult::Finished);

			PreSrcBuffer.Reset(DecodeBlockSizeInSamples);
			PreSrcBuffer.AddZeroed(DecodeBlockSizeInSamples);
			const int32 NumSamplesDecoded = Output->PopAudio(MakeArrayView(PreSrcBuffer.GetData(), DecodeBlockSizeInSamples), Details);

			// push that (interleaved) audio to the (interleaved) circular buffer
			int32 NumPushed = CircularDecoderOutputBuffer.Push(PreSrcBuffer.GetData(), NumSamplesDecoded);
			ensure(NumPushed == NumSamplesDecoded); // there will be a discontinuity in the output because our CircularDecoderOutputBuffer was not large enough!

			// for debugging
			TotalNumFramesDecoded += (NumPushed / NumChannels);
		}

		// now that we have enough audio decoded, pop off the circular buffer into an interleaved, pre-src temp buffer
		// (It is possible that CircularDecoderOutputBuffer.Num() < NumOutputSamples if the decoder is dry...)
		const uint32 NumSamplesToPop = FMath::Min(CircularDecoderOutputBuffer.Num(), NumSamplesToDecode);
		const uint32 NumPostSrcSamples = NumOutputFrames * NumChannels;

		PreSrcBuffer.Reset(NumSamplesToPop);
		PreSrcBuffer.AddZeroed(NumSamplesToPop);

		// pop into pre-src buffer
		uint32 NumSamplesPopped = CircularDecoderOutputBuffer.Pop(PreSrcBuffer.GetData(), NumSamplesToPop);
		ensure(NumSamplesPopped == NumSamplesToPop); // Circular buffer is too small! there will be discontinuities in the output

		// holds result of sample rate conversion
		float* PostSrcBufferPtr = PreSrcBuffer.GetData(); // assume no SRC

		// perform SRC 
		if (!FMath::IsNearlyEqual(LastPitchShiftCents, PitchShiftInCents))
		{
			Resampler.Init(Audio::EResamplingMethod::Linear, 1.f / TotalInSamplesOutSamplesRatio, NumChannels);
			LastPitchShiftCents = PitchShiftInCents;
		}

		int32 NumFramesConverted;
		int32 Error = Resampler.ProcessAudio(PreSrcBuffer.GetData(), NumSamplesToPop / NumChannels, false, OutputDest, NumOutputFrames, NumFramesConverted);
		ensure(!Error);

		if (Error)
		{
			bDecoderIsDone = false;
			Input.Reset();
			Output.Reset();
			Decoder.Reset();
		}


		if (NumFramesConverted < (int32)NumOutputFrames)
		{
			bDecoderIsDone = true;
		}
		
		TotalNumFramesOutput += NumFramesConverted;
		return NumFramesConverted;
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
		
		// aquire codec:
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

		// return true if all the components were succesfully create
		return Input.IsValid() && Output.IsValid() && Decoder.IsValid();
	}

} // namesapce Audio
