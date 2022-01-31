// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataChannelObserver.h"
#include "Async/Async.h"
#include "PixelStreamingPrivate.h"
#include "Settings.h"
#include "InputDevice.h"
#include "Modules/ModuleManager.h"
#include "IPixelStreamingModule.h"
#include "PixelStreamingModule.h"
#include "HAL/PlatformTime.h"
#include "IPixelStreamingSessions.h"
#include "Stats.h"
#include "PixelStreamingStatNames.h"
#include "PixelStreamingDelegates.h"

UE::PixelStreaming::FDataChannelObserver::FDataChannelObserver(UE::PixelStreaming::IPixelStreamingSessions* InPlayerSessions, FPixelStreamingPlayerId InPlayerId)
	: PlayerSessions(InPlayerSessions)
	, PlayerId(InPlayerId)
	, InputDevice(static_cast<FInputDevice&>(IPixelStreamingModule::Get().GetInputDevice()))
{
}

void UE::PixelStreaming::FDataChannelObserver::Register(rtc::scoped_refptr<webrtc::DataChannelInterface> InDataChannel)
{
	DataChannel = InDataChannel;
	DataChannel->RegisterObserver(this);
}

void UE::PixelStreaming::FDataChannelObserver::Unregister()
{
	if (DataChannel)
	{
		DataChannel->UnregisterObserver();
		DataChannel = nullptr;
	}
}

void UE::PixelStreaming::FDataChannelObserver::OnStateChange()
{
	if (!DataChannel)
	{
		return;
	}
	
	webrtc::DataChannelInterface::DataState State = DataChannel->state();
	UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates();
	if (State == webrtc::DataChannelInterface::DataState::kOpen && Delegates)
	{
		Delegates->OnDataChannelOpenNative.Broadcast(this->PlayerId, this->DataChannel.get());
	}
}

void UE::PixelStreaming::FDataChannelObserver::OnMessage(const webrtc::DataBuffer& Buffer)
{
	if (!DataChannel)
	{
		return;
	}

	UE::PixelStreaming::Protocol::EToStreamerMsg MsgType = static_cast<UE::PixelStreaming::Protocol::EToStreamerMsg>(Buffer.data.data()[0]);

	if (MsgType == UE::PixelStreaming::Protocol::EToStreamerMsg::RequestQualityControl)
	{
		check(Buffer.data.size() == 1);
		UE_LOG(LogPixelStreaming, Log, TEXT("Player %s has requested quality control through the data channel."), *PlayerId);
		PlayerSessions->SetQualityController(PlayerId);
	}
	else if (MsgType == UE::PixelStreaming::Protocol::EToStreamerMsg::LatencyTest)
	{
		SendLatencyReport();
	}
	else if (MsgType == UE::PixelStreaming::Protocol::EToStreamerMsg::RequestInitialSettings)
	{
		SendInitialSettings();
	}
	else if (!IsEngineExitRequested())
	{
		InputDevice.OnMessage(Buffer);
	}
}

void UE::PixelStreaming::FDataChannelObserver::SendLatencyReport() const
{
	if (!DataChannel)
	{
		return;
	}

	if (UE::PixelStreaming::Settings::CVarPixelStreamingDisableLatencyTester.GetValueOnAnyThread())
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
		
		PlayerSessions->SendMessage(PlayerId, UE::PixelStreaming::Protocol::EToPlayerMsg::LatencyTest, ReportToTransmitJSON);
	});
}

void UE::PixelStreaming::FDataChannelObserver::OnBufferedAmountChange(uint64_t PreviousAmount)
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

void UE::PixelStreaming::FDataChannelObserver::SendInitialSettings() const
{
	if (!DataChannel)
	{
		return;
	}

	const FString PixelStreamingPayload = FString::Printf(TEXT("{ \"AllowPixelStreamingCommands\": %s, \"DisableLatencyTest\": %s }"),
		UE::PixelStreaming::Settings::CVarPixelStreamingAllowConsoleCommands.GetValueOnAnyThread() ? TEXT("true") : TEXT("false"),
		UE::PixelStreaming::Settings::CVarPixelStreamingDisableLatencyTester.GetValueOnAnyThread() ? TEXT("true") : TEXT("false"));

	const FString WebRTCPayload = FString::Printf(TEXT("{ \"DegradationPref\": \"%s\", \"FPS\": %d, \"MinBitrate\": %d, \"MaxBitrate\": %d, \"LowQP\": %d, \"HighQP\": %d }"),
		*UE::PixelStreaming::Settings::CVarPixelStreamingDegradationPreference.GetValueOnAnyThread(),
		UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCFps.GetValueOnAnyThread(),
		UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCMinBitrate.GetValueOnAnyThread(),
		UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread(),
		UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCLowQpThreshold.GetValueOnAnyThread(),
		UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCHighQpThreshold.GetValueOnAnyThread());

	const FString EncoderPayload = FString::Printf(TEXT("{ \"TargetBitrate\": %d, \"MaxBitrate\": %d, \"MinQP\": %d, \"MaxQP\": %d, \"RateControl\": \"%s\", \"FillerData\": %d, \"MultiPass\": \"%s\" }"),
		UE::PixelStreaming::Settings::CVarPixelStreamingEncoderTargetBitrate.GetValueOnAnyThread(),
		UE::PixelStreaming::Settings::CVarPixelStreamingEncoderMaxBitrate.GetValueOnAnyThread(),
		UE::PixelStreaming::Settings::CVarPixelStreamingEncoderMinQP.GetValueOnAnyThread(),
		UE::PixelStreaming::Settings::CVarPixelStreamingEncoderMaxQP.GetValueOnAnyThread(),
		*UE::PixelStreaming::Settings::CVarPixelStreamingEncoderRateControl.GetValueOnAnyThread(),
		UE::PixelStreaming::Settings::CVarPixelStreamingEnableFillerData.GetValueOnAnyThread() ? 1 : 0,
		*UE::PixelStreaming::Settings::CVarPixelStreamingEncoderMultipass.GetValueOnAnyThread());

	const FString FullPayload = FString::Printf(TEXT("{ \"PixelStreaming\": %s, \"Encoder\": %s, \"WebRTC\": %s }"), *PixelStreamingPayload, *EncoderPayload, *WebRTCPayload);

	if (!PlayerSessions->SendMessage(PlayerId, UE::PixelStreaming::Protocol::EToPlayerMsg::InitialSettings, FullPayload))
	{
		UE_LOG(LogPixelStreaming, Log, TEXT("Failed to send initial Pixel Streaming settings."));
	}
}