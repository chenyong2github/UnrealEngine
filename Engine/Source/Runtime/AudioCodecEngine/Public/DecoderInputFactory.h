// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/UniquePtr.h"

// Forward declares.
class USoundWave;

namespace Audio
{
	// Forward declares.
	struct IDecoderInput;

	// Just loose for now.
	AUDIOCODECENGINE_API TUniquePtr<IDecoderInput> CreateBackCompatDecoderInput(
		FName InOldFormatName,
		const USoundWave* InSoundWave );
}
