// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


namespace DisplayClusterStrings
{
	// Common strings
	namespace common
	{
		static constexpr auto PairSeparator     = TEXT(" ");
		static constexpr auto KeyValSeparator   = TEXT("=");
		static constexpr auto ArrayValSeparator = TEXT(",");
	}

	// Command line arguments
	namespace args
	{
		static constexpr auto Cluster    = TEXT("dc_cluster");
		static constexpr auto Node       = TEXT("dc_node");
		static constexpr auto Config     = TEXT("dc_cfg");
		static constexpr auto Camera     = TEXT("dc_camera");

		// Stereo device types (command line values)
		namespace dev
		{
			static constexpr auto QBS  = TEXT("quad_buffer_stereo");
			static constexpr auto TB   = TEXT("dc_dev_top_bottom");
			static constexpr auto SbS  = TEXT("dc_dev_side_by_side");
			static constexpr auto Mono = TEXT("dc_dev_mono");
		}
	}

	// RHI names
	namespace rhi
	{
		static constexpr auto D3D11  = TEXT("D3D11");
		static constexpr auto D3D12  = TEXT("D3D12");
	}

	// Log strings
	namespace log
	{
		static constexpr auto Found     = TEXT("found");
		static constexpr auto NotFound  = TEXT("not found");
	}
};
