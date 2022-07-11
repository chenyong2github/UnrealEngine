// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "PixelStreamingServers.h"
#include "Streamer.h"
#include "IPixelStreamingSignallingConnectionObserver.h"
#include "PixelStreamingPrivate.h"
#include "WebSocketsModule.h"
#include "ToStringExtensions.h"
#include "PixelStreamingVideoInputBackBuffer.h"
#include "Utils.h"
#include "Settings.h"

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
		}

		virtual ~FMockPlayer() = default;

		enum class EMode
		{
			Unknown,
			AcceptOffers,
			OfferToReceive,
		};

		void SetMode(EMode InMode) { Mode = InMode; }

		void Connect(const FString& Url)
		{
			SignallingServerConnection->Connect(Url);
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
				}
			});

			//PeerConnection->SetVideoSink(new FMockVideoSink());

			if (Mode == EMode::OfferToReceive)
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
			else if (Type == webrtc::SdpType::kAnswer && Mode == EMode::OfferToReceive)
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

		EMode Mode = EMode::Unknown;
		TUniquePtr<FPixelStreamingSignallingConnection> SignallingServerConnection;
		TUniquePtr<FPixelStreamingPeerConnection> PeerConnection;
		bool Completed = false;
	};

	class FHandshakeTest;

	class FMockStreamRun : public FRunnable
	{
	public:
		FMockStreamRun(FHandshakeTest& InParentTest)
			: ParentTest(InParentTest) {}
		virtual ~FMockStreamRun() = default;

		virtual bool Init() override
		{
			bRunning = true;
			return true;
		}

		virtual void Stop() override
		{
		}

		virtual void Exit() override
		{
		}

		virtual uint32 Run() override;

		FHandshakeTest& ParentTest;
		bool bRunning = false;
	};

	class FHandshakeWaitCommand : public IAutomationLatentCommand
	{
	public:
		FHandshakeWaitCommand(FHandshakeTest& ParentTest)
		{
			StreamRunnable = MakeUnique<FMockStreamRun>(ParentTest);
			StreamThread = FRunnableThread::Create(StreamRunnable.Get(), TEXT("FMockStreamRun Thread"));
		}

		virtual ~FHandshakeWaitCommand()
		{
			StreamThread->Kill(true);
		}

		virtual bool Update() override
		{
			return !StreamRunnable->bRunning;
		}

		FRunnableThread* StreamThread = nullptr;
		TUniquePtr<FMockStreamRun> StreamRunnable;
	};

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandshakeTest, "System.Plugins.PixelStreaming.Handshake", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::Disabled)
	bool FHandshakeTest::RunTest(const FString& Parameters)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FHandshakeWaitCommand(*this));
		return true;
	}

	uint32 FMockStreamRun::Run()
	{
		// if we want to actually check we receive frames we need to use VP8 for now
		//UE::PixelStreaming::DoOnGameThreadAndWait(MAX_uint32, []() {
		//	Settings::CVarPixelStreamingEncoderCodec->Set(TEXT("VP8"), ECVF_SetByCode);
		//});

		TSharedPtr<FStreamer> Streamer;
		TUniquePtr<FMockPlayer> Player;

		Streamer = FStreamer::Create("Mock Streamer");
		Streamer->SetVideoInput(FPixelStreamingVideoInputBackBuffer::Create());
		Streamer->SetSignallingServerURL("ws://localhost:8888");
		Streamer->StartStreaming();

		FPlatformProcess::Sleep(2.0f);

		Player = MakeUnique<FMockPlayer>();
		Player->SetMode(FMockPlayer::EMode::AcceptOffers);
		Player->Connect("ws://localhost");

		FPlatformProcess::Sleep(3.0f);

		if (!Player->Completed)
		{
			UE_LOG(PixelStreamingHandshakeLog, Error, TEXT("Failed Streamer Offer: ICE state did not change to connected."));
		}

		Player = nullptr;
		Streamer = nullptr;

		FPlatformProcess::Sleep(5.0f);

		UE::PixelStreaming::DoOnGameThreadAndWait(MAX_uint32, []() {
			Settings::CVarPixelStreamingSuppressICECandidateErrors->Set(true, ECVF_SetByCode);
		});

		Streamer = FStreamer::Create("Mock Streamer");
		Streamer->SetVideoInput(FPixelStreamingVideoInputBackBuffer::Create());
		Streamer->SetSignallingServerURL("ws://localhost:8888");
		Streamer->StartStreaming();

		FPlatformProcess::Sleep(2.0f);

		Player = MakeUnique<FMockPlayer>();
		Player->SetMode(FMockPlayer::EMode::OfferToReceive);
		Player->Connect("ws://localhost");

		FPlatformProcess::Sleep(3.0f);

		if (!Player->Completed)
		{
			UE_LOG(PixelStreamingHandshakeLog, Error, TEXT("Failed Player Offer: ICE state did not change to connected."));
		}

		Player = nullptr;
		Streamer = nullptr;

		UE::PixelStreaming::DoOnGameThread([]() {
			Settings::CVarPixelStreamingSuppressICECandidateErrors->Set(false, ECVF_SetByCode);
		});

		bRunning = false;
		return 0;
	}
} // namespace UE::PixelStreaming

#endif // WITH_DEV_AUTOMATION_TESTS
