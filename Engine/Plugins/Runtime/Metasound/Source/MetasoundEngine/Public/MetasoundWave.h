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
	class FWaveAsset;
	typedef TDataReadReference<FWaveAsset> FWaveAssetReadRef;

	// Metasound data type that holds onto a weak ptr. Mostly used as a placeholder until we have a proper proxy type.
	class METASOUNDENGINE_API FWaveAsset
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

		using FDecoderInputPtr = TSharedPtr<Audio::IDecoderInput,ESPMode::ThreadSafe>;
		static FDecoderInputPtr CreateDecoderInput(
			const FWaveAssetReadRef& InWaveRef);
	};

	DECLARE_METASOUND_DATA_REFERENCE_TYPES(FWaveAsset, METASOUNDENGINE_API, FWaveAssetTypeInfo, FWaveAssetReadRef, FWaveAssetWriteRef)
}
