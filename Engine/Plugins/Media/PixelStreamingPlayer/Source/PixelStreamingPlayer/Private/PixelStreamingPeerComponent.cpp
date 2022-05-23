// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingPeerComponent.h"
#include "PixelStreamingPlayerPrivate.h"

UPixelStreamingPeerComponent::UPixelStreamingPeerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UPixelStreamingPeerComponent::Initialize(UPixelStreamingSignallingComponent* InSignallingComponent)
{
	SignallingComponent = InSignallingComponent;

	PeerConnection = FPixelStreamingPeerConnection::Create(SignallingComponent->GetConfig());
	check(PeerConnection);
	PeerConnection->SetVideoSink(VideoSink);
	PeerConnection->SetIceCandidateCallback([&SignallingComponent = SignallingComponent](const webrtc::IceCandidateInterface* Candidate) {
		SignallingComponent->GetConnection()->SendIceCandidate(*Candidate);
	});
}

void UPixelStreamingPeerComponent::ReceiveOffer(const FString& Sdp)
{
	if (PeerConnection)
	{
		PeerConnection->SetSuccessCallback([&SignallingComponent = SignallingComponent](const webrtc::SessionDescriptionInterface* Sdp) {
			SignallingComponent->GetConnection()->SendAnswer(*Sdp);
		});
		PeerConnection->SetFailureCallback([](const FString& ErrorMsg) {
			UE_LOG(LogPixelStreamingPlayer, Log, TEXT("SetRemoteDescription Failed: %s"), *ErrorMsg);
		});
		PeerConnection->SetRemoteDescription(Sdp);
	}
}

void UPixelStreamingPeerComponent::ReceiveIceCandidate(const FString& SdpMid, int SdpMLineIndex, const FString& Sdp)
{
	if (PeerConnection) 
	{
		PeerConnection->AddRemoteIceCandidate(SdpMid, SdpMLineIndex, Sdp);
	}
}
