// Copyright Epic Games, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "ClientRTC.h"
#include "PixelStreamingModule.h"
#include "PixelStreamingPrivate.h"

using namespace UE::PixelStreaming;

bool bStreamingStarted = false;
bool bClientConnected = false;
bool bMessageReceived = false;
bool bMessageMatched = false;
TSharedPtr<FClientRTC> ClientSession;
const FString TestMessage(TEXT("Hello Streamer"));

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FSetupStreamingLatentCommand, const FString, SignallingServerUrl);
bool FSetupStreamingLatentCommand::Update()
{
	IPixelStreamingModule& Module = IPixelStreamingModule::Get();
	Module.OnStreamingStarted().AddLambda([](IPixelStreamingModule&) {
		bStreamingStarted = true;
	});
	Module.StartStreaming(SignallingServerUrl);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND(FWaitStreamingStartedLatentCommand);
bool FWaitStreamingStartedLatentCommand::Update()
{
	return bStreamingStarted;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FSetupClientLatentCommand, const FString, SignallingServerUrl);
bool FSetupClientLatentCommand::Update()
{
	ClientSession = MakeShared<FClientRTC>();
	ClientSession->OnConnected.AddLambda([](FClientRTC&) {
		bClientConnected = true;
	});
	ClientSession->OnDisconnected.AddLambda([](FClientRTC&) {
		bClientConnected = false;
	});
	ClientSession->Connect(SignallingServerUrl);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND(FWaitClientConnectedLatentCommand);
bool FWaitClientConnectedLatentCommand::Update()
{
	if (ClientSession->GetState() == FClientRTC::EState::Connecting)
	{
		return false;
	}
	else if (ClientSession->GetState() == FClientRTC::EState::ConnectedStreamer)
	{
		if (!bClientConnected)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Client connect event failed?"));
			return true;
		}
	}
	else if (ClientSession->GetState() == FClientRTC::EState::Disconnected)
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Client connect failed?"));
		return true;
	}
	return bClientConnected;
}

DEFINE_LATENT_AUTOMATION_COMMAND(FSendMessageLatentCommand);
bool FSendMessageLatentCommand::Update()
{
	ClientSession->OnDataMessage.AddLambda([](FClientRTC&, uint8 Type, const FString& Descriptor) {
		bMessageReceived = true;
		bMessageMatched = Descriptor == TestMessage;
	});
	ClientSession->SendMessage(Protocol::EToStreamerMsg::TestEcho, TestMessage);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND(FWaitMessageLatentCommand);
bool FWaitMessageLatentCommand::Update()
{
	if (bMessageReceived)
	{
		if (!bMessageMatched)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Message received but did not match."));
			return true;
		}
	}
	else if (ClientSession->GetState() == FClientRTC::EState::Disconnected)
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Client connection lost."));
		return true;
	}
	return bMessageReceived;
}

DEFINE_LATENT_AUTOMATION_COMMAND(FCleanupLatentCommand);
bool FCleanupLatentCommand::Update()
{
	ClientSession.Reset();
	IPixelStreamingModule::Get().StopStreaming();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClientConnectTest, "PixelStreaming.Client Connect", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FClientConnectTest::RunTest(FString const& Parameters)
{
	bStreamingStarted = false;
	bClientConnected = false;
	bMessageReceived = false;
	bMessageMatched = false;

	ADD_LATENT_AUTOMATION_COMMAND(FSetupStreamingLatentCommand("ws://localhost:8888"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitStreamingStartedLatentCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FSetupClientLatentCommand("ws://localhost"));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitClientConnectedLatentCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FSendMessageLatentCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FWaitMessageLatentCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FCleanupLatentCommand());
	return true;
}
