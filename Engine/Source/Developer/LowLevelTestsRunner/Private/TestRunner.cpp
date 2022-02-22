// Copyright Epic Games, Inc. All Rights Reserved.

#define CATCH_CONFIG_RUNNER

#include "TestRunner.h"
#include "TestHarness.h"
#include "HAL/PlatformTLS.h"

#include "CommonEngineInit.inl"

// TODO: Implement global initialization via flags
#if WITH_ENGINE || WITH_EDITOR || WITH_APPLICATION_CORE || WITH_COREUOBJECT
#define USE_GLOBAL_ENGINE_SETUP
#endif

// Test run interceptor
struct TestRunListener : public Catch::TestEventListenerBase {
	using TestEventListenerBase::TestEventListenerBase; // inherit constructor
public:
	void testCaseStarting(Catch::TestCaseInfo  const& TestInfo) override {
		if (bDebug)
		{
			std::cout << TestInfo.lineInfo.file << ":" << TestInfo.lineInfo.line << " with tags " << TestInfo.tagsAsString() << std::endl;
		}
	}
};

CATCH_REGISTER_LISTENER(TestRunListener);

void GlobalSetup()
{
	if (bGAllowLogging)
	{
		FCommandLine::Set(TEXT(""));
	}
	else
	{
		FCommandLine::Set(TEXT(R"(-LogCmds="global off")"));
		FLogSuppressionInterface::Get().ProcessConfigAndCommandLine();
	}
	InitThreadPool();
	InitAsyncQueues();
	InitTaskGraph();
	InitOutputDevices();
	InitRendering();
	InitDerivedDataCache();
	InitSlate();
	InitForWithEditorOnlyData();
	InitEditor();
	InitCoreUObject();
}

void GlobalTeardown()
{
	CleanupCoreUObject();
	CleanupThreadPool();
	CleanupTaskGraph();
	CleanupPlatform();
}


int RunTests(int argc, const char* argv[])
{
	// remember thread id of the main thread
	GGameThreadId = FPlatformTLS::GetCurrentThreadId();
	GIsGameThreadIdInitialized = true;

#ifdef SLEEP_ON_INIT
	// Sleep to allow sync with Gauntlet
	std::this_thread::sleep_for(std::chrono::milliseconds(5000));
#endif

	//Read command-line from file (if any). Some platforms do this earlier.
#ifndef PLATFORM_SKIP_ADDITIONAL_ARGS
	int ArgsOverrideNum = 0;
	const char** ArgsOverride = ReadAndAppendAdditionalArgs(GetProcessExecutablePath(), &ArgsOverrideNum, argv, argc);
	if (ArgsOverride != nullptr && ArgsOverrideNum > 1)
	{
		argc = ArgsOverrideNum;
		argv = ArgsOverride;
	}
#endif

	int CatchArgc;
	TUniquePtr<const char* []> CatchArgv = MakeUnique<const char* []>(argc);

	// Everything past a "--" argument, if present, is not sent to the catch test runner.
	for (CatchArgc = 0; CatchArgc < argc; ++CatchArgc)
	{
		if (std::strcmp(argv[CatchArgc], "--") == 0)
		{
			break;
		}
		CatchArgv.Get()[CatchArgc] = argv[CatchArgc];
	}

	// By default don't wait for input
	bool bWaitForInputToTerminate = false;

	for (int i = CatchArgc; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--wait") == 0)
		{
			bWaitForInputToTerminate = true;
		}
		else if (std::strcmp(argv[i], "--no-wait") == 0)
		{
			bWaitForInputToTerminate = false;
		}
		if (std::strcmp(argv[i], "--no-log") == 0)
		{
			bGAllowLogging = false;
		}
		if (std::strcmp(argv[i], "--no-mt") == 0)
		{
			bMultithreaded = false;
		}
		if (std::strcmp(argv[i], "--debug") == 0)
		{
			bDebug = true;
		}
	}

// Global initialization
#ifdef USE_GLOBAL_ENGINE_SETUP
	GlobalSetup();
#endif

	int SessionResult = 0;
	{
		TGuardValue<bool> CatchRunning(bCatchIsRunning, true);
		SessionResult = Catch::Session().run(CatchArgc, CatchArgv.Get());
		CatchArgv.Reset();
	}

// Global cleanup
#if defined(USE_GLOBAL_ENGINE_SETUP)
	GlobalTeardown();
#endif

#if PLATFORM_DESKTOP
	if (bWaitForInputToTerminate)
	{
		std::cout << "Press enter to exit..." << std::endl;
		std::cin.ignore();
	}
#endif

	return SessionResult;
};