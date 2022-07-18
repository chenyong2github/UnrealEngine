// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingStreamer.h"
#include "IPixelStreamingSignallingConnectionObserver.h"
#include "PixelStreamingPeerConnection.h"
#include "VideoSourceGroup.h"
#include "ThreadSafeMap.h"
#include "Dom/JsonObject.h"
#include "IPixelStreamingInputChannel.h"
#include "PixelStreamingSignallingConnection.h"
#include "Templates/SharedPointer.h"

namespace UE::PixelStreaming
{
	class FStreamer : public IPixelStreamingStreamer, public IPixelStreamingSignallingConnectionObserver, public TSharedFromThis<FStreamer>
	{
	public:
		static TSharedPtr<FStreamer> Create(const FString& StreamerId);
		virtual ~FStreamer() = default;

		virtual void SetStreamFPS(int32 InFramesPerSecond) override;
		virtual int32 GetStreamFPS() override;
		virtual void SetCoupleFramerate(bool bCouple) override;

		virtual void SetVideoInput(TSharedPtr<IPixelStreamingVideoInput> Input) override;
		virtual TWeakPtr<IPixelStreamingVideoInput> GetVideoInput() override;
		virtual void SetTargetViewport(FSceneViewport* InTargetViewport) override;
		virtual FSceneViewport* GetTargetViewport() override;
		virtual void SetTargetWindow(TSharedPtr<SWindow> InTargetWindow) override;
		virtual TWeakPtr<SWindow> GetTargetWindow() override;

		virtual void SetSignallingServerURL(const FString& InSignallingServerURL) override;
		virtual FString GetSignallingServerURL() override;
		virtual bool IsSignallingConnected() override;
		virtual void StartStreaming() override;
		virtual void StopStreaming() override;
		virtual bool IsStreaming() const override { return bStreamingStarted; }

		virtual FStreamingStartedEvent& OnStreamingStarted() override;
		virtual FStreamingStoppedEvent& OnStreamingStopped() override;

		virtual void ForceKeyFrame() override;
		void PushFrame();

		virtual void FreezeStream(UTexture2D* Texture) override;
		virtual void UnfreezeStream() override;

		virtual void SendPlayerMessage(Protocol::EToPlayerMsg Type, const FString& Descriptor) override;
		virtual void SendFileData(const TArray64<uint8>& ByteData, FString& MimeType, FString& FileExtension) override;
		virtual void KickPlayer(FPixelStreamingPlayerId PlayerId) override;

		virtual void SetInputChannel(TSharedPtr<IPixelStreamingInputChannel> InInputChannel) override { InputChannel = InInputChannel; }
		virtual TWeakPtr<IPixelStreamingInputChannel> GetInputChannel() override { return InputChannel; }

		virtual IPixelStreamingAudioSink* GetPeerAudioSink(FPixelStreamingPlayerId PlayerId) override;
		virtual IPixelStreamingAudioSink* GetUnlistenedAudioSink() override;

		// TODO(Luke) hook this back up so that the Engine can change how the interface is working browser side
		void AddPlayerConfig(TSharedRef<FJsonObject>& JsonObject);

	private:
		FStreamer(const FString& StreamerId);

		bool CreateSession(FPixelStreamingPlayerId PlayerId);
		void AddStreams(FPixelStreamingPlayerId PlayerId);

		// IPixelStreamingSignallingConnectionObserver impl
		virtual void OnSignallingConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config) override;
		virtual void OnSignallingSessionDescription(FPixelStreamingPlayerId PlayerId, webrtc::SdpType Type, const FString& Sdp) override;
		virtual void OnSignallingRemoteIceCandidate(FPixelStreamingPlayerId PlayerId, const FString& SdpMid, int SdpMLineIndex, const FString& Sdp) override;
		virtual void OnSignallingPlayerConnected(FPixelStreamingPlayerId PlayerId, const FPixelStreamingPlayerConfig& PlayerConfig) override;
		virtual void OnSignallingPlayerDisconnected(FPixelStreamingPlayerId PlayerId) override;
		virtual void OnSignallingSFUPeerDataChannels(FPixelStreamingPlayerId SFUId, FPixelStreamingPlayerId PlayerId, int32 SendStreamId, int32 RecvStreamId) override;
		virtual void OnSignallingConnected() override;
		virtual void OnSignallingDisconnected(int32 StatusCode, const FString& Reason, bool bWasClean) override;
		virtual void OnSignallingError(const FString& ErrorMsg) override;

		// own methods
		void ConsumeStats(FPixelStreamingPlayerId PlayerId, FName StatName, float StatValue);
		void OnOffer(FPixelStreamingPlayerId PlayerId, const FString& Sdp);
		void OnAnswer(FPixelStreamingPlayerId PlayerId, const FString& Sdp);
		void DeletePlayerSession(FPixelStreamingPlayerId PlayerId);
		void DeleteAllPlayerSessions();
		void AddNewDataChannel(FPixelStreamingPlayerId PlayerId, TSharedPtr<FPixelStreamingDataChannel> NewChannel);
		void OnDataChannelOpen(FPixelStreamingPlayerId PlayerId);
		void OnDataChannelClosed(FPixelStreamingPlayerId PlayerId);
		void OnDataChannelMessage(FPixelStreamingPlayerId PlayerId, uint8 Type, const webrtc::DataBuffer& RawBuffer);
		void SendInitialSettings(FPixelStreamingPlayerId PlayerId) const;
		void SendPeerControllerMessages(FPixelStreamingPlayerId PlayerId) const;
		void SendLatencyReport(FPixelStreamingPlayerId PlayerId) const;
		void SendFreezeFrame(TArray<FColor> RawData, const FIntRect& Rect);
		void SendCachedFreezeFrameTo(FPixelStreamingPlayerId PlayerId) const;
		bool ShouldPeerGenerateFrames(FPixelStreamingPlayerId PlayerId) const;

		void SetQualityController(FPixelStreamingPlayerId PlayerId);

	private:
		FString StreamerId;
		FString CurrentSignallingServerURL;

		TSharedPtr<IPixelStreamingInputChannel> InputChannel;
		TUniquePtr<FPixelStreamingSignallingConnection> SignallingServerConnection;
		double LastSignallingServerConnectionAttemptTimestamp = 0;

		webrtc::PeerConnectionInterface::RTCConfiguration PeerConnectionConfig;

		struct FPlayerContext
		{
			FPixelStreamingPlayerConfig Config;
			TSharedPtr<FPixelStreamingPeerConnection> PeerConnection;
			TSharedPtr<FPixelStreamingDataChannel> DataChannel;
		};

		TThreadSafeMap<FPixelStreamingPlayerId, FPlayerContext> Players;

		FPixelStreamingPlayerId QualityControllingId = INVALID_PLAYER_ID;
		FPixelStreamingPlayerId SFUPlayerId = INVALID_PLAYER_ID;
		FPixelStreamingPlayerId InputControllingId = INVALID_PLAYER_ID;

		bool bStreamingStarted = false;
		bool bCaptureNextBackBufferAndStream = false;

		// When we send a freeze frame we retain the data so we send freeze frame to new peers if they join during a freeze frame.
		TArray64<uint8> CachedJpegBytes;

		FStreamingStartedEvent StreamingStartedEvent;
		FStreamingStoppedEvent StreamingStoppedEvent;

		TUniquePtr<webrtc::SessionDescriptionInterface> SFULocalDescription;
		TUniquePtr<webrtc::SessionDescriptionInterface> SFURemoteDescription;

		TSharedPtr<FVideoSourceGroup> VideoSourceGroup;

		FDelegateHandle ConsumeStatsHandle;
	};
} // namespace UE::PixelStreaming
