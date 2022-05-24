// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "PixelStreamingServers.h"
#include "IPixelStreamingModule.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace UE::PixelStreaming::Servers
{

	DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FWaitForServer, TSharedPtr<FServerBase>, Server);
	bool FWaitForServer::Update()
	{
		if (Server)
		{
			return Server->IsTimedOut() || Server->IsReady();
		}
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FCleanupServer, TSharedPtr<FServerBase>, Server);
	bool FCleanupServer::Update()
	{
		if (Server)
		{
			Server->Stop();
			return !FPaths::DirectoryExists(Server->GetPathOnDisk());
		}
		else
		{
			return true;
		}
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLaunchTransientCirrusTest, "PixelStreaming.LaunchTransientCirrus", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FLaunchTransientCirrusTest::RunTest(const FString& Parameters)
	{

		TSharedPtr<FServerBase> SignallingServer = MakeSignallingServer();
		FLaunchArgs LaunchArgs;
		LaunchArgs.bEphemeral = true;
		LaunchArgs.bPollUntilReady = false;

		SignallingServer->Launch(LaunchArgs);

		TestTrue("Transient cirrus server directory exists", FPaths::DirectoryExists(SignallingServer->GetPathOnDisk()));

		SignallingServer->Stop();

		TestTrue("Transient cirrus server directory should not exist", !FPaths::DirectoryExists(SignallingServer->GetPathOnDisk()));

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLaunchTransientCirrusWithParams, "PixelStreaming.LaunchTransientCirrusWithParams", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	bool FLaunchTransientCirrusWithParams::RunTest(const FString& Parameters)
	{

		TSharedPtr<FServerBase> SignallingServer = MakeSignallingServer();
		FLaunchArgs LaunchArgs;
		LaunchArgs.bEphemeral = true;
		LaunchArgs.bPollUntilReady = true;

		SignallingServer->OnReady.AddLambda([this](TMap<EEndpoint, FString> Endpoints) {
			TestTrue("Server was ready.", true);
		});

		SignallingServer->OnFailedToReady.AddLambda([this]() {
			TestTrue("Server was ready.", false);
		});

		SignallingServer->Launch(LaunchArgs);

		TestTrue("Transient cirrus server directory exists", FPaths::DirectoryExists(SignallingServer->GetPathOnDisk()));

		ADD_LATENT_AUTOMATION_COMMAND(FWaitForServer(SignallingServer));
		ADD_LATENT_AUTOMATION_COMMAND(FCleanupServer(SignallingServer));

		return true;
	}

} // namespace UE::PixelStreaming::Servers

#endif // WITH_DEV_AUTOMATION_TESTS