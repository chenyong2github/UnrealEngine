// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#if PLATFORM_UNIX

#include "PlayerCore.h"
#include "StreamAccessUnitBuffer.h"
#include "Decoder/VideoDecoderH264.h"
#include "Renderer/RendererBase.h"
#include "Player/PlayerSessionServices.h"
#include "Utilities/StringHelpers.h"
#include "HAL/LowLevelMemTracker.h"
#include "ElectraPlayerPrivate.h"

namespace Electra
{


bool IVideoDecoderH264::Startup(const IVideoDecoderH264::FSystemConfiguration& InConfig)
{
	return false;
}

void IVideoDecoderH264::Shutdown()
{
}

bool IVideoDecoderH264::GetStreamDecodeCapability(FStreamDecodeCapability& OutResult, const FStreamDecodeCapability& InStreamParameter)
{
	return false;
}

IVideoDecoderH264::FSystemConfiguration::FSystemConfiguration()
{
}

IVideoDecoderH264::FInstanceConfiguration::FInstanceConfiguration()
{
}

IVideoDecoderH264* IVideoDecoderH264::Create()
{
	return nullptr;
}


} // namespace Electra


#endif // PLATFORM_UNIX
