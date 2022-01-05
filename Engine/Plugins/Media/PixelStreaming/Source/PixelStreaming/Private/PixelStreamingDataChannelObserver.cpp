// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingDataChannelObserver.h"
#include "PixelStreamingPrivate.h"
#include "PixelStreamingSettings.h"
#include "InputDevice.h"
#include "Modules/ModuleManager.h"
#include "IPixelStreamingModule.h"
#include "PixelStreamingModule.h"
#include "HAL/PlatformTime.h"
#include "IPixelStreamingSessions.h"
#include "PixelStreamingStats.h"
#include "PixelStreamingStatNames.h"

FPixelStreamingDataChannelObserver::FPixelStreamingDataChannelObserver(IPixelStreamingSessions* InPlayerSessions, FPlayerId InPlayerId)
	: PlayerSessions(InPlayerSessions)
	, PlayerId(InPlayerId)
	, InputDevice(FPixelStreamingModule::GetModule()->GetInputDevice())
{
}

void FPixelStreamingDataChannelObserver::Register(rtc::scoped_refptr<webrtc::DataChannelInterface> InDataChannel)
{
	this->DataChannel = InDataChannel;
	this->DataChannel->RegisterObserver(this);
}

void FPixelStreamingDataChannelObserver::Unregister()
{
	if (this->DataChannel)
	{
		this->DataChannel->UnregisterObserver();
		this->DataChannel = nullptr;
	}
}

void FPixelStreamingDataChannelObserver::OnStateChange()
{
	webrtc::DataChannelInterface::DataState State = this->DataChannel->state();
	if (State == webrtc::DataChannelInterface::DataState::kOpen)
	{
		this->OnDataChannelOpen.Broadcast(this->PlayerId, this->DataChannel.get());
	}
}

void FPixelStreamingDataChannelObserver::OnMessage(const webrtc::DataBuffer& Buffer)
{
	PixelStreamingProtocol::EToStreamerMsg MsgType = static_cast<PixelStreamingProtocol::EToStreamerMsg>(Buffer.data.data()[0]);

	if (MsgType == PixelStreamingProtocol::EToStreamerMsg::RequestQualityControl)
	{
		check(Buffer.data.size() == 1);
		UE_LOG(PixelStreamer, Log, TEXT("Player %s has requested quality control through the data channel."), *this->PlayerId);
		this->PlayerSessions->SetQualityController(this->PlayerId);
	}
	else if (MsgType == PixelStreamingProtocol::EToStreamerMsg::LatencyTest)
	{
		SendLatencyReport();
	}
	else if (MsgType == PixelStreamingProtocol::EToStreamerMsg::RequestInitialSettings)
	{
		this->SendInitialSettings();
	}
	else if (!IsEngineExitRequested())
	{
		InputDevice.OnMessage(Buffer);
	}
}

void FPixelStreamingDataChannelObserver::SendLatencyReport() const
{
	if (PixelStreamingSettings::CVarPixelStreamingDisableLatencyTester.GetValueOnAnyThread())
	{
		return;
	}

	double ReceiptTimeMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64());

	AsyncTask(ENamedThreads::GameThread, [this, ReceiptTimeMs]() {
		double EncodeMs = -1.0;
		double CaptureToSendMs = 0.0;

		FPixelStreamingStats* Stats = FPixelStreamingStats::Get();
		if (Stats)
		{
			// bool QueryPeerStat_GameThread(FPlayerId PlayerId, FName StatToQuery, double& OutStatValue)
			Stats->QueryPeerStat_GameThread(PlayerId, PixelStreamingStatNames::MeanEncodeTime, EncodeMs);
			Stats->QueryPeerStat_GameThread(PlayerId, PixelStreamingStatNames::MeanSendDelay, CaptureToSendMs);
		}

		double TransmissionTimeMs = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64());

		FString ReportToTransmitJSON = FString::Printf(
			TEXT("{ \"ReceiptTimeMs\": %.2f, \"EncodeMs\": %.2f, \"CaptureToSendMs\": %.2f, \"TransmissionTimeMs\": %.2f }"),
			ReceiptTimeMs,
			EncodeMs,
			CaptureToSendMs,
			TransmissionTimeMs);

		PlayerSessions->SendMessage(PlayerId, PixelStreamingProtocol::EToPlayerMsg::LatencyTest, ReportToTransmitJSON);
	});
}

void FPixelStreamingDataChannelObserver::OnBufferedAmountChange(uint64_t PreviousAmount)
{
	UE_LOG(
		PixelStreamer,
		VeryVerbose,
		TEXT("player %s: OnBufferedAmountChanged: prev %d, cur %d"),
		*this->PlayerId,
		PreviousAmount,
		this->DataChannel->buffered_amount());
}

void FPixelStreamingDataChannelObserver::SendInitialSettings() const
{
	if (!this->DataChannel)
	{
		return;
	}

	const FString PixelStreamingPayload = FString::Printf(TEXT("{ \"AllowPixelStreamingCommands\": %s, \"DisableLatencyTest\": %s }"),
		PixelStreamingSettings::CVarPixelStreamingAllowConsoleCommands.GetValueOnAnyThread() ? TEXT("true") : TEXT("false"),
		PixelStreamingSettings::CVarPixelStreamingDisableLatencyTester.GetValueOnAnyThread() ? TEXT("true") : TEXT("false"));

	const FString WebRTCPayload = FString::Printf(TEXT("{ \"DegradationPref\": \"%s\", \"FPS\": %d, \"MinBitrate\": %d, \"MaxBitrate\": %d, \"LowQP\": %d, \"HighQP\": %d }"),
		*PixelStreamingSettings::CVarPixelStreamingDegradationPreference.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingWebRTCFps.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingWebRTCMinBitrate.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingWebRTCLowQpThreshold.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingWebRTCHighQpThreshold.GetValueOnAnyThread());

	const FString EncoderPayload = FString::Printf(TEXT("{ \"TargetBitrate\": %d, \"MaxBitrate\": %d, \"MinQP\": %d, \"MaxQP\": %d, \"RateControl\": \"%s\", \"FillerData\": %d, \"MultiPass\": \"%s\" }"),
		PixelStreamingSettings::CVarPixelStreamingEncoderTargetBitrate.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingEncoderMaxBitrate.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingEncoderMinQP.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingEncoderMaxQP.GetValueOnAnyThread(),
		*PixelStreamingSettings::CVarPixelStreamingEncoderRateControl.GetValueOnAnyThread(),
		PixelStreamingSettings::CVarPixelStreamingEnableFillerData.GetValueOnAnyThread() ? 1 : 0,
		*PixelStreamingSettings::CVarPixelStreamingEncoderMultipass.GetValueOnAnyThread());

	const FString FullPayload = FString::Printf(TEXT("{ \"PixelStreaming\": %s, \"Encoder\": %s, \"WebRTC\": %s }"), *PixelStreamingPayload, *EncoderPayload, *WebRTCPayload);

	if (!this->PlayerSessions->SendMessage(this->PlayerId, PixelStreamingProtocol::EToPlayerMsg::InitialSettings, FullPayload))
	{
		UE_LOG(PixelStreamer, Log, TEXT("Failed to send initial Pixel Streaming settings."));
	}
}