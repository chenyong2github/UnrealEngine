// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingAudioSink.h"
#include "PlayerId.h"
#include "ProtocolDefs.h"
#include "Containers/UnrealString.h"

// `FPlayerSession` is only directly accessible to `FStreamer` as it completely owns the lifecycle of those objects.
// This is nice from a safety point of view; however, some other objects in Pixel Streaming need to interface with `FPlayerSession`.
// By using `FPlayerId` users can use this `IPixelStreamingSessions` interface to perform a limited set of allowable
// actions on `FPlayerSession` objects.
class IPixelStreamingSessions
{
	public:
		virtual int GetNumPlayers() const = 0;
		virtual IPixelStreamingAudioSink* GetAudioSink(FPlayerId PlayerId) const = 0;
		virtual IPixelStreamingAudioSink* GetUnlistenedAudioSink() const = 0;
		virtual bool IsQualityController(FPlayerId PlayerId) const = 0;
		virtual void SetQualityController(FPlayerId PlayerId) = 0;
		virtual bool SendMessage(FPlayerId PlayerId, PixelStreamingProtocol::EToPlayerMsg Type, const FString& Descriptor) const = 0;
		virtual void SendLatestQP(FPlayerId PlayerId, int LatestQP) const = 0;
		virtual void SendFreezeFrameTo(FPlayerId PlayerId, const TArray64<uint8>& JpegBytes) const = 0;
};