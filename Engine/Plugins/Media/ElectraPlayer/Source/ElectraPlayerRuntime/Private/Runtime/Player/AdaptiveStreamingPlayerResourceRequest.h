// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "StreamTypes.h"
#include "InfoLog.h"

namespace Electra
{

class IAdaptiveStreamingPlayerResourceRequest : public TSharedFromThis<IAdaptiveStreamingPlayerResourceRequest, ESPMode::ThreadSafe>
{
public:
	enum class EPlaybackResourceType
	{
		Playlist,
		LicenseKey
	};

	virtual ~IAdaptiveStreamingPlayerResourceRequest() = default;

	//! Returns the type of the requested resource.
	virtual EPlaybackResourceType GetResourceType() const = 0;
	//! Returns the URL of the requested resource.
	virtual FString GetResourceURL() const = 0;

	//! Sets the binary resource data. If data can not be provided do not set anything (or a nullptr)
	virtual void SetPlaybackData(TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe>	PlaybackData) = 0;

	//! Signal request completion. Must be called with ot without data being set.
	virtual void SignalDataReady() = 0;
};



class IAdaptiveStreamingPlayerResourceProvider
{
public:
	virtual ~IAdaptiveStreamingPlayerResourceProvider() = default;
	virtual void ProvideStaticPlaybackDataForURL(TSharedPtr<IAdaptiveStreamingPlayerResourceRequest, ESPMode::ThreadSafe> InOutRequest) = 0;
};


} // namespace Electra


