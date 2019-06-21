// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterBuildConfig.h"
#include "DisplayClusterUtils/DisplayClusterCommonStrings.h"


namespace DisplayClusterStrings
{
	// Command line arguments
	namespace args
	{
		static constexpr auto Cluster    = TEXT("dc_cluster");
		static constexpr auto Standalone = TEXT("dc_standalone");
		static constexpr auto Node       = TEXT("dc_node");
		static constexpr auto Config     = TEXT("dc_cfg");
		static constexpr auto Camera     = TEXT("dc_camera");

		// Stereo device types (command line values)
		namespace dev
		{
			static constexpr auto QBS   = TEXT("quad_buffer_stereo");
			static constexpr auto TB    = TEXT("dc_dev_top_bottom");
			static constexpr auto SbS   = TEXT("dc_dev_side_by_side");
			static constexpr auto Mono  = TEXT("dc_dev_mono");
		}
	}

	namespace misc
	{
#ifdef DISPLAY_CLUSTER_USE_DEBUG_STANDALONE_CONFIG
		static constexpr auto DbgStubConfig = TEXT("?");
		static constexpr auto DbgStubNodeId = TEXT("node_stub");
#endif
	}
};
