// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/Service/RenderSync/DisplayClusterRenderSyncClient.h"
#include "Network/Service/RenderSync/DisplayClusterRenderSyncStrings.h"

#include "Misc/DisplayClusterLog.h"


FDisplayClusterRenderSyncClient::FDisplayClusterRenderSyncClient()
	: FDisplayClusterRenderSyncClient(FString("CLN_RS"))
{
}

FDisplayClusterRenderSyncClient::FDisplayClusterRenderSyncClient(const FString& InName)
	: FDisplayClusterClient(InName)
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProtocolRenderSync
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterRenderSyncClient::WaitForSwapSync()
{
	static const TSharedPtr<FDisplayClusterPacketInternal> Request(
		new FDisplayClusterPacketInternal(
			DisplayClusterRenderSyncStrings::WaitForSwapSync::Name,
			DisplayClusterRenderSyncStrings::TypeRequest,
			DisplayClusterRenderSyncStrings::ProtocolName)
	);

	{
		TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR(*FString::Printf(TEXT("nDisplay RenderSyncClient::%s"), *Request->GetName()), CpuChannel);
		SendRecvPacket(Request);
	}
}
