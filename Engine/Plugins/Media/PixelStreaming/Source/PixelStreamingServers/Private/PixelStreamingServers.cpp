// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingServers.h"
#include "CirrusWrapper.h"
#include "SignallingServer.h"

namespace UE::PixelStreamingServers
{
	TSharedPtr<IServer> MakeCirrusServer()
	{
		return MakeShared<FCirrusWrapper>();
	}

	TSharedPtr<IServer> MakeSignallingServer()
	{
		return MakeShared<FSignallingServer>();
	}

	TSharedPtr<FMonitoredProcess> DownloadPixelStreamingServers(bool bSkipIfPresent)
	{
		return Utils::DownloadPixelStreamingServers(bSkipIfPresent);
	}

} // namespace UE::PixelStreamingServers
