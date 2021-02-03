// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


namespace TextureShareStrings
{
	namespace texture_name
	{
		static constexpr auto SceneColor = TEXT("SceneColor");

		static constexpr auto SceneDepth = TEXT("SceneDepth");
		static constexpr auto SmallDepthZ = TEXT("SmallDepthZ");

		static constexpr auto GBufferA = TEXT("GBufferA");
		static constexpr auto GBufferB = TEXT("GBufferB");
		static constexpr auto GBufferC = TEXT("GBufferC");
		static constexpr auto GBufferD = TEXT("GBufferD");
		static constexpr auto GBufferE = TEXT("GBufferE");
		static constexpr auto GBufferF = TEXT("GBufferF");

		static constexpr auto LightAttenuation = TEXT("LightAttenuation");
		static constexpr auto LightAccumulation = TEXT("LightAccumulation");
		static constexpr auto LightingChannels = TEXT("LightingChannels");

		static constexpr auto GBufferVelocity = TEXT("GBufferVelocity");
		static constexpr auto ShadingRate = TEXT("ShadingRate");

		static constexpr auto DirectionalOcclusion = TEXT("DirectionalOcclusion");

		static constexpr auto BackBuffer = TEXT("BackBuffer");
	}
};
