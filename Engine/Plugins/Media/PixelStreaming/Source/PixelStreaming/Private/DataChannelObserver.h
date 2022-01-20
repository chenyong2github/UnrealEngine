// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCIncludes.h"
#include "PixelStreamingPlayerId.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateCombinations.h"

namespace UE {
	namespace PixelStreaming {
		class IPixelStreamingSessions;
		class FInputDevice;

		// Observer on a particular player/peer's data channel.
		class FDataChannelObserver : public webrtc::DataChannelObserver
		{

		public:
			FDataChannelObserver(IPixelStreamingSessions* InSessions, FPixelStreamingPlayerId InPlayerId);

			// Begin webrtc::DataChannelObserver

			// The data channel state have changed.
			virtual void OnStateChange() override;

			//  A data buffer was successfully received.
			virtual void OnMessage(const webrtc::DataBuffer& buffer) override;

			// The data channel's buffered_amount has changed.
			virtual void OnBufferedAmountChange(uint64_t sent_data_size) override;

			// End webrtc::DataChannelObserver

			void SendInitialSettings() const;

			void Register(rtc::scoped_refptr<webrtc::DataChannelInterface> InDataChannel);
			void Unregister();

		private:
			void SendLatencyReport() const;
			

		private:
			rtc::scoped_refptr<webrtc::DataChannelInterface> DataChannel;
			IPixelStreamingSessions* PlayerSessions;
			FPixelStreamingPlayerId PlayerId;
			FInputDevice& InputDevice;
		};
	}
}
