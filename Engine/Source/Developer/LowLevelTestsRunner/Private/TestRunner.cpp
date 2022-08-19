// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestRunner.h"
#include "TestRunnerPrivate.h"

#include "HAL/PlatformOutputDevices.h"
#include "HAL/PlatformTLS.h"
#include "LowLevelTestModule.h"
#include "Modules/ModuleManager.h"
#include "Templates/SharedPointer.h"
#include "TestCommon/CoreUtilities.h"
#include "TestHarness.h"

#if WITH_APPLICATION_CORE
#include "HAL/PlatformApplicationMisc.h"
#endif

#include <catch2/catch_session.hpp>

#include <iostream>
#include <set>

namespace UE::LowLevelTests
{

static ITestRunner* GTestRunner;

ITestRunner* ITestRunner::Get()
{
	return GTestRunner;
}

ITestRunner::ITestRunner()
{
	check(!GTestRunner);
	GTestRunner = this;
}

ITestRunner::~ITestRunner()
{
	check(GTestRunner == this);
	GTestRunner = nullptr;
}

class FTestRunner final : public ITestRunner
{
public:
	bool HasGlobalSetup() const final { return bGlobalSetup; }
	bool HasLogOutput() const final { return bLogOutput || bDebugMode; }
	bool IsDebugMode() const final { return bDebugMode; }

	void SetGlobalSetup(bool bInGlobalSetup) { bGlobalSetup = bInGlobalSetup; }
	void SetLogOutput(bool bInLogOutput) { bLogOutput = bInLogOutput; }
	void SetDebugMode(bool bInDebugMode) { bDebugMode = bInDebugMode; }

private:
	bool bGlobalSetup = true;
	bool bLogOutput = false;
	bool bDebugMode = false;
};

static void LoadBaseTestModule(const FString& BaseModuleName)
{
	if (!BaseModuleName.IsEmpty())
	{
		FModuleManager::Get().LoadModule(*BaseModuleName);
	}
}

static void UnloadBaseTestModule(const FString& BaseModuleName)
{
	if (!BaseModuleName.IsEmpty())
	{
		FModuleManager::Get().UnloadModule(*BaseModuleName);
	}
}

static TArray<FName> GetGlobalModuleNames()
{
	TArray<FName> ModuleNames;
	FModuleManager::Get().FindModules(TEXT("*GlobalLowLevelTests"), ModuleNames);
	return ModuleNames;
}

static void GlobalModuleSetup()
{
	for (FName ModuleName : GetGlobalModuleNames())
	{
		if (ILowLevelTestsModule* Module = FModuleManager::LoadModulePtr<ILowLevelTestsModule>(ModuleName))
		{
			Module->GlobalSetup();
		}
	}
}

static void GlobalModuleTeardown()
{
	for (FName ModuleName : GetGlobalModuleNames())
	{
		if (ILowLevelTestsModule* Module = FModuleManager::GetModulePtr<ILowLevelTestsModule>(ModuleName))
		{
			Module->GlobalTeardown();
			if (Module->SupportsAutomaticShutdown())
			{
				Module->ShutdownModule();
			}
		}
	}
}

} // UE::LowLevelTests

int RunTests(int argc, const char* argv[])
{
	using namespace UE::LowLevelTests;

	// Set up the Game Thread.
	GGameThreadId = FPlatformTLS::GetCurrentThreadId();
	GIsGameThreadIdInitialized = true;

#ifdef SLEEP_ON_INIT
	// Sleep to allow sync with Gauntlet.
	FPlatformProcess::Sleep(5.0f);
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

	int CatchArgc = 0;
	TUniquePtr<const char* []> CatchArgv = MakeUnique<const char* []>(argc + 1);

	std::set<std::string> KnownLLTArgs;
	KnownLLTArgs.insert("--wait");
	KnownLLTArgs.insert("--no-wait");
	KnownLLTArgs.insert("--log");
	KnownLLTArgs.insert("--no-log");
	KnownLLTArgs.insert("--mt");
	KnownLLTArgs.insert("--no-mt");
	KnownLLTArgs.insert("--global-setup");
	KnownLLTArgs.insert("--no-global-setup");
	KnownLLTArgs.insert("--base-global-module");
	KnownLLTArgs.insert("--debug");

	// By default don't wait for input
	bool bWaitForInputToTerminate = false;

	FTestRunner TestRunner;
	bool bMultiThreaded = false;

	FString BaseModuleName;

	// Every argument that is not in the list of known low level test options and is considered to be a catch test runner argument.
	for (int i = 0; i < argc; ++i)
	{
		if (KnownLLTArgs.find(argv[i]) != KnownLLTArgs.end())
		{
			if (std::strcmp(argv[i], "--wait") == 0)
			{
				bWaitForInputToTerminate = true;
			}
			else if (std::strcmp(argv[i], "--no-wait") == 0)
			{
				bWaitForInputToTerminate = false;
			}
			if (std::strcmp(argv[i], "--log") == 0)
			{
				TestRunner.SetLogOutput(true);
			}
			else if (std::strcmp(argv[i], "--no-log") == 0)
			{
				TestRunner.SetLogOutput(false);
			}
			if (std::strcmp(argv[i], "--mt") == 0)
			{
				bMultiThreaded = true;
			}
			else if (std::strcmp(argv[i], "--no-mt") == 0)
			{
				bMultiThreaded = false;
			}
			if (std::strcmp(argv[i], "--global-setup") == 0)
			{
				TestRunner.SetGlobalSetup(true);
			}
			else if (std::strcmp(argv[i], "--no-global-setup") == 0)
			{
				TestRunner.SetGlobalSetup(false);
			}
			if (std::strcmp(argv[i], "--debug") == 0)
			{
				TestRunner.SetDebugMode(true);
			}
			// If we have --base-global-module parse proceeding argument as its option-value
			if (std::strcmp(argv[i], "--base-global-module") == 0 && i + 1 < argc)
			{
				BaseModuleName = argv[i + 1];
				++i;
			}
		}
		else
		{
			// Passing to catch2
			CatchArgv.Get()[CatchArgc++] = argv[i];
		}
	}

	// Break in the debugger on failed assertions when attached.
	CatchArgv.Get()[CatchArgc++] = "--break";

	if (TestRunner.HasGlobalSetup())
	{
		FCommandLine::Set(TEXT(""));

		// Finish setting up the Game Thread, which requires the command line.
		FPlatformProcess::SetThreadAffinityMask(FPlatformAffinity::GetMainGameMask());
		FPlatformProcess::SetupGameThread();

		// Always set up GError to handle FatalError, failed assertions, and crashes and other fatal errors.
	#if WITH_APPLICATION_CORE
		GError = FPlatformApplicationMisc::GetErrorOutputDevice();
	#else
		GError = FPlatformOutputDevices::GetError();
	#endif

		if (TestRunner.HasLogOutput())
		{
			// Set up GWarn to handle Error, Warning, Display; but only when log output is enabled.
		#if WITH_APPLICATION_CORE
			GWarn = FPlatformApplicationMisc::GetFeedbackContext();
		#else
			GWarn = FPlatformOutputDevices::GetFeedbackContext();
		#endif

			// Set up default output devices to handle Log, Verbose, VeryVerbose.
			FPlatformOutputDevices::SetupOutputDevices();
		}

		LoadBaseTestModule(BaseModuleName);

		GlobalModuleSetup();
	}

	int SessionResult = 0;
	{
		SessionResult = Catch::Session().run(CatchArgc, CatchArgv.Get());
		CatchArgv.Reset();
	}

	if (TestRunner.HasGlobalSetup())
	{
		GlobalModuleTeardown();

		UnloadBaseTestModule(BaseModuleName);

		CleanupPlatform();

		FCommandLine::Reset();
	}

#if PLATFORM_DESKTOP
	if (bWaitForInputToTerminate)
	{
		std::cout << "Press enter to exit..." << std::endl;
		std::cin.ignore();
	}
#endif

	return SessionResult;
}
