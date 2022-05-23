// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPeerConnection.h"
#include "PixelStreamingSignallingComponent.h"
#include "PixelStreamingMediaTexture.h"
#include "PixelStreamingPeerComponent.generated.h"

/**
 * A blueprint representation of a Pixel Streaming Peer Connection. Should communicate with a Pixel Streaming Signalling Connection
 * and will accept video sinks to receive video data.
 */
UCLASS(BlueprintType, Blueprintable, Category = "PixelStreaming", META = (DisplayName = "PixelStreaming Peer Component", BlueprintSpawnableComponent))
class PIXELSTREAMINGPLAYER_API UPixelStreamingPeerComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

public:
	/**
	 * Begins the peer connection.
	 * @param InSignallingComponent A signalling component provided for the peer connection to communicate with. This should be be same signalling connection used to negotiate this peer connection.
	 */
	UFUNCTION(BlueprintCallable, Category = "PixelStreaming")
	void Initialize(UPixelStreamingSignallingComponent* InSignallingComponent);

	/**
	 * Send an offer to the peer connection. This is used to negotiate a media connection.
	 * @param Sdp The Session Description provided from signalling.
	 */
	UFUNCTION(BlueprintCallable, Category = "PixelStreaming")
	void ReceiveOffer(const FString& Sdp);

	/**
	 * Notify the peer connection of an ICE candidate sent by the singalling connection.
	 * @param SdpMid Provided by the singalling connection.
	 * @param SdpMLineIndex Provided by the signalling connection.
	 * @param Sdp Provided by the signalling connection.
	 */
	UFUNCTION(BlueprintCallable, Category = "PixelStreaming")
	void ReceiveIceCandidate(const FString& SdpMid, int SdpMLineIndex, const FString& Sdp);

	/**
	 * A sink for the video data received once this connection has finished negotiating.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Properties", META = (DisplayName = "Pixel Streaming Video Sink", AllowPrivateAccess = true))
	UPixelStreamingMediaTexture* VideoSink = nullptr;

private:
	TUniquePtr<FPixelStreamingPeerConnection> PeerConnection;
	UPixelStreamingSignallingComponent* SignallingComponent = nullptr;
};
