// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundDataReference.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "IAudioProxyInitializer.h"
#include "Sound/SoundWave.h"
#include "IAudioCodecRegistry.h"
#include "IAudioCodec.h"
#include "DSP/InterpolatedLinearPitchShifter.h"


namespace Metasound
{
	// Forward declare ReadRef
	class FWaveAsset;
	typedef TDataReadReference<FWaveAsset> FWaveAssetReadRef;


	// Metasound data type that holds onto a weak ptr. Mostly used as a placeholder until we have a proper proxy type.
	class METASOUNDENGINE_API FWaveAsset
	{
		FSoundWaveProxyPtr SoundWaveProxy;
	public:

		FWaveAsset() = default;
		FWaveAsset(const FWaveAsset&) = default;
		FWaveAsset& operator=(const FWaveAsset& Other) = default;

		FWaveAsset(const TUniquePtr<Audio::IProxyData>& InInitData);

		bool IsSoundWaveValid() const;

		const FSoundWaveProxyPtr& GetSoundWaveProxy() const
		{
			return SoundWaveProxy;
		}

		const FSoundWaveProxy* operator->() const
		{
			return SoundWaveProxy.Get();
		}

		FSoundWaveProxy* operator->()
		{
			return SoundWaveProxy.Get();
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
			uint32 OutputBlockSizeInFrames{ 512 };
			float OutputSampleRate { 44100.0f };
			float MaxPitchShiftMagnitudeAllowedInOctaves { 4.0f };
			float InitialPitchShiftSemitones { 0.0f };
			float StartTimeSeconds { 0.0f };
		};

		bool CanGenerateAudio() const
		{
			return !bDecoderIsDone && Input.IsValid() && Output.IsValid() && Decoder.IsValid();
		}

		/** Initialize the decoder to produce audio from the given wave.
		 *
		 * @param InInitParams - Initialization parameters for the decoder. 
		 * @param InWave - The wave proxy to decode.
		 * @param bRetainExistingSamples - If true, any existing decoded samples in the FSimpleDecoderWrapper
		 *                                 will be output during subsequent calls to GenerateAudio(...). If false,
		 *                                 any previously existing decoded samples will be discarded.
		 *
		 * @return True on success, false on failure. 
		 */
		bool Initialize(const InitParams& InInitParams, const FSoundWaveProxyPtr& InWave, bool bRetainExistingSamples=false);

		// returns number of samples written.   
		uint32 GenerateAudio(float* OutputDest, int32 NumOutputFrames, int32& OutNumFramesConsumed, float PitchShiftInCents = 0.0f, bool bIsLooping = false);

		//void SeekToTime(const float InSeconds);

	private:
		// actual decoder objects
		TUniquePtr<Audio::IDecoder> Decoder;
		TUniquePtr<Audio::IDecoderOutput> Output;
		TSharedPtr<Audio::IDecoderInput, ESPMode::ThreadSafe> Input;

		// init helper for decoders
		bool InitializeDecodersInternal(const FSoundWaveProxyPtr& Wave);

		// SRC objects
		Audio::FResampler Resampler;
		FLinearPitchShifter PitchShifter;

		// buffers
		TArray<float> PreSrcBuffer;
		TArray<float> PostSrcBuffer;
		Audio::TCircularAudioBuffer<float> OutputCircularBuffer;

		// meta data:
		float InputSampleRate{ 44100.f };
		float OutputSampleRate{ 44100.f };
		float FsOutToInRatio{ 1.f };
		float MaxPitchShiftCents{ 12.0f };
		float MaxPitchShiftRatio{ 2.0f };

		uint32 NumChannels{ 1 };
		uint32 NumFrames{ 0 };
		uint32 DecodeBlockSizeInFrames{ 64 };
		uint32 DecodeBlockSizeInSamples{ 64 };
		
		// cached values
		float StartTimeSeconds{ 0.0f };
		int32 TotalNumFramesOutput{ 0 };
		int32 TotalNumFramesDecoded{ 0 };

		bool bDecoderIsDone{ true };
		bool bIsSeekable{ false };
		bool bDecoderHasLooped{ false };

	}; // class FSimpleDecoderWrapper
} // namespace Audio
