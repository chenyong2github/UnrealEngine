// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/Service/RenderSync/DisplayClusterRenderSyncService.h"
#include "Network/Service/RenderSync/DisplayClusterRenderSyncStrings.h"
#include "Network/Packet/DisplayClusterPacketInternal.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Config/IPDisplayClusterConfigManager.h"

#include "Network/Session/DisplayClusterSession.h"

#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterAppExit.h"
#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"

#include "Interfaces/IPv4/IPv4Endpoint.h"


FDisplayClusterRenderSyncService::FDisplayClusterRenderSyncService()
	: FDisplayClusterService(FString("SRV_RS"))
	, BarrierSwap(GDisplayCluster->GetPrivateClusterMgr()->GetNodesAmount(), FString("RenderSync_barrier"), GDisplayCluster->GetConfigMgr()->GetConfig()->Cluster->Network.RenderSyncBarrierTimeout)
{
}

FDisplayClusterRenderSyncService::~FDisplayClusterRenderSyncService()
{
	Shutdown();
}


bool FDisplayClusterRenderSyncService::Start(const FString& Address, int32 Port)
{
	BarrierSwap.Activate();

	return FDisplayClusterServer::Start(Address, Port);
}

void FDisplayClusterRenderSyncService::Shutdown()
{
	BarrierSwap.Deactivate();

	return FDisplayClusterServer::Shutdown();
}

TUniquePtr<IDisplayClusterSession> FDisplayClusterRenderSyncService::CreateSession(FSocket* Socket, const FIPv4Endpoint& Endpoint, uint64 SessionId)
{
	return MakeUnique<FDisplayClusterSession<FDisplayClusterPacketInternal, true, true>>(
		Socket,
		this,
		this,
		SessionId,
		FString::Printf(TEXT("%s_session_%lu_%s"), *GetName(), SessionId, *Endpoint.ToString()),
		FDisplayClusterService::GetThreadPriority());
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterSessionStatusListener
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterRenderSyncService::NotifySessionClose(uint64 SessionId)
{
	// Unblock waiting threads to allow current Tick() finish
	BarrierSwap.Deactivate();

	FDisplayClusterService::NotifySessionClose(SessionId);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterSessionPacketHandler
//////////////////////////////////////////////////////////////////////////////////////////////
TSharedPtr<FDisplayClusterPacketInternal> FDisplayClusterRenderSyncService::ProcessPacket(const TSharedPtr<FDisplayClusterPacketInternal>& Request)
{
	// Check the pointer
	if (!Request.IsValid())
	{
		UE_LOG(LogDisplayClusterNetwork, Error, TEXT("%s - Invalid request data (nullptr)"), *GetName());
		return nullptr;
	}

	UE_LOG(LogDisplayClusterNetwork, Verbose, TEXT("%s - Processing packet: %s"), *GetName(), *Request->ToLogString());

	// Check protocol and type
	if (Request->GetProtocol() != DisplayClusterRenderSyncStrings::ProtocolName || Request->GetType() != DisplayClusterRenderSyncStrings::TypeRequest)
	{
		UE_LOG(LogDisplayClusterNetwork, Error, TEXT("%s - Unsupported packet type: %s"), *GetName(), *Request->ToLogString());
		return nullptr;
	}

	// Initialize response packet
	TSharedPtr<FDisplayClusterPacketInternal> Response = MakeShared<FDisplayClusterPacketInternal>(Request->GetName(), FString(DisplayClusterRenderSyncStrings::TypeResponse), Request->GetProtocol());

	// Dispatch the packet
	if (Request->GetName().Equals(DisplayClusterRenderSyncStrings::WaitForSwapSync::Name, ESearchCase::IgnoreCase))
	{
		WaitForSwapSync();
		return Response;
	}

	// Being here means that we have no appropriate dispatch logic for this packet
	UE_LOG(LogDisplayClusterNetwork, Warning, TEXT("%s - No dispatcher found for packet '%s'"), *GetName(), *Request->GetName());

	return nullptr;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProtocolRenderSync
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterRenderSyncService::WaitForSwapSync()
{
	if (BarrierSwap.Wait() != FDisplayClusterBarrier::WaitResult::Ok)
	{
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::NormalSoft, FString("Error on swap barrier. Exit required."));
	}
}
