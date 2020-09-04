// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/DisplayClusterBuildConfig.h"
#include "Misc/DisplayClusterCommonStrings.h"


namespace DisplayClusterStrings
{
	namespace misc
	{
#ifdef DISPLAY_CLUSTER_USE_DEBUG_STANDALONE_CONFIG
		static constexpr auto DbgStubConfig = TEXT("?");
		static constexpr auto DbgStubNodeId = TEXT("node_stub");
#endif
	}

	namespace log
	{
		static constexpr auto NotFound      = TEXT("not found");
		static constexpr auto Found         = TEXT("found");
	}
};
