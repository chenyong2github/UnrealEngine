// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundWave.h"
#include "MetasoundPrimitives.h"
#include "IAudioCodec.h"

namespace Metasound
{	
	// Simple for now, just wrap around the TArray<uint8>
	class FWaveDecoderInput : public Audio::FDecoderInputArrayView
	{
		FWaveReadRef WaveRef;	// These are not thread safe, and will need to be
	public:
		FWaveDecoderInput(const FWaveReadRef& InWaveRef)
			: Audio::FDecoderInputArrayView(MakeArrayView(InWaveRef->CompressedBytes.GetData(), InWaveRef->CompressedBytes.Num()), 0)
			, WaveRef(InWaveRef)
		{}
	};

	FWave::FDecoderInputPtr FWave::CreateDecoderInput(const FWaveReadRef& InWaveRef) 
	{
		return MakeShared<FWaveDecoderInput, ESPMode::ThreadSafe>(InWaveRef);
	}
}
