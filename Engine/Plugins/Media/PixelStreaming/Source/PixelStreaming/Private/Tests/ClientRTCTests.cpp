// Copyright Epic Games, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "ClientRTC.h"
#include "IPixelStreamingModule.h"
#include "PixelStreamingPrivate.h"

#if WITH_DEV_AUTOMATION_TESTS

// not using the macro here so we can define all this context and states
class FClientRTCMessageLatentCommand : public IAutomationLatentCommand
{
public:
	FClientRTCMessageLatentCommand(const FString& InStreamingSignallingUrl, const FString& InClientSignallingUrl)
		: StreamingSignallingUrl(InStreamingSignallingUrl)
		, ClientSignallingUrl(InClientSignallingUrl)
	{
	}

	virtual ~FClientRTCMessageLatentCommand()
	{
		ClientSession.Reset();
		IPixelStreamingModule& Module = IPixelStreamingModule::Get();
		Module.StopStreaming();
	}

	enum class EState
	{
		StreamerInit,
		StreamerWait,
		StreamerReady,

		ClientWait,
		ClientReady,

		MessageWait,

		Finished,
	};

	// return true when we're done
	bool Update() override
	{
		switch (State)
		{
			case EState::StreamerInit:
				return StreamerInit();
			case EState::StreamerWait:
				return Wait();
			case EState::StreamerReady:
				return StreamerReady();
			case EState::ClientWait:
				return Wait();
			case EState::ClientReady:
				return ClientReady();
			case EState::MessageWait:
				return Wait();
			default:
				UE_LOG(LogPixelStreaming, Error, TEXT("Unknown test state."));
				// fall through
			case EState::Finished:
				return true;
		}
	}

private:
	static constexpr uint32 TimeoutStreamerStartup = 5000;
	static constexpr uint32 TimeoutClientConnect = 5000;
	static constexpr uint32 TimeoutMessageWait = 5000;

	EState State = EState::StreamerInit;

	FString StreamingSignallingUrl;
	FString ClientSignallingUrl;

	TSharedPtr<UE::PixelStreaming::FClientRTC> ClientSession;

	FDateTime WaitStartTime;
	uint32 CurrentTimeout = 0;
	FString TimeoutMessage;

	bool StreamerInit()
	{
		IPixelStreamingModule& Module = IPixelStreamingModule::Get();
		Module.OnStreamingStarted().AddLambda([&](IPixelStreamingModule&) {
			State = EState::StreamerReady;
		});
		Module.StartStreaming(StreamingSignallingUrl);

		StartTimeout(TimeoutStreamerStartup, "Streaming startup timeout.");
		State = EState::StreamerWait;
		return false;
	}

	bool StreamerReady()
	{
		ClientSession = MakeShared<UE::PixelStreaming::FClientRTC>();
		ClientSession->OnConnected.AddLambda([](UE::PixelStreaming::FClientRTC& Session) {
		});
		ClientSession->OnDisconnected.AddLambda([](UE::PixelStreaming::FClientRTC& Session) {
		});
		ClientSession->OnDataChannelOpen.AddLambda([&](UE::PixelStreaming::FClientRTC& Session) {
			State = EState::ClientReady;
		});
		ClientSession->Connect(ClientSignallingUrl);

		StartTimeout(TimeoutClientConnect, "Client connect timeout.");
		State = EState::ClientWait;
		return false;
	}

	bool ClientReady()
	{
		const FString TestMessage(TEXT("Hello Streamer"));

		ClientSession->OnDataMessage.AddLambda([this, TestMessage](UE::PixelStreaming::FClientRTC&, uint8 Type, const FString& Descriptor) {
			if (Descriptor != TestMessage)
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Received data message did not match."));
			}
			State = EState::Finished;
		});

		if (!ClientSession->SendMessage(UE::PixelStreaming::Protocol::EToStreamerMsg::TestEcho, TestMessage))
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to send data message."));
			return true;
		}

		StartTimeout(TimeoutMessageWait, "Timeout waiting for data message.");
		State = EState::MessageWait;
		return false;
	}

	void StartTimeout(uint32 Timeout, const FString& Message)
	{
		WaitStartTime = FDateTime::Now();
		CurrentTimeout = Timeout;
		TimeoutMessage = Message;
	}

	bool Wait()
	{
		const FTimespan WaitDelta = FDateTime::Now() - WaitStartTime;
		if (WaitDelta.GetTotalMilliseconds() > CurrentTimeout)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("%s"), *TimeoutMessage);
			return true;
		}
		return false;
	}
};

bool ClientRTCInitTest()
{
	std::unique_ptr<UE::PixelStreaming::FClientRTC> client = std::make_unique<UE::PixelStreaming::FClientRTC>();
	const bool success = client->GetState() == UE::PixelStreaming::FClientRTC::EState::Disconnected;
	checkf(success, TEXT("ClientRTC initialized to an incorrect state."));
	return success;
}

bool ClientRTCMessageTest()
{
	const FString ServerSignallingUrl("ws://localhost:8888");
	const FString ClientSignallingUrl("ws://localhost");
	ADD_LATENT_AUTOMATION_COMMAND(FClientRTCMessageLatentCommand(ServerSignallingUrl, ClientSignallingUrl));

	return true;
}

// test name -> test function
TMap<FString, TFunction<bool()>> ClientRTCTestNames{
	{ "Init", ClientRTCInitTest },
	{ "Connect", ClientRTCMessageTest }
};

// The base test that will just contain all our actual tests
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FTestClientRTC, "PixelStreaming.ClientRTC", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
void FTestClientRTC::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	for (auto&& [name, func] : ClientRTCTestNames)
	{
		OutBeautifiedNames.Add(name);
		OutTestCommands.Add(name);
	}
}

bool FTestClientRTC::RunTest(const FString& Parameters)
{
	if (ClientRTCTestNames.Contains(Parameters))
	{
		return ClientRTCTestNames[Parameters]();
	}
	UE_LOG(LogPixelStreaming, Error, TEXT("Unknown test %S."), *Parameters);
	return false;
}

#endif // WITH_DEV_AUTOMATION_TESTS
