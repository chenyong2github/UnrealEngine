//

#pragma once

#include "PixelStreamingPlayerConfig.h"
#include "PixelStreamingPeerConnection.h"
#include "PixelStreamingDataChannel.h"

namespace UE::PixelStreaming
{
	struct FPlayerContext
	{
		FPixelStreamingPlayerConfig Config;
		TSharedPtr<FPixelStreamingPeerConnection> PeerConnection;
		TSharedPtr<FPixelStreamingDataChannel> DataChannel;
	};
}