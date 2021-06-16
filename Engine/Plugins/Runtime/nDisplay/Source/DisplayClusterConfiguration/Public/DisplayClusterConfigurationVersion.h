// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

enum class EDisplayClusterConfigurationVersion : uint8
{
	Unknown,     // Unknown version or not a config file
	Version_CFG, // Old .cfg format (going to be deprecated)
	Version_426, // 4.26 JSON based config format
	Version_427, // 4.27 JSON based config format
};

namespace DisplayClusterConfiguration
{
	static constexpr auto GetCurrentConfigurationSchemeMarker()
	{
		return TEXT("4.27");
	}
}
