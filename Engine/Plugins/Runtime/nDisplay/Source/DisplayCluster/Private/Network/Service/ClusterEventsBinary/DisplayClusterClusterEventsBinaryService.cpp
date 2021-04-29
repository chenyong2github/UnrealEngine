// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/Service/ClusterEventsBinary/DisplayClusterClusterEventsBinaryService.h"
#include "Network/Session/DisplayClusterSession.h"
#include "Network/Conversion/DisplayClusterNetworkDataConversion.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Cluster/DisplayClusterClusterEvent.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"

#include "Interfaces/IPv4/IPv4Endpoint.h"


FDisplayClusterClusterEventsBinaryService::FDisplayClusterClusterEventsBinaryService()
	: FDisplayClusterService(FString("SRV_CEB"))
{
}

FDisplayClusterClusterEventsBinaryService::~FDisplayClusterClusterEventsBinaryService()
{
	Shutdown();
}


TUniquePtr<IDisplayClusterSession> FDisplayClusterClusterEventsBinaryService::CreateSession(FSocket* Socket, const FIPv4Endpoint& Endpoint, uint64 SessionId)
{
	return MakeUnique<FDisplayClusterSession<FDisplayClusterPacketBinary, false, false>>(
		Socket,
		this,
		this,
		SessionId,
		FString::Printf(TEXT("%s_session_%lu_%s"), *GetName(), SessionId, *Endpoint.ToString()),
		FDisplayClusterService::GetThreadPriority());
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterSessionListener
//////////////////////////////////////////////////////////////////////////////////////////////
typename IDisplayClusterSessionPacketHandler<FDisplayClusterPacketBinary, false>::ReturnType FDisplayClusterClusterEventsBinaryService::ProcessPacket(const TSharedPtr<FDisplayClusterPacketBinary>& Request)
{
	// Check the pointer
	if (!Request.IsValid())
	{
		UE_LOG(LogDisplayClusterNetwork, Error, TEXT("%s - Invalid request data (nullptr)"), *GetName());
		return IDisplayClusterSessionPacketHandler<FDisplayClusterPacketBinary, false>::ReturnType();
	}

	// Convert net packet to the internal event data type
	FDisplayClusterClusterEventBinary ClusterEvent;
	if (!DisplayClusterNetworkDataConversion::BinaryPacketToBinaryEvent(Request, ClusterEvent))
	{
		UE_LOG(LogDisplayClusterNetwork, Error, TEXT("%s - couldn't translate net packet data to binary event"), *GetName());
		return typename IDisplayClusterSessionPacketHandler<FDisplayClusterPacketBinary, false>::ReturnType();
	}

	// Emit the event
	UE_LOG(LogDisplayClusterNetwork, Verbose, TEXT("%s - re-emitting cluster event for internal replication..."), *GetName());
	EmitClusterEventBinary(ClusterEvent);

	return typename IDisplayClusterSessionPacketHandler<FDisplayClusterPacketBinary, false>::ReturnType();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProtocolEventsBinary
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterEventsBinaryService::EmitClusterEventBinary(const FDisplayClusterClusterEventBinary& Event)
{
	GDisplayCluster->GetPrivateClusterMgr()->EmitClusterEventBinary(Event, true);
}
