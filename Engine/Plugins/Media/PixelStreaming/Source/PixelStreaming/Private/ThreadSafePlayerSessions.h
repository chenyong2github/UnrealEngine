// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IPixelStreamingSessions.h"
#include "PlayerSession.h"
#include "PixelStreamingDataChannelObserver.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateCombinations.h"

// This class provides a mechanism to read/write FPlayerSession on any thread.
// This is mostly achieved by submitting all such work to the WebRTC signalling thread
// using the WebRTC task queue (which is a single thread).
// Note: Any public methods in this class with a return type will making the calling thread block
// until the WebRTC thread is done and can return the result.
class FThreadSafePlayerSessions : public IPixelStreamingSessions
{
    public:

        FThreadSafePlayerSessions(rtc::Thread* WebRtcSignallingThread);
		virtual ~FThreadSafePlayerSessions() = default;
        bool IsInSignallingThread() const;
		void SendFreezeFrame(const TArray64<uint8>& JpegBytes);
		void SendUnfreezeFrame();
		void SendMessageAll(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const;
		void DisconnectPlayer(FPlayerId PlayerId, const FString& Reason);
		void SendLatestQPAllPlayers(int LatestQP) const;
		void OnRemoteIceCandidate(FPlayerId PlayerId, const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp);

		webrtc::PeerConnectionInterface* CreatePlayerSession(FPlayerId PlayerId, 
			rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory, 
			webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig, 
			FSignallingServerConnection* SignallingServerConnection);

		void DeleteAllPlayerSessions();
		int DeletePlayerSession(FPlayerId PlayerId);
		FPixelStreamingDataChannelObserver* GetDataChannelObserver(FPlayerId PlayerId);

        // Begin IPixelStreamingSessions
		virtual int GetNumPlayers() const override;
		virtual IPixelStreamingAudioSink* GetAudioSink(FPlayerId PlayerId) const override;
		virtual IPixelStreamingAudioSink* GetUnlistenedAudioSink() const override;
		virtual bool IsQualityController(FPlayerId PlayerId) const override;
		virtual void SetQualityController(FPlayerId PlayerId) override;
		virtual bool SendMessage(FPlayerId PlayerId, PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const override;
		virtual void SendLatestQP(FPlayerId PlayerId, int LatestQP) const override;
		virtual void SendFreezeFrameTo(FPlayerId PlayerId, const TArray64<uint8>& JpegBytes) const override;
        // End IPixelStreamingSessions

	public:
		DECLARE_MULTICAST_DELEGATE_OneParam(FOnQualityControllerChanged, FPlayerId)
	    FOnQualityControllerChanged OnQualityControllerChanged;

		DECLARE_MULTICAST_DELEGATE_OneParam(FOnPlayerDeleted, FPlayerId)
	    FOnPlayerDeleted OnPlayerDeleted;

	private:

		// Note: This is very intentionally internal and there is no public version because as soon as we hand it out it isn't thread safe anymore.
		FPlayerSession* GetPlayerSession_SignallingThread(FPlayerId PlayerId) const;

		void OnRemoteIceCandidate_SignallingThread(FPlayerId PlayerId, const std::string& SdpMid, int SdpMLineIndex, const std::string& Sdp);
		IPixelStreamingAudioSink* GetUnlistenedAudioSink_SignallingThread() const;
		IPixelStreamingAudioSink* GetAudioSink_SignallingThread(FPlayerId PlayerId) const;
		void SendLatestQP_SignallingThread(FPlayerId PlayerId, int LatestQP) const;
		int GetNumPlayers_SignallingThread() const;
		void SendFreezeFrame_SignallingThread(const TArray64<uint8>& JpegBytes);
		void SendUnfreezeFrame_SignallingThread();
		void SendFreezeFrameTo_SignallingThread(FPlayerId PlayerId, const TArray64<uint8>& JpegBytes) const;
		void SetQualityController_SignallingThread(FPlayerId PlayerId);
		void DisconnectPlayer_SignallingThread(FPlayerId PlayerId, const FString& Reason);
		void SendMessageAll_SignallingThread(PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const;
		bool SendMessage_SignallingThread(FPlayerId PlayerId, PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const;
		void SendLatestQPAllPlayers_SignallingThread(int LatestQP) const;

		webrtc::PeerConnectionInterface* CreatePlayerSession_SignallingThread(FPlayerId PlayerId, 
			rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerConnectionFactory, 
			webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig,
			FSignallingServerConnection* SignallingServerConnection);

		void DeleteAllPlayerSessions_SignallingThread();
		int DeletePlayerSession_SignallingThread(FPlayerId PlayerId);
		FPixelStreamingDataChannelObserver* GetDataChannelObserver_SignallingThread(FPlayerId PlayerId);

    private:
        rtc::Thread* WebRtcSignallingThread;
        TMap<FPlayerId, FPlayerSession*> Players;

		mutable FCriticalSection QualityControllerCS;
		FPlayerId QualityControllingPlayer = ToPlayerId(FString(TEXT("No quality controlling peer.")));

};