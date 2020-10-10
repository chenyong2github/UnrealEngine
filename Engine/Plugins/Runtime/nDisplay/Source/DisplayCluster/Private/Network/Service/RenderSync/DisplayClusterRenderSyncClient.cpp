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
void FDisplayClusterRenderSyncClient::WaitForSwapSync(double* ThreadWaitTime, double* BarrierWaitTime)
{
	static const TSharedPtr<FDisplayClusterPacketInternal> Request(
		new FDisplayClusterPacketInternal(
			DisplayClusterRenderSyncStrings::WaitForSwapSync::Name,
			DisplayClusterRenderSyncStrings::TypeRequest,
			DisplayClusterRenderSyncStrings::ProtocolName)
	);

	TSharedPtr<FDisplayClusterPacketInternal> Response = SendRecvPacket(Request);

	if (Response.IsValid())
	{
		if (ThreadWaitTime)
		{
			if (!Response->GetTextArg(FString(DisplayClusterRenderSyncStrings::ArgumentsDefaultCategory), FString(DisplayClusterRenderSyncStrings::WaitForSwapSync::ArgThreadTime), *ThreadWaitTime))
			{
				UE_LOG(LogDisplayClusterNetwork, Verbose, TEXT("Argument %s not available"), DisplayClusterRenderSyncStrings::WaitForSwapSync::ArgThreadTime);
			}
		}

		if (BarrierWaitTime)
		{
			if (!Response->GetTextArg(FString(DisplayClusterRenderSyncStrings::ArgumentsDefaultCategory), FString(DisplayClusterRenderSyncStrings::WaitForSwapSync::ArgBarrierTime), *BarrierWaitTime))
			{
				UE_LOG(LogDisplayClusterNetwork, Verbose, TEXT("Argument %s not available"), DisplayClusterRenderSyncStrings::WaitForSwapSync::ArgBarrierTime);
			}
		}
	}
}
