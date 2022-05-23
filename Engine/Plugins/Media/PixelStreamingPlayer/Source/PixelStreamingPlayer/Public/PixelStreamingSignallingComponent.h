// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingSignallingConnection.h"
#include "StreamMediaSource.h"
#include "Components/ActorComponent.h"
#include "PixelStreamingSignallingComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE(FPixelStreamingSignallingComponentConnected, UPixelStreamingSignallingComponent, OnConnected);
DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_OneParam(FPixelStreamingSignallingComponentConnectionError, UPixelStreamingSignallingComponent, OnConnectionError, const FString&, ErrorMsg);
DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_ThreeParams(FPixelStreamingSignallingComponentDisconnected, UPixelStreamingSignallingComponent, OnDisconnected, int32, StatusCode, const FString&, Reason, bool, bWasClean);
DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE(FPixelStreamingSignallingComponentConfig, UPixelStreamingSignallingComponent, OnConfig);
DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_OneParam(FPixelStreamingSignallingComponentOffer, UPixelStreamingSignallingComponent, OnOffer, const FString&, Sdp);
DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_OneParam(FPixelStreamingSignallingComponentAnswer, UPixelStreamingSignallingComponent, OnAnswer, const FString&, Sdp);
DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_ThreeParams(FPixelStreamingSignallingComponentIceCandidate, UPixelStreamingSignallingComponent, OnIceCandidate, const FString&, SdpMid, int, SdpMLineIndex, const FString&, Sdp);
DECLARE_DYNAMIC_MULTICAST_SPARSE_DELEGATE_TwoParams(FPixelStreamingSignallingComponentDataChannels, UPixelStreamingSignallingComponent, OnDataChannels, int32, SendStreamId, int32, RecvStreamId);

/**
 * A blueprint class representing a Pixel Streaming Signalling connection. Used to communicate with the signalling server and
 * should route information to the peer connection.
 */
UCLASS(BlueprintType, Blueprintable, Category = "PixelStreaming", META = (DisplayName = "PixelStreaming Signalling Component", BlueprintSpawnableComponent))
class PIXELSTREAMINGPLAYER_API UPixelStreamingSignallingComponent : public UActorComponent, public IPixelStreamingSignallingConnectionObserver
{
	GENERATED_UCLASS_BODY()

public:
	/**
	 * Attempt to connect to a specified signalling server.
	 * @param Url The url of the signalling server. Ignored if this component has a MediaSource. In that case the URL on the media source will be used instead.
	 */
	UFUNCTION(BlueprintCallable, Category = "PixelStreaming")
	void Connect(const FString& Url);

	/**
	 * Disconnect from the signalling server. No action if no connection exists.
	 */
	UFUNCTION(BlueprintCallable, Category = "PixelStreaming")
	void Disconnect();

	/**
	 * Fired when the signalling connection is successfully established.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Components|Activation")
	FPixelStreamingSignallingComponentConnected OnConnected;

	/**
	 * Fired if the connection failed or an error occurs during the connection. If this is fired at any point the connection should be considered closed.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Components|Activation")
	FPixelStreamingSignallingComponentConnectionError OnConnectionError;

	/**
	 * Fired when the connection successfully closes.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Components|Activation")
	FPixelStreamingSignallingComponentDisconnected OnDisconnected;

	/**
	 * Fired when the connection receives a config message from the server. This is the earliest place where the peer connection can be initialized.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Components|Activation")
	FPixelStreamingSignallingComponentConfig OnConfig;

	/**
	 * Fired when the connection receives an offer from the server. This means there is media being offered up to this connection. Forward to the peer connection.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Components|Activation")
	FPixelStreamingSignallingComponentOffer OnOffer;

	/**
	 * Fired when the server sends through an ice candidate. Forward this information on to the peer connection.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Components|Activation")
	FPixelStreamingSignallingComponentIceCandidate OnIceCandidate;

	/**
	 * If this media source is set we will use its supplied URL instead of the Url parameter on the connect call.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Properties", META = (DisplayName = "Stream Media Source", AllowPrivateAccess = true))
	UStreamMediaSource* MediaSource = nullptr;

	/**
	 * Gets the current webrtc RTC configuration which is supplied by the signalling server.
	 * @return The current webrtc RTC configuration from the signalling server.
	 */
	const webrtc::PeerConnectionInterface::RTCConfiguration& GetConfig() const { return RTCConfig; }

	/**
	 * Gets the current signalling connection object.
	 * @return The raw connection object/
	 */
	FPixelStreamingSignallingConnection* GetConnection() const { return SignallingConnection.Get(); }

protected:
	//
	// ISignallingServerConnectionObserver implementation.
	//
	virtual void OnSignallingConnected() override;
	virtual void OnSignallingDisconnected(int32 StatusCode, const FString& Reason, bool bWasClean) override;
	virtual void OnSignallingError(const FString& ErrorMsg) override;
	virtual void OnSignallingConfig(const webrtc::PeerConnectionInterface::RTCConfiguration& Config) override;
	virtual void OnSignallingSessionDescription(webrtc::SdpType Type, const FString& Sdp) override;
	virtual void OnSignallingRemoteIceCandidate(const FString& SdpMid, int SdpMLineIndex, const FString& Sdp) override;
	virtual void OnSignallingPeerDataChannels(int32 SendStreamId, int32 RecvStreamId) override;
	virtual void OnSignallingPlayerCount(uint32 Count) override;

private:
	TUniquePtr<FPixelStreamingSignallingConnection> SignallingConnection;
	webrtc::PeerConnectionInterface::RTCConfiguration RTCConfig;
};
