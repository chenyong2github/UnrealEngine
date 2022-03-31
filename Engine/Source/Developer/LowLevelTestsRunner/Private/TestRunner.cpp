// Copyright Epic Games, Inc. All Rights Reserved.

#define CATCH_CONFIG_RUNNER

#include "TestRunner.h"
#include "TestHarness.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformTLS.h"

#include "LowLevelTestModule.h"
#include "TestCommon/CoreUtilities.h"
#include "TestCommon/CoreUObjectUtilities.h"

#include <set>

namespace Catch
{
	// Test run interceptor
	struct TestRunListener : public TestEventListenerBase {
		using TestEventListenerBase::TestEventListenerBase; // inherit constructor
	private:
		std::ostream& catchOut = getCurrentContext().getConfig()->stream();
	public:
		void testCaseStarting(Catch::TestCaseInfo  const& TestInfo) override {
			if (bGDebug)
			{
				catchOut << TestInfo.lineInfo.file << ":" << TestInfo.lineInfo.line << " with tags " << TestInfo.tagsAsString() << " \n";
			}
		}

		void testCaseEnded(Catch::TestCaseStats const& testCaseStats) override {
			if (testCaseStats.totals.testCases.failed > 0)
			{
				catchOut << "* Error: Test case \"" << testCaseStats.testInfo.name << "\" failed \n";
			}
		}

		bool assertionEnded(Catch::AssertionStats const& assertionStats) override {
			if (!assertionStats.assertionResult.succeeded()) {
				catchOut << "* Error: Assertion \"" << assertionStats.assertionResult.getExpression() << "\" failed at " << assertionStats.assertionResult.getSourceInfo().file << ": " << assertionStats.assertionResult.getSourceInfo().line << "\n";
			}
			// true == clear message buffer
			return true;
		}
	};

	CATCH_REGISTER_LISTENER(TestRunListener);
}

void LoadBaseTestModule(FString BaseModuleName)
{
	if (!BaseModuleName.IsEmpty())
	{
		FModuleManager::Get().LoadModule(*BaseModuleName);
	}
}

void UnloadBaseTestModule(FString BaseModuleName)
{
	if (!BaseModuleName.IsEmpty())
	{
		FModuleManager::Get().UnloadModule(*BaseModuleName);
	}
}

void GlobalModuleSetup()
{
	// Search for all low level test modules
	TArray<FName> ModuleNames;
	FModuleManager::Get().FindModules(TEXT("*GlobalLowLevelTests"), ModuleNames);

	if (ModuleNames.Num() <= 0)
	{
		return;
	}

	for (FName ModuleName : ModuleNames)
	{
		ILowLevelTestsModule* Module = FModuleManager::LoadModulePtr<ILowLevelTestsModule>(ModuleName);
		if (Module != nullptr)
		{
			Module->GlobalSetup();
		}
	}
}

void GlobalModuleTeardown()
{
	// Search for all low level test modules
	TArray<FName> ModuleNames;
	FModuleManager::Get().FindModules(TEXT("*GlobalLowLevelTests"), ModuleNames);

	if (ModuleNames.Num() <= 0)
	{
		return;
	}

	for (FName ModuleName : ModuleNames)
	{
		ILowLevelTestsModule* Module = FModuleManager::GetModulePtr<ILowLevelTestsModule>(ModuleName);
		if (Module != nullptr)
		{
			Module->GlobalTeardown();
			if (Module->SupportsAutomaticShutdown())
			{
				Module->ShutdownModule();
			}
		}
	}
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

	int CatchArgc = 0;
	TUniquePtr<const char* []> CatchArgv = MakeUnique<const char* []>(argc);

	std::set<std::string> KnownLLTArgs;
	KnownLLTArgs.insert("--wait");
	KnownLLTArgs.insert("--no-wait");
	KnownLLTArgs.insert("--log");
	KnownLLTArgs.insert("--no-log");
	KnownLLTArgs.insert("--mt");
	KnownLLTArgs.insert("--no-mt");
	KnownLLTArgs.insert("--debug");
	KnownLLTArgs.insert("--base-global-module");

	// By default don't wait for input
	bool bWaitForInputToTerminate = false;

	FString LoadModuleName = TEXT("");

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
				bGAllowLogging = true;
			}
			if (std::strcmp(argv[i], "--no-log") == 0)
			{
				bGAllowLogging = false;
			}
			if (std::strcmp(argv[i], "--mt") == 0)
			{
				bGMultithreaded = true;
			}
			if (std::strcmp(argv[i], "--no-mt") == 0)
			{
				bGMultithreaded = false;
			}
			if (std::strcmp(argv[i], "--debug") == 0)
			{
				bGDebug = true;
			}
			// If we have --base-global-module parse proceeding argument as its option-value
			if (std::strcmp(argv[i], "--base-global-module") == 0 &&
				i + 1 < argc)
			{
				LoadModuleName = argv[i + 1];
				++i;
			}
		}
		else
		{
			// Passing to catch2
			CatchArgv.Get()[CatchArgc++] = argv[i];
		}
	}

	// Global command line initialization
	InitCommandLine(bGAllowLogging);

	LoadBaseTestModule(LoadModuleName);
	GlobalModuleSetup();

	int SessionResult = 0;
	{
		TGuardValue<bool> CatchRunning(bCatchIsRunning, true);
		SessionResult = Catch::Session().run(CatchArgc, CatchArgv.Get());
		CatchArgv.Reset();
	}

	CleanupCoreUObject();
	CleanupTaskGraphAndDependencies();

	GlobalModuleTeardown();
	UnloadBaseTestModule(LoadModuleName);

	// Required for platform cleanup, program will crash on exit otherwise
	CleanupPlatform();

	// Command line cleanup
	CleanupCommandLine();

#if PLATFORM_DESKTOP
	if (bWaitForInputToTerminate)
	{
		std::cout << "Press enter to exit..." << std::endl;
		std::cin.ignore();
	}
#endif

	return SessionResult;
};