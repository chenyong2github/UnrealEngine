// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/Service/ClusterEventsBinary/DisplayClusterClusterEventsBinaryClient.h"
#include "Network/Conversion/DisplayClusterNetworkDataConversion.h"
#include "Cluster/DisplayClusterClusterEvent.h"

#include "Misc/DisplayClusterLog.h"


FDisplayClusterClusterEventsBinaryClient::FDisplayClusterClusterEventsBinaryClient()
	: FDisplayClusterClusterEventsBinaryClient(FString("CLN_CEB"))
{
}

FDisplayClusterClusterEventsBinaryClient::FDisplayClusterClusterEventsBinaryClient(const FString& InName)
	: FDisplayClusterClient(InName)
{
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProtocolEventsBinary
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterEventsBinaryClient::EmitClusterEventBinary(const FDisplayClusterClusterEventBinary& Event)
{
	// Convert internal binary event type to binary net packet
	TSharedPtr<FDisplayClusterPacketBinary> Request = DisplayClusterNetworkDataConversion::BinaryEventToBinaryPacket(Event);
	if (!Request)
	{
		UE_LOG(LogDisplayClusterNetworkMsg, Error, TEXT("Couldn't convert binary cluster event data to net packet"));
		return;
	}

	// Send event
	const bool bResult = SendPacket(Request);
	if (!bResult)
	{
		UE_LOG(LogDisplayClusterNetwork, Error, TEXT("Couldn't send binary cluster event"));
	}
}
