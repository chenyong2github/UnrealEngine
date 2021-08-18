// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


namespace DisplayClusterPostprocessStrings
{
	namespace texture_share
	{
		static constexpr auto ShareName = TEXT("share");

		static constexpr auto DestinationViewports = TEXT("destination");
		static constexpr auto SourceShare          = TEXT("source");
		static constexpr auto ShareViewports       = TEXT("send");

		static constexpr auto BackbufferTextureId  = TEXT("BackBuffer");

		namespace debug
		{
			static constexpr auto DuplicateTexture = TEXT("duplicate_texture");
			static constexpr auto RepeatCopy       = TEXT("repeat_copy");
		}
	}

	namespace postprocess
	{
		static constexpr auto TextureShare  = TEXT("TextureShare");
		static constexpr auto D3D12CrossGPU = TEXT("D3D12CrossGPU");
	}
};
