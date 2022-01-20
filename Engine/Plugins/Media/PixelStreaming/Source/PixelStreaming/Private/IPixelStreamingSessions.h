// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingAudioSink.h"
#include "PixelStreamingPlayerId.h"
#include "ProtocolDefs.h"
#include "Containers/UnrealString.h"

namespace UE {
	namespace PixelStreaming {
		// `FPlayerSession` is only directly accessible to `FThreadSafePlayerSessions` as it completely owns the lifecycle of those objects.
		// This is nice from a safety point of view; however, some other objects in Pixel Streaming need to interface with `FPlayerSession`.
		// By using `FPixelStreamingPlayerId` users can use this `IPixelStreamingSessions` interface to perform a limited set of allowable
		// actions on `FPlayerSession` objects.
		class IPixelStreamingSessions
		{
		public:
			virtual int GetNumPlayers() const = 0;
			virtual IPixelStreamingAudioSink* GetAudioSink(FPixelStreamingPlayerId PlayerId) const = 0;
			virtual IPixelStreamingAudioSink* GetUnlistenedAudioSink() const = 0;
			virtual bool IsQualityController(FPixelStreamingPlayerId PlayerId) const = 0;
			virtual void SetQualityController(FPixelStreamingPlayerId PlayerId) = 0;
			virtual bool SendMessage(FPixelStreamingPlayerId PlayerId, Protocol::EToPlayerMsg Type, const FString& Descriptor) const = 0;
			virtual void SendLatestQP(FPixelStreamingPlayerId PlayerId, int LatestQP) const = 0;
			virtual void SendFreezeFrameTo(FPixelStreamingPlayerId PlayerId, const TArray64<uint8>& JpegBytes) const = 0;
			virtual void PollWebRTCStats() const = 0;
		};
	}
}
