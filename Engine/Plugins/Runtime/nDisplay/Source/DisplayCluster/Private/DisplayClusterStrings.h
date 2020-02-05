// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterBuildConfig.h"
#include "DisplayClusterUtils/DisplayClusterCommonStrings.h"


namespace DisplayClusterStrings
{
	namespace misc
	{
#ifdef DISPLAY_CLUSTER_USE_DEBUG_STANDALONE_CONFIG
		static constexpr auto DbgStubConfig = TEXT("?");
		static constexpr auto DbgStubNodeId = TEXT("node_stub");
#endif
	}
};
