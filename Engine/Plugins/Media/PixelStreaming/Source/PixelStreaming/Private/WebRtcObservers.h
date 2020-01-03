// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCIncludes.h"
#include "Utils.h"

#include "Templates/UnrealTemplate.h"
#include "Templates/Function.h"
#include "Containers/UnrealString.h"

#include <string>
#include <memory>

//////////////////////////////////////////////////////////////////////////
// FSetSessionDescriptionObserver
// WebRTC requires an implementation of `webrtc::SetSessionDescriptionObserver` interface as a callback
// for setting session description, either on receiving remote `offer` (`PeerConnection::SetRemoteDescription`)
// of on sending `answer` (`PeerConnection::SetLocalDescription`)
class FSetSessionDescriptionObserver : public webrtc::SetSessionDescriptionObserver
{
public:
	using FSuccessCallback = TUniqueFunction<void()>;
	using FFailureCallback = TUniqueFunction<void(const FString&)>;

	static FSetSessionDescriptionObserver*
		Create(FSuccessCallback&& successCallback, FFailureCallback&& failureCallback)
	{
		return new rtc::RefCountedObject<FSetSessionDescriptionObserver>(MoveTemp(successCallback), MoveTemp(failureCallback));
	}

	FSetSessionDescriptionObserver(FSuccessCallback&& successCallback, FFailureCallback&& failureCallback)
		: SuccessCallback(MoveTemp(successCallback))
		, FailureCallback(MoveTemp(failureCallback))
	{}

	// we don't need to do anything on success
	void OnSuccess() override
	{
		SuccessCallback();
	}

	// errors usually mean incompatibility between our session configuration (often H.264, its profile and level) and
	// player, malformed SDP or if player doesn't support PlanB/UnifiedPlan (whatever was used by proxy)
	void OnFailure(const std::string& Error) override
	{
		FailureCallback(ToString(Error));
	}

private:
	FSuccessCallback SuccessCallback;
	FFailureCallback FailureCallback;
};

class FCreateSessionDescriptionObserver : public webrtc::CreateSessionDescriptionObserver
{
public:
	using FSuccessCallback = TUniqueFunction<void(webrtc::SessionDescriptionInterface*)>;
	using FFailureCallback = TUniqueFunction<void(const FString&)>;

	static FCreateSessionDescriptionObserver*
		Create(FSuccessCallback&& successCallback, FFailureCallback&& failureCallback)
	{
		return new rtc::RefCountedObject<FCreateSessionDescriptionObserver>(MoveTemp(successCallback), MoveTemp(failureCallback));
	}

	FCreateSessionDescriptionObserver(FSuccessCallback&& successCallback, FFailureCallback&& failureCallback)
		: SuccessCallback(MoveTemp(successCallback))
		, FailureCallback(MoveTemp(failureCallback))
	{}

	void OnSuccess(webrtc::SessionDescriptionInterface* SDP) override
	{
		SuccessCallback(SDP);
	}

	void OnFailure(webrtc::RTCError Error) override
	{
		FailureCallback(ANSI_TO_TCHAR(Error.message()));
	}

private:
	FSuccessCallback SuccessCallback;
	FFailureCallback FailureCallback;
};
