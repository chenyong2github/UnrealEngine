// Copyright Epic Games, Inc. All Rights Reserved.

#include "Logging/LogScopedVerbosityOverride.h"
#include "Modules/ModuleManager.h"
#include "NetworkPredictionTests.h"
#include "SlateGlobals.h"
#include "Styling/SlateWidgetStyleContainerBase.h"
#include "TestCommon/Initialization.h"

#include <catch2/catch_test_macros.hpp>

DEFINE_LOG_CATEGORY(LogNetworkPredictionTests);

GROUP_BEFORE_GLOBAL(Catch::DefaultGroup)
{
	{
		// Silence some warnings during initialization unrelated to the tests
		LOG_SCOPE_VERBOSITY_OVERRIDE(LogSlate, ELogVerbosity::Error);
		LOG_SCOPE_VERBOSITY_OVERRIDE(LogSlateStyle, ELogVerbosity::Error);
		LOG_SCOPE_VERBOSITY_OVERRIDE(LogUObjectGlobals, ELogVerbosity::Error);
		InitAll(true, true);
	}

	FModuleManager::Get().LoadModule(TEXT("NetworkPrediction"));
}

GROUP_AFTER_GLOBAL(Catch::DefaultGroup)
{
	CleanupAll();
}
