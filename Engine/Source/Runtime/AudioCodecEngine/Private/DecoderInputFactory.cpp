// Copyright Epic Games, Inc. All Rights Reserved.

#include "DecoderInputFactory.h"
#include "DecoderInputBackCompat.h"
#include "IAudioCodec.h"
#include "Sound/SoundWave.h"

namespace Audio
{		
	TUniquePtr<Audio::IDecoderInput> CreateBackCompatDecoderInput(
		FName InOldFormatName,
		const USoundWave* InSoundWave)
	{
		return MakeUnique<FBackCompatInput>(InOldFormatName, InSoundWave);
	}
}
