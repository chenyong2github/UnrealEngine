// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::RivermaxCore
{
	struct FRivermaxStreamOptions;
}

namespace UE::RivermaxCore::Private::Utils
{	
	/** Various constants used for stream initialization */
	static constexpr uint32 BytesPerHeader = 20;
	static constexpr uint32 BytesPerPacket = 1200;
	static constexpr uint32 FullHDHeight = 1080;
	static constexpr uint32 FullHDWidth = 1920;
	static constexpr uint32 MaxPayloadSize = 1280;
	static constexpr uint32 VideoTROModification = 0; // Todo : Investigate what TRO modification is

	/** Convert a set of streaming option to its SDP description. Currently only support video type. */
	void StreamOptionsToSDPDescription(const UE::RivermaxCore::FRivermaxStreamOptions& Options, FAnsiStringBuilderBase& OutSDPDescription);
}
