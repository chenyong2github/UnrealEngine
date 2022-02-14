// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataChannelObserver.h"
#include "Async/Async.h"
#include "PixelStreamingPrivate.h"
#include "Settings.h"
#include "InputDevice.h"
#include "IPixelStreamingModule.h"
#include "Stats.h"
#include "PixelStreamingStatNames.h"
#include "PixelStreamingDelegates.h"
#include "PlayerSessions.h"

namespace UE::PixelStreaming
{
	FDataChannelObserver::FDataChannelObserver(FPlayerSessions* InPlayerSessions, FPixelStreamingPlayerId InPlayerId)
		: PlayerSessions(InPlayerSessions)
		, PlayerId(InPlayerId)
		, InputDevice(static_cast<FInputDevice&>(IPixelStreamingModule::Get().GetInputDevice()))
	{
	}

	void FDataChannelObserver::Register(rtc::scoped_refptr<webrtc::DataChannelInterface> InDataChannel)
	{
		DataChannel = InDataChannel;
		DataChannel->RegisterObserver(this);
	}

	void FDataChannelObserver::Unregister()
	{
		if (DataChannel)
		{
			DataChannel->UnregisterObserver();
			DataChannel = nullptr;
		}
	}

	void FDataChannelObserver::OnStateChange()
	{
		if (!DataChannel)
		{
			return;
		}

		switch (DataChannel->state())
		{
			case webrtc::DataChannelInterface::DataState::kOpen:
			{
				if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
				{
					Delegates->OnDataChannelOpenNative.Broadcast(PlayerId, DataChannel.get());
				}
				break;
			}
			case webrtc::DataChannelInterface::DataState::kClosed:
			{
				if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
				{
					Delegates->OnDataChannelClosedNative.Broadcast(PlayerId, DataChannel.get());
				}
				PlayerSessions->ForSession(PlayerId, [](TSharedPtr<IPlayerSession> Session) {
					Session->OnDataChannelClosed();
				});
				break;
			}
		}
	}

	void FDataChannelObserver::OnMessage(const webrtc::DataBuffer& Buffer)
	{
		Protocol::EToStreamerMsg MsgType = static_cast<Protocol::EToStreamerMsg>(Buffer.data.data()[0]);

		if (MsgType == Protocol::EToStreamerMsg::RequestQualityControl)
		{
			check(Buffer.data.size() == 1);
			UE_LOG(LogPixelStreaming, Log, TEXT("Player %s has requested quality control through the data channel."), *PlayerId);
			PlayerSessions->SetQualityController(PlayerId);
		}
		else if (MsgType == Protocol::EToStreamerMsg::LatencyTest)
		{
			SendLatencyReport();
		}
		else if (MsgType == Protocol::EToStreamerMsg::RequestInitialSettings)
		{
			SendInitialSettings();
		}
		else if (MsgType == Protocol::EToStreamerMsg::TestEcho)
		{
			const size_t StringLen = (Buffer.data.size() - 1) / sizeof(TCHAR);
			const TCHAR* StringPtr = reinterpret_cast<const TCHAR*>(Buffer.data.data() + 1);
			const FString EchoMessage(StringLen, StringPtr);
			PlayerSessions->ForSession(PlayerId, [&](TSharedPtr<IPlayerSession> Session) {
				Session->SendMessage(Protocol::EToPlayerMsg::TestEcho, EchoMessage);
			});
		}
		else if (!IsEngineExitRequested())
		{
			InputDevice.OnMessage(Buffer);
		}
	}

	void FDataChannelObserver::SendLatencyReport() const
	{
		if (Settings::CVarPixelStreamingDisableLatencyTester.GetValueOnAnyThread())
		{
			return;
		}

		double ReceiptTimeMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64());

		AsyncTask(ENamedThreads::GameThread, [this, ReceiptTimeMs]() {
			FString ReportToTransmitJSON;

			if (!UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCDisableStats.GetValueOnAnyThread())
			{
				double EncodeMs = -1.0;
				double CaptureToSendMs = 0.0;

				UE::PixelStreaming::FStats* Stats = UE::PixelStreaming::FStats::Get();
				if (Stats)
				{
					// bool QueryPeerStat_GameThread(FPixelStreamingPlayerId PlayerId, FName StatToQuery, double& OutStatValue)
					Stats->QueryPeerStat_GameThread(PlayerId, PixelStreamingStatNames::MeanEncodeTime, EncodeMs);
					Stats->QueryPeerStat_GameThread(PlayerId, PixelStreamingStatNames::MeanSendDelay, CaptureToSendMs);
				}

				double TransmissionTimeMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64());
				ReportToTransmitJSON = FString::Printf(
					TEXT("{ \"ReceiptTimeMs\": %.2f, \"EncodeMs\": %.2f, \"CaptureToSendMs\": %.2f, \"TransmissionTimeMs\": %.2f }"),
					ReceiptTimeMs,
					EncodeMs,
					CaptureToSendMs,
					TransmissionTimeMs);
			}
			else
			{
				double TransmissionTimeMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64());
				ReportToTransmitJSON = FString::Printf(
					TEXT("{ \"ReceiptTimeMs\": %.2f, \"EncodeMs\": \"Pixel Streaming stats are disabled\", \"CaptureToSendMs\": \"Pixel Streaming stats are disabled\", \"TransmissionTimeMs\": %.2f }"),
					ReceiptTimeMs,
					TransmissionTimeMs);
			}

			PlayerSessions->ForSession(PlayerId, [&](TSharedPtr<IPlayerSession> Session) {
				Session->SendMessage(Protocol::EToPlayerMsg::LatencyTest, ReportToTransmitJSON);
			});
		});
	}

	void FDataChannelObserver::OnBufferedAmountChange(uint64_t PreviousAmount)
	{
		if (!DataChannel)
		{
			return;
		}

		UE_LOG(
			LogPixelStreaming,
			VeryVerbose,
			TEXT("player %s: OnBufferedAmountChanged: prev %d, cur %d"),
			*PlayerId,
			PreviousAmount,
			DataChannel->buffered_amount());
	}

	void FDataChannelObserver::SendInitialSettings() const
	{
		const FString PixelStreamingPayload = FString::Printf(TEXT("{ \"AllowPixelStreamingCommands\": %s, \"DisableLatencyTest\": %s }"),
			Settings::CVarPixelStreamingAllowConsoleCommands.GetValueOnAnyThread() ? TEXT("true") : TEXT("false"),
			Settings::CVarPixelStreamingDisableLatencyTester.GetValueOnAnyThread() ? TEXT("true") : TEXT("false"));

		const FString WebRTCPayload = FString::Printf(TEXT("{ \"DegradationPref\": \"%s\", \"FPS\": %d, \"MinBitrate\": %d, \"MaxBitrate\": %d, \"LowQP\": %d, \"HighQP\": %d }"),
			*Settings::CVarPixelStreamingDegradationPreference.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingWebRTCFps.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingWebRTCMinBitrate.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingWebRTCLowQpThreshold.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingWebRTCHighQpThreshold.GetValueOnAnyThread());

		const FString EncoderPayload = FString::Printf(TEXT("{ \"TargetBitrate\": %d, \"MaxBitrate\": %d, \"MinQP\": %d, \"MaxQP\": %d, \"RateControl\": \"%s\", \"FillerData\": %d, \"MultiPass\": \"%s\" }"),
			Settings::CVarPixelStreamingEncoderTargetBitrate.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingEncoderMaxBitrate.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingEncoderMinQP.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingEncoderMaxQP.GetValueOnAnyThread(),
			*Settings::CVarPixelStreamingEncoderRateControl.GetValueOnAnyThread(),
			Settings::CVarPixelStreamingEnableFillerData.GetValueOnAnyThread() ? 1 : 0,
			*Settings::CVarPixelStreamingEncoderMultipass.GetValueOnAnyThread());

		const FString FullPayload = FString::Printf(TEXT("{ \"PixelStreaming\": %s, \"Encoder\": %s, \"WebRTC\": %s }"), *PixelStreamingPayload, *EncoderPayload, *WebRTCPayload);

		PlayerSessions->ForSession(PlayerId, [&](TSharedPtr<IPlayerSession> Session) {
			if (!Session->SendMessage(Protocol::EToPlayerMsg::InitialSettings, FullPayload))
			{
				UE_LOG(LogPixelStreaming, Log, TEXT("Failed to send initial Pixel Streaming settings."));
			}
		});
	}
} // namespace UE::PixelStreaming
