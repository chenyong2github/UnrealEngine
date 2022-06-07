// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/**
 * Common test harness include file. Use this with tests that can be run as both low level tests or functional tests.
 * The two types of frameworks are as follows:
 * - Low level tests using Catch2, generates a monolithic test application, built separate
 * - UE legacy test automation framework, tests are built into the target executable
 */

#if WITH_LOW_LEVEL_TESTS
#include "LowLevelTestsRunner/Public/TestHarness.h"
#elif WITH_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/LowLevelTestAdapter.h"
#endif
