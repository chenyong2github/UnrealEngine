// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundDataReference.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "IAudioProxyInitializer.h"
#include "Sound/SoundWave.h"
#include "IAudioCodecRegistry.h"
#include "IAudioCodec.h"

// Forward declares
namespace Audio
{	
	struct IDecoderInput;
}

class USoundWave;

namespace Audio
{
	class ICodecRegistry;
	struct IDecoderOutput;
	struct IDecoder;
}

namespace Metasound
{
	// Forward declare ReadRef
	class FWaveAsset;
	typedef TDataReadReference<FWaveAsset> FWaveAssetReadRef;


	// Metasound data type that holds onto a weak ptr. Mostly used as a placeholder until we have a proper proxy type.
	class METASOUNDENGINE_API FWaveAsset
	{
	public:
		using FDecoderInputPtr = TSharedPtr<Audio::IDecoderInput, ESPMode::ThreadSafe>;

		struct FDecoderTrio
		{
			FDecoderInputPtr Input;
			TUniquePtr<Audio::IDecoderOutput> Output;
			TUniquePtr<Audio::IDecoder> Decoder;
		};

		TUniquePtr<FSoundWaveProxy> SoundWaveProxy;

		FWaveAsset() = default;

		FWaveAsset& operator=(const FWaveAsset& Other)
		{
			SoundWaveProxy = MakeUnique<FSoundWaveProxy>(*Other.SoundWaveProxy);
			return *this;
		}

		FWaveAsset(const TUniquePtr<Audio::IProxyData>& InInitData)
		{
			SoundWaveProxy.Reset();

			if (InInitData.IsValid())
			{
				SoundWaveProxy = MakeUnique<FSoundWaveProxy>(InInitData->GetAs<FSoundWaveProxy>());
			}
		}

		FDecoderInputPtr CreateDecoderInput() const;


		FDecoderTrio CreateDecoderTrio(const float OutputSampleRate, const int32 NumFramesPerBlock) const;

		bool IsSoundWaveValid() const
		{
			return SoundWaveProxy.IsValid();
		}
		
	};

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FWaveAsset, METASOUNDENGINE_API, FWaveAssetTypeInfo, FWaveAssetReadRef, FWaveAssetWriteRef)
}
