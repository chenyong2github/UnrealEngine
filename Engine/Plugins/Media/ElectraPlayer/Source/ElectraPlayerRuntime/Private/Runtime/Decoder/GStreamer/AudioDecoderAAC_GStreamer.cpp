// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#if PLATFORM_UNIX

#include "PlayerCore.h"
#include "StreamAccessUnitBuffer.h"
#include "Decoder/AudioDecoderAAC.h"
#include "Renderer/RendererBase.h"
#include "Player/PlayerSessionServices.h"
#include "Utilities/UtilsMPEGAudio.h"
#include "Utilities/StringHelpers.h"
#include "HAL/LowLevelMemTracker.h"
#include "ElectraPlayerPrivate.h"

namespace Electra
{

IAudioDecoderAAC::FSystemConfiguration::FSystemConfiguration()
{
}

IAudioDecoderAAC::FInstanceConfiguration::FInstanceConfiguration()
{
}

bool IAudioDecoderAAC::Startup(const IAudioDecoderAAC::FSystemConfiguration& InConfig)
{
	return false;
}

void IAudioDecoderAAC::Shutdown()
{
}

IAudioDecoderAAC* IAudioDecoderAAC::Create()
{
	return nullptr;
}

} // namespace Electra


#endif // PLATFORM_UNIX


