// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundDataReference.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "IAudioProxyInitializer.h"
#include "Sound/SoundWave.h"

// Forward declares
namespace Audio
{	
	struct IDecoderInput;
}

class USoundWave;

namespace Metasound
{
	// Forward declare ReadRef
	class FWave;
	typedef TDataReadReference<FWave> FWaveReadRef;
		
	class METASOUNDENGINE_API FWave
	{
		TArray<uint8> CompressedBytes;
		friend class FWaveDecoderInput;

	public:
		FWave() = default;

		// For testing only.
		FWave(const TArray<uint8>& InBytes);
		
		FWave(USoundWave* InWave);

		using FDecoderInputPtr = TSharedPtr<Audio::IDecoderInput,ESPMode::ThreadSafe>;

		// Factory function to create a Decoder input
		static FDecoderInputPtr CreateDecoderInput(
			const FWaveReadRef& InWaveRef);
	};
	// Metasound datatype that holds onto a weak ptr. Mostly used as a placeholder until we have a proper proxy type.
	class FWaveAsset
	{
	private:
		TWeakObjectPtr<USoundWave> SoundWave;
	public:

		FWaveAsset() = default;

		FWaveAsset(const Audio::IProxyData& InInitData)
		{
			const FSoundWaveProxy& SoundWaveProxy = InInitData.GetAs<FSoundWaveProxy>();
			SoundWave = SoundWaveProxy.SoundWavePtr;
		}

		USoundWave* GetSoundWave()
		{
			return SoundWave.Get();
		}

		const USoundWave* GetSoundWave() const
		{
			return SoundWave.Get();
		}
	};

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FWave, METASOUNDENGINE_API, FWaveTypeInfo, FWaveReadRef, FWaveWriteRef)
	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FWaveAsset, METASOUNDENGINE_API, FWaveAssetTypeInfo, FWaveAssetReadRef, FWaveAssetWriteRef)
}
