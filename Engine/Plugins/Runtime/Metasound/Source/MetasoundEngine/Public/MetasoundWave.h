// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundDataReference.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "IAudioProxyInitializer.h"
#include "Sound/SoundWave.h"
#include "IAudioCodecRegistry.h"
#include "IAudioCodec.h"


namespace Metasound
{
	// Forward declare ReadRef
	class FWaveAsset;
	typedef TDataReadReference<FWaveAsset> FWaveAssetReadRef;


	// Metasound data type that holds onto a weak ptr. Mostly used as a placeholder until we have a proper proxy type.
	class METASOUNDENGINE_API FWaveAsset
	{
	public:
		// TODO: Implement using TSharedPtr<> to enable cheaper assignment when 
		// choosing between multiple instances of FWaveAsset.
		FSoundWaveProxyPtr SoundWaveProxy;

		FWaveAsset() = default;
		FWaveAsset(const FWaveAsset&) = delete;

		// Define move constructor and move assignment since
		// object cannot be copied to due TUniquePtr member.
		FWaveAsset(FWaveAsset&&) = default;
		FWaveAsset& operator=(FWaveAsset&&) = default;

		FWaveAsset& operator=(const FWaveAsset& Other)
		{
			SoundWaveProxy = Other.SoundWaveProxy;
			return *this;
		}

		FWaveAsset(const TUniquePtr<Audio::IProxyData>& InInitData)
		{
			SoundWaveProxy.Reset();

			if (InInitData.IsValid())
			{
				// should we be getting handed a SharedPtr here?
				SoundWaveProxy = MakeShared<FSoundWaveProxy, ESPMode::ThreadSafe>(InInitData->GetAs<FSoundWaveProxy>());
			}
		}

		bool IsSoundWaveValid() const
		{
			return SoundWaveProxy.IsValid();
		}
		
	};

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FWaveAsset, METASOUNDENGINE_API, FWaveAssetTypeInfo, FWaveAssetReadRef, FWaveAssetWriteRef)
}

namespace Audio
{
	// Forward declares
	class ICodecRegistry;
	struct IDecoderInput;
	struct IDecoderOutput;
	struct IDecoder;

	class FSimpleDecoderWrapper
	{
	public:
		struct InitParams
		{
			float OutputSampleRate;
			uint32 OutputBlockSizeInFrames;
			float MaxPitchShiftMagnitudeAllowedInOctaves = 4.f;
		};

		bool CanGenerateAudio() const
		{
			return !bDecoderIsDone && Input.IsValid() && Output.IsValid() && Decoder.IsValid();
		}

		bool Initialize(const InitParams& InInitParams, const FSoundWaveProxyPtr& InWave);

		// returns number of samples written.   
		uint32 GenerateAudio(float* OutputDest, int32 NumOutputFrames, float PitchShiftInCents = 0.f);

	private:
		// actual decoder objects
		TUniquePtr<Audio::IDecoder> Decoder;
		TUniquePtr<Audio::IDecoderOutput> Output;
		TSharedPtr<Audio::IDecoderInput, ESPMode::ThreadSafe> Input;

		// init helper for decoders
		bool InitializeDecodersInternal(const FSoundWaveProxyPtr& Wave);

		// SRC object
		Audio::FResampler Resampler;

		// buffers
		TArray<float> PreSrcBuffer;
		Audio::TCircularAudioBuffer<float> CircularDecoderOutputBuffer;

		// meta data:
		float InputSampleRate;
		float OutputSampleRate;
		float FsInToFsOutRatio;
		float MaxPitchShiftCents;
		float MaxPitchShiftRatio;

		uint32 NumChannels;
		uint32 DecodeBlockSizeInFrames;
		uint32 DecodeBlockSizeInSamples;
		
		// cached values
		float LastPitchShiftCents{ 0.f };
		int32 TotalNumFramesOutput{ 0 };
		int32 TotalNumFramesDecoded{ 0 };

		bool bDecoderIsDone{ true };

	}; // class FSimpleDecoderWrapper


} // namespace Audio
