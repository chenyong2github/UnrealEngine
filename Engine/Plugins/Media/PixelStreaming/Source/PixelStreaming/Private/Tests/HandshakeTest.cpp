// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Streamer.h"
#include "IPixelStreamingSignallingConnectionObserver.h"
#include "PixelStreamingPrivate.h"
#include "WebSocketsModule.h"
#include "ToStringExtensions.h"
#include "PixelStreamingVideoInputBackBuffer.h"
#include "Utils.h"
#include "Settings.h"
#include "PixelStreamingServers.h"
#include "GenericPlatform/GenericPlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS

DECLARE_LOG_CATEGORY_EXTERN(PixelStreamingHandshakeLog, Log, All);
DEFINE_LOG_CATEGORY(PixelStreamingHandshakeLog);

namespace UE::PixelStreaming
{
	class FMockVideoSink : public rtc::VideoSinkInterface<webrtc::VideoFrame>
	{
	protected:
		virtual void OnFrame(const webrtc::VideoFrame& frame) override
		{
			return;
		}
	};

	class FMockPlayer : public IPixelStreamingSignallingConnectionObserver
	{
	public:
		FMockPlayer()
		{
			FPixelStreamingSignallingConnection::FWebSocketFactory WebSocketFactory = [](const FString& Url) { return FWebSocketsModule::Get().CreateWebSocket(Url, TEXT("")); };
			SignallingServerConnection = MakeUnique<FPixelStreamingSignallingConnection>(WebSocketFactory, *this, TEXT("FMockPlayer"));

			UE::PixelStreaming::DoOnGameThreadAndWait(MAX_uint32, []() {
				Settings::CVarPixelStreamingSuppressICECandidateErrors->Set(true, ECVF_SetByCode);
			});
		}

		virtual ~FMockPlayer()
		{
			Disconnect();

			UE::PixelStreaming::DoOnGameThread([]() {
				Settings::CVarPixelStreamingSuppressICECandidateErrors->Set(false, ECVF_SetByCode);
			});
		}

		enum class EMode
		{
			Unknown,
			AcceptOffers,
			CreateOffers,
		};

		void SetMode(EMode InMode) { Mode = InMode; }

		void Connect(const FString& Url)
		{
			SignallingServerConnection->Connect(Url);
		}

		void Disconnect()
		{
			SignallingServerConnection->Disconnect();
		}

		bool IsSignallingConnected()
		{
			return SignallingServerConnection->IsConnected();
		}

		virtual void OnSignallingConnected() override
		{
		}

		virtual void OnSignallingDisconnected(int32 StatusCode, const FString& Reason, bool bWasClean) override
		{
		}

		virtual void OnSignallingError(const FString& ErrorMsg) override
		{
		}

		virtual void OnSignallingConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config) override
		{

			PeerConnection = FPixelStreamingPeerConnection::Create(Config);

			PeerConnection->OnEmitIceCandidate.AddLambda([this](const webrtc::IceCandidateInterface* Candidate) {
				SignallingServerConnection->SendIceCandidate(*Candidate);
			});

			PeerConnection->OnIceStateChanged.AddLambda([this](webrtc::PeerConnectionInterface::IceConnectionState NewState) {
				UE_LOG(LogPixelStreaming, Log, TEXT("Player OnIceStateChanged: %s"), UE::PixelStreaming::ToString(NewState));
				if (NewState == webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionConnected)
				{
					Completed = true;
					OnConnectionEstablished.Broadcast();
				}
			});

			//PeerConnection->SetVideoSink(new FMockVideoSink());

			if (Mode == EMode::CreateOffers)
			{
				PeerConnection->CreateOffer(
					FPixelStreamingPeerConnection::EReceiveMediaOption::All,
					[this](const webrtc::SessionDescriptionInterface* SDP) {
						SignallingServerConnection->SendOffer(*SDP);
					},
					[](const FString& Error) {
					});
			}
		}

		virtual void OnSignallingSessionDescription(webrtc::SdpType Type, const FString& Sdp) override
		{
			if (Type == webrtc::SdpType::kOffer && Mode == EMode::AcceptOffers)
			{
				const auto OnFailure = [](const FString& Error) {
					// fail
				};
				const auto OnSuccess = [this, &OnFailure]() {
					const auto OnSuccess = [this](const webrtc::SessionDescriptionInterface* Sdp) {
						SignallingServerConnection->SendAnswer(*Sdp);
					};
					PeerConnection->CreateAnswer(FPixelStreamingPeerConnection::EReceiveMediaOption::All, OnSuccess, OnFailure);
				};
				PeerConnection->ReceiveOffer(Sdp, OnSuccess, OnFailure);
			}
			else if (Type == webrtc::SdpType::kAnswer && Mode == EMode::CreateOffers)
			{
				const auto OnFailure = [](const FString& Error) {
					// fail
				};
				const auto OnSuccess = []() {
					// nothing
				};
				PeerConnection->ReceiveAnswer(Sdp, OnSuccess, OnFailure);
			}
		}

		virtual void OnSignallingRemoteIceCandidate(const FString& SdpMid, int SdpMLineIndex, const FString& Sdp) override
		{
			PeerConnection->AddRemoteIceCandidate(SdpMid, SdpMLineIndex, Sdp);
		}

		virtual void OnSignallingPlayerCount(uint32 Count) override {}
		virtual void OnSignallingPeerDataChannels(int32 SendStreamId, int32 RecvStreamId) override {}

		DECLARE_MULTICAST_DELEGATE(FOnConnectionEstablished);
		FOnConnectionEstablished OnConnectionEstablished;

		EMode Mode = EMode::Unknown;
		TUniquePtr<FPixelStreamingSignallingConnection> SignallingServerConnection;
		TUniquePtr<FPixelStreamingPeerConnection> PeerConnection;
		bool Completed = false;
	};

	TSharedPtr<FStreamer> CreateStreamer(int StreamerPort)
	{
		TSharedPtr<FStreamer> OutStreamer = FStreamer::Create("Mock Streamer");
		OutStreamer->SetVideoInput(FPixelStreamingVideoInputBackBuffer::Create());
		OutStreamer->SetSignallingServerURL(FString::Printf(TEXT("ws://127.0.0.1:%d"), StreamerPort));
		OutStreamer->StartStreaming();
		return OutStreamer;
	}

	TSharedPtr<FMockPlayer> CreatePlayer(FMockPlayer::EMode OfferMode, int PlayerPort)
	{
		TSharedPtr<FMockPlayer> OutPlayer = MakeShared<FMockPlayer>();
		OutPlayer->SetMode(OfferMode);
		return OutPlayer;
	}

	TSharedPtr<UE::PixelStreamingServers::IServer> CreateSignallingServer(int StreamerPort, int PlayerPort)
	{
		// Make signalling server
		TSharedPtr<UE::PixelStreamingServers::IServer> OutSignallingServer = UE::PixelStreamingServers::MakeSignallingServer();
		UE::PixelStreamingServers::FLaunchArgs LaunchArgs;
		LaunchArgs.ProcessArgs = FString::Printf(TEXT("--StreamerPort=%d --HttpPort=%d"), StreamerPort, PlayerPort);
		bool bLaunchedSignallingServer = OutSignallingServer->Launch(LaunchArgs);
		if(!bLaunchedSignallingServer)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to launch signalling server."));
		}
		UE_LOG(LogPixelStreaming, Log, TEXT("Signalling server launched=%s"), bLaunchedSignallingServer ? TEXT("true") : TEXT("false"));
		return OutSignallingServer;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FWaitForICEConnectedOrTimeout, double, TimeoutSeconds, TSharedPtr<FMockPlayer>, OutPlayer);
	bool FWaitForICEConnectedOrTimeout::Update()
	{
		// If no signalling we can early exit.
		if(!OutPlayer->IsSignallingConnected())
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Early exiting waiting for ICE connected as player is not connected to signalling server."));
			return true;
		}

		if (OutPlayer)
		{
			double DeltaTime = FPlatformTime::Seconds() - StartTime;
			if(DeltaTime > TimeoutSeconds)
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Timed out waiting for RTC connection between streamer and player."));
				return true;
			}
			return OutPlayer->Completed;
		}
		return true;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FWaitForStreamerConnectedOrTimeout, double, TimeoutSeconds, TSharedPtr<FStreamer>, OutStreamer);
	bool FWaitForStreamerConnectedOrTimeout::Update()
	{
		if(OutStreamer->IsSignallingConnected())
		{
			UE_LOG(LogPixelStreaming, Log, TEXT("Streamer connected to signalling server."));
			return true;
		}

		double DeltaTime = FPlatformTime::Seconds() - StartTime;
		if(DeltaTime > TimeoutSeconds)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Timed out waiting for streamer to connect to signalling server."));
			return true;
		}
		return false;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FWaitForPlayerConnectedOrTimeout, double, TimeoutSeconds, TSharedPtr<FMockPlayer>, OutPlayer, int, PlayerPort);
	bool FWaitForPlayerConnectedOrTimeout::Update()
	{
		if(OutPlayer->IsSignallingConnected())
		{
			UE_LOG(LogPixelStreaming, Log, TEXT("Player connected to signalling server."));
			return true;
		}
		else
		{
			OutPlayer->Connect(FString::Printf(TEXT("ws://127.0.0.1:%d"), PlayerPort));
		}

		double DeltaTime = FPlatformTime::Seconds() - StartTime;
		if(DeltaTime > TimeoutSeconds)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Timed out waiting for player to connect to signalling server."));
			return true;
		}
		return false;
	}

	DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FCleanupAll, TSharedPtr<UE::PixelStreamingServers::IServer>, OutSignallingServer, TSharedPtr<FStreamer>, OutStreamer, TSharedPtr<FMockPlayer>, OutPlayer);
	bool FCleanupAll::Update()
	{
		if(OutPlayer)
		{
			OutPlayer->Disconnect();
			OutPlayer.Reset();
		}

		if(OutStreamer)
		{
			OutStreamer->StopStreaming();
			OutStreamer.Reset();
		}

		if(OutSignallingServer)
		{
			OutSignallingServer->Stop();
			OutSignallingServer.Reset();
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandshakeTestStreamerOffer, "System.Plugins.PixelStreaming.HandshakeStreamerOffer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
	bool FHandshakeTestStreamerOffer::RunTest(const FString& Parameters)
	{
		int32 StreamerPort = 8866;
		int32 PlayerPort = 86;
		TSharedPtr<UE::PixelStreamingServers::IServer> OutSignallingServer = CreateSignallingServer(StreamerPort, PlayerPort);
		TSharedPtr<FStreamer> OutStreamer = CreateStreamer(StreamerPort);
		TSharedPtr<FMockPlayer> OutPlayer = CreatePlayer(FMockPlayer::EMode::AcceptOffers, PlayerPort);

		OutPlayer->OnConnectionEstablished.AddLambda([this, OutPlayer](){
			TestTrue(TEXT("Expected streamer and peer to establish RTC connection when streamer offered first."), OutPlayer->Completed);
		});

		ADD_LATENT_AUTOMATION_COMMAND(FWaitForStreamerConnectedOrTimeout(5.0, OutStreamer))
		ADD_LATENT_AUTOMATION_COMMAND(FWaitForPlayerConnectedOrTimeout(5.0, OutPlayer, PlayerPort))
		ADD_LATENT_AUTOMATION_COMMAND(FWaitForICEConnectedOrTimeout(5.0, OutPlayer))
		ADD_LATENT_AUTOMATION_COMMAND(FCleanupAll(OutSignallingServer, OutStreamer, OutPlayer))

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandshakeTestPlayerOffer, "System.Plugins.PixelStreaming.HandshakePlayerOffer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
	bool FHandshakeTestPlayerOffer::RunTest(const FString& Parameters)
	{
		int32 StreamerPort = 6446;
		int32 PlayerPort = 68;
		TSharedPtr<UE::PixelStreamingServers::IServer> OutSignallingServer = CreateSignallingServer(StreamerPort, PlayerPort);
		TSharedPtr<FStreamer> OutStreamer = CreateStreamer(StreamerPort);
		TSharedPtr<FMockPlayer> OutPlayer = CreatePlayer(FMockPlayer::EMode::CreateOffers, PlayerPort);

		OutPlayer->OnConnectionEstablished.AddLambda([this, OutPlayer](){
			TestTrue(TEXT("Expected streamer and peer to establish RTC connection when streamer offered first."), OutPlayer->Completed);
		});
		
		ADD_LATENT_AUTOMATION_COMMAND(FWaitForStreamerConnectedOrTimeout(5.0, OutStreamer))
		ADD_LATENT_AUTOMATION_COMMAND(FWaitForPlayerConnectedOrTimeout(5.0, OutPlayer, PlayerPort))
		ADD_LATENT_AUTOMATION_COMMAND(FWaitForICEConnectedOrTimeout(5.0, OutPlayer))
		ADD_LATENT_AUTOMATION_COMMAND(FCleanupAll(OutSignallingServer, OutStreamer, OutPlayer))

		return true;
	}

} // namespace UE::PixelStreaming

#endif // WITH_DEV_AUTOMATION_TESTS
