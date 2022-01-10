// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


namespace DisplayClusterPostprocessStrings
{
	namespace texture_share
	{
		static constexpr const TCHAR* ShareName = TEXT("share");

		static constexpr const TCHAR* DestinationViewports = TEXT("destination");
		static constexpr const TCHAR* SourceShare          = TEXT("source");
		static constexpr const TCHAR* ShareViewports       = TEXT("send");

		static constexpr const TCHAR* BackbufferTextureId  = TEXT("BackBuffer");

		namespace debug
		{
			static constexpr const TCHAR* DuplicateTexture = TEXT("duplicate_texture");
			static constexpr const TCHAR* RepeatCopy       = TEXT("repeat_copy");
		}
	}

	namespace postprocess
	{
		static constexpr const TCHAR* TextureShare  = TEXT("TextureShare");
		static constexpr const TCHAR* D3D12CrossGPU = TEXT("D3D12CrossGPU");
	}
};
