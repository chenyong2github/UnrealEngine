// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCIncludes.h"
#include "PixelStreamingPlayerId.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateCombinations.h"
#include "IPixelStreamingInputDevice.h"

namespace UE::PixelStreaming
{
	class FPlayerSessions;

	enum EDataChannelDirection
	{
		Bidirectional,
		SendOnly,
		RecvOnly
	};

	// Observer on a particular player/peer's data channel.
	class FDataChannelObserver : public webrtc::DataChannelObserver
	{

	public:
		FDataChannelObserver(FPlayerSessions* InSessions, FPixelStreamingPlayerId InPlayerId, EDataChannelDirection InDirection, TSharedPtr<IPixelStreamingInputDevice> InInputDevice);
		virtual ~FDataChannelObserver();

		// Begin webrtc::DataChannelObserver

		// The data channel state have changed.
		virtual void OnStateChange() override;

		//  A data buffer was successfully received.
		virtual void OnMessage(const webrtc::DataBuffer& buffer) override;

		// The data channel's buffered_amount has changed.
		virtual void OnBufferedAmountChange(uint64_t sent_data_size) override;

		// End webrtc::DataChannelObserver

		bool IsDataChannelOpen() const;
		void SendInitialSettings() const;
		void Register(rtc::scoped_refptr<webrtc::DataChannelInterface> InDataChannel);
		void Unregister();

	private:
		void SendPeerControllerMessages() const;
		void SendLatencyReport() const;
		void OnDataChannelOpen();
		void SetDirection(EDataChannelDirection InDirection);

	private:
		rtc::scoped_refptr<webrtc::DataChannelInterface> DataChannel;
		FPlayerSessions* PlayerSessions;
		FPixelStreamingPlayerId PlayerId;
		EDataChannelDirection Direction = EDataChannelDirection::Bidirectional;
		TSharedPtr<IPixelStreamingInputDevice> InputDevice;
	};
} // namespace UE::PixelStreaming
