// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestRunner.h"
#include "TestHarness.h"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <catch2/catch_test_case_info.hpp>

#include "CoreGlobals.h"
#include "HAL/PlatformTLS.h"
#include "Logging/LogVerbosity.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Modules/ModuleManager.h"
#include "Templates/SharedPointer.h"

#include "LowLevelTestModule.h"
#include "TestCommon/CoreUtilities.h"

#include <set>
#include <iostream>

bool bCatchIsRunning = false;
bool bGAllowLogging = false;
bool bGMultithreaded = false;
bool bGDebug = false;
bool bGGlobalSetup = true;

namespace Catch {
	class TestRunListener : public EventListenerBase, public FOutputDevice
	{
		using EventListenerBase::EventListenerBase; // inherit constructor
	private:
		void testRunStarting(TestRunInfo const& testRunInfo) override {
			// Register this event listener as an output device to enable reporting of UE_LOG, ensure etc
			// Note: For UE_LOG(...) reporting the user must pass the "--log" command line argument, but this is not required for ensure reporting
			GLog->AddOutputDevice(this);
		}

		void testRunEnded(TestRunStats const& testRunStats) override {
			GLog->RemoveOutputDevice(this);
		}

		// FOutputDevice interface
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type LogVerbosity, const class FName& Category) override
		{
			// By default only log warnings/errors. If the user passes the command line argument "--debug" be more verbose
			ELogVerbosity::Type DesiredLogVerbosity = ELogVerbosity::Warning;
			if (bGDebug)
			{
				DesiredLogVerbosity = ELogVerbosity::Log; // Probably ELogVerbosity::Type::Verbose/VeryVerbose is too noisy
			}

			// TODO It might be nicer to increase the desired logging verbosity using the "-v high"/"--verbosity high"
			// Catch option and changing condition above to `getCurrentContext().getConfig()->verbosity() == Verbosity::High`
			// I tried this but unfortunately it didn't work, passing that option makes catch complain with the error message:
			// "Verbosity level not supported by this reporter"

			// TODO Perhaps we should check IsInGameThread() or do something to make this threadsafe...?
			// UPDATE: CanBeUsedOnMultipleThreads returns true
			if (LogVerbosity <= DesiredLogVerbosity)
			{
				std::cout << *FText::FromName(Category).ToString() << "(" << ToString(LogVerbosity) << ")" << ": " << V << "\n";
			}
		}

		virtual bool CanBeUsedOnMultipleThreads() const
		{
			return true;
		}
		// End of FOutputDevice interface

		void testCaseStarting(TestCaseInfo  const& TestInfo) override {
			if (bGDebug)
			{
				std::cout << TestInfo.lineInfo.file << ":" << TestInfo.lineInfo.line << " with tags " << TestInfo.tagsAsString() << " \n";
			}
		}

		void testCaseEnded(TestCaseStats const& testCaseStats) override {
			if (testCaseStats.totals.testCases.failed > 0)
			{
				std::cout << "* Error: Test case \"" << testCaseStats.testInfo->name << "\" failed \n";
			}
		}

		void assertionEnded(AssertionStats const& assertionStats) override {
			if (!assertionStats.assertionResult.succeeded()) {
				std::cout << "* Error: Assertion \"" << assertionStats.assertionResult.getExpression() << "\" failed at " << assertionStats.assertionResult.getSourceInfo().file << ": " << assertionStats.assertionResult.getSourceInfo().line << "\n";
			}
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
	KnownLLTArgs.insert("--global-setup");
	KnownLLTArgs.insert("--no-global-setup");
	KnownLLTArgs.insert("--base-global-module");
	KnownLLTArgs.insert("--debug");

	// By default don't wait for input
	bool bWaitForInputToTerminate = false;

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
				bGAllowLogging = true;
			}
			else if (std::strcmp(argv[i], "--no-log") == 0)
			{
				bGAllowLogging = false;
			}
			if (std::strcmp(argv[i], "--mt") == 0)
			{
				bGMultithreaded = true;
			}
			else if (std::strcmp(argv[i], "--no-mt") == 0)
			{
				bGMultithreaded = false;
			}
			if (std::strcmp(argv[i], "--global-setup") == 0)
			{
				bGGlobalSetup = true;
			}
			else if (std::strcmp(argv[i], "--no-global-setup") == 0)
			{
				bGGlobalSetup = false;
			}
			if (std::strcmp(argv[i], "--debug") == 0)
			{
				bGDebug = true;
			}
			// If we have --base-global-module parse proceeding argument as its option-value
			if (std::strcmp(argv[i], "--base-global-module") == 0 &&
				i + 1 < argc)
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

	if (bGGlobalSetup)
	{	
		InitCommandLine(bGAllowLogging);

		LoadBaseTestModule(BaseModuleName);

		GlobalModuleSetup();
	}

	int SessionResult = 0;
	{
		TGuardValue<bool> CatchRunning(bCatchIsRunning, true);
		SessionResult = Catch::Session().run(CatchArgc, CatchArgv.Get());
		CatchArgv.Reset();
	}

	if (bGGlobalSetup)
	{
		GlobalModuleTeardown();

		UnloadBaseTestModule(BaseModuleName);

		CleanupPlatform();

		CleanupCommandLine();
	}

#if PLATFORM_DESKTOP
	if (bWaitForInputToTerminate)
	{
		std::cout << "Press enter to exit..." << std::endl;
		std::cin.ignore();
	}
#endif

	return SessionResult;
};