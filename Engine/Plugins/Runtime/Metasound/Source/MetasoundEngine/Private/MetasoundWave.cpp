// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundWave.h"
#include "MetasoundPrimitives.h"
#include "IAudioCodec.h"
#include "IAudioCodecRegistry.h"
#include "Sound/SoundWave.h"
#include "DecoderInputFactory.h"
#include "AudioDeviceManager.h"
#include "AudioDevice.h"


namespace Metasound
{	
	FWaveAsset::FDecoderInputPtr FWaveAsset::CreateDecoderInput() const
	{
		if (SoundWaveProxy.IsValid())
		{
			FName OldFormat = SoundWaveProxy->GetRuntimeFormat();

			TUniquePtr<Audio::IDecoderInput> Input = Audio::CreateBackCompatDecoderInput(OldFormat, *SoundWaveProxy);

			return FWaveAsset::FDecoderInputPtr(Input.Release());
		}

		return nullptr;
	}


	FWaveAsset::FDecoderTrio FWaveAsset::CreateDecoderTrio(const float OutputSampleRate, const int32 NumFramesPerBlock) const
	{
		using namespace Audio;

		if (!IsSoundWaveValid())
		{
			return { };
		}

		FWaveAsset::FDecoderInputPtr Input;
		if (!(Input = FWaveAsset::CreateDecoderInput()))
		{
			return { };
		}

		ICodecRegistry::FCodecPtr Codec = ICodecRegistry::Get().FindCodecByParsingInput(Input.Get());
		if (!Codec)
		{
			return { };
		}

		IDecoderOutput::FRequirements Reqs
		{
			Float32_Interleaved,
			NumFramesPerBlock,
			static_cast<int32>(OutputSampleRate)
		};

		TUniquePtr<IDecoderOutput> Output = IDecoderOutput::Create(Reqs);
		TUniquePtr<IDecoder> Decoder = Codec->CreateDecoder(Input.Get(), Output.Get());

		return
		{
			  MoveTemp(Input)
			, MoveTemp(Output)
			, MoveTemp(Decoder)
		};

	}

}
