// Copyright Epic Games, Inc. All Rights Reserved.

#include "SwitchboardListener.h"
#include "SwitchboardListenerApp.h"

#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "RequiredProgramMainCPPInclude.h"

IMPLEMENT_APPLICATION(SwitchboardListener, "SwitchboardListener");
DEFINE_LOG_CATEGORY(LogSwitchboard);

namespace
{
	struct FCommandLineOptions
	{
		FIPv4Address Address;
		uint16 Port;
	};

	bool ParseCommandLine(int ArgC, TCHAR* ArgV[], FCommandLineOptions& OutOptions)
	{
		const FString CommandLine = FCommandLine::BuildFromArgV(nullptr, ArgC, ArgV, nullptr);
		TArray<FString> Tokens;
		TArray<FString> Switches;
		FCommandLine::Parse(*CommandLine, Tokens, Switches);
		TMap<FString, FString> SwitchPairs;
		for (int32 SwitchIdx = Switches.Num() - 1; SwitchIdx >= 0; --SwitchIdx)
		{
			FString& Switch = Switches[SwitchIdx];
			TArray<FString> SplitSwitch;
			if (2 == Switch.ParseIntoArray(SplitSwitch, TEXT("="), true))
			{
				SwitchPairs.Add(SplitSwitch[0], SplitSwitch[1].TrimQuotes());
				Switches.RemoveAt(SwitchIdx);
			}
		}

		if (!SwitchPairs.Contains(TEXT("ip")))
		{
			return false;
		}
		if (!SwitchPairs.Contains(TEXT("port")))
		{
			return false;
		}

		if (!FIPv4Address::Parse(SwitchPairs[TEXT("ip")], OutOptions.Address))
		{
			return false;
		}

		TCHAR* End = nullptr;
		OutOptions.Port = FCString::Strtoi(*SwitchPairs[TEXT("port")], &End, 10);

		return true;
	}
}

int32 InitEngine(const FString& InCommandLine)
{
	const int32 InitResult = GEngineLoop.PreInit((TEXT("SwitchboardListener %s"), *InCommandLine));
	if (InitResult != 0)
	{
		return InitResult;
	}

	ProcessNewlyLoadedUObjects();
	FModuleManager::Get().StartProcessingNewlyLoadedObjects();

	// Load internal Concert plugins in the pre-default phase
	IPluginManager::Get().LoadModulesForEnabledPlugins(ELoadingPhase::PreDefault);

	// Load Concert Sync plugins in default phase
	IPluginManager::Get().LoadModulesForEnabledPlugins(ELoadingPhase::Default);

	return 0;
}

bool InitSocketSystem()
{
	EModuleLoadResult LoadResult;
	FModuleManager::Get().LoadModuleWithFailureReason(TEXT("Sockets"), LoadResult);

	FIPv4Endpoint::Initialize();

	return LoadResult == EModuleLoadResult::Success;
}

void UninitEngine()
{
	RequestEngineExit(TEXT("SwitchboardListener Shutdown"));
}

bool RunSwitchboardListener(int ArgC, TCHAR* ArgV[])
{
	FCommandLineOptions Options;

	if (!ParseCommandLine(ArgC, ArgV, Options))
	{
		UE_LOG(LogSwitchboard, Warning, TEXT("No ip/port passed on command line!"));
		UE_LOG(LogSwitchboard, Warning, TEXT("Defaulting to: -ip=0.0.0.0 -port=2980"));
		Options.Address = FIPv4Address(0, 0, 0, 0);
		Options.Port = 2980;
	}

	FSwitchboardListener Listener({ Options.Address, Options.Port });

	if (!Listener.Init())
	{
		return false;
	}

	double LastTime = FPlatformTime::Seconds();
	const float IdealFrameTime = 1.0f / 30.0f;

	bool bListenerIsRunning = true;

	while (bListenerIsRunning)
	{
		const double CurrentTime = FPlatformTime::Seconds();
		const double DeltaTime = CurrentTime - LastTime;
		LastTime = CurrentTime;

		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

		// Pump & Tick objects
		FTicker::GetCoreTicker().Tick(DeltaTime);

		bListenerIsRunning = Listener.Tick();

		GFrameCounter++;
		FStats::AdvanceFrame(false);
		GLog->FlushThreadedLogs();

		// Run garbage collection for the UObjects for the rest of the frame or at least to 2 ms
		IncrementalPurgeGarbage(true, FMath::Max<float>(0.002f, IdealFrameTime - (FPlatformTime::Seconds() - CurrentTime)));

		// Throttle main thread main fps by sleeping if we still have time
		FPlatformProcess::Sleep(FMath::Max<float>(0.0f, IdealFrameTime - (FPlatformTime::Seconds() - CurrentTime)));
	}

	return true;
}

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	const int32 InitResult = InitEngine(TEXT(""));

	if (InitResult != 0)
	{
		UE_LOG(LogSwitchboard, Fatal, TEXT("Could not initialize engine, Error code: %d"), InitResult);
		return InitResult;
	}

	UE_LOG(LogSwitchboard, Display, TEXT("Successfully initialized engine."));

	if (!InitSocketSystem())
	{
		UE_LOG(LogSwitchboard, Fatal, TEXT("Could not initialize socket system!"));
		return 1;
	}

	UE_LOG(LogSwitchboard, Display, TEXT("Successfully initialized socket system."));

#if PLATFORM_WINDOWS
	ShowWindow(GetConsoleWindow(), SW_MINIMIZE);
#endif

	const bool bListenerResult = RunSwitchboardListener(ArgC, ArgV);
	UninitEngine();
	return bListenerResult ? 0 : 1;
}
