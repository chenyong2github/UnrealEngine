// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCIncludes.h"
#include "PlayerId.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateCombinations.h"

class IPixelStreamingSessions;
class FInputDevice;

// Observer on a particular player/peer's data channel.
class FPixelStreamingDataChannelObserver : public webrtc::DataChannelObserver
{

    public:

        FPixelStreamingDataChannelObserver(IPixelStreamingSessions* InSessions, FPlayerId InPlayerId);

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

    public:

        DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDataChannelOpen, FPlayerId, webrtc::DataChannelInterface*)
	    FOnDataChannelOpen OnDataChannelOpen;

    private:
        rtc::scoped_refptr<webrtc::DataChannelInterface> DataChannel;
        IPixelStreamingSessions* PlayerSessions;
        FPlayerId PlayerId;
        FInputDevice& InputDevice;


};