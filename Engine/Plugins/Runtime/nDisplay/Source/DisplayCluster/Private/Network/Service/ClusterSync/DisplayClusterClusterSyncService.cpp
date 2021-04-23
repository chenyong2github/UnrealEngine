// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/Service/ClusterSync/DisplayClusterClusterSyncService.h"
#include "Network/Service/ClusterSync/DisplayClusterClusterSyncStrings.h"
#include "Network/Conversion/DisplayClusterNetworkDataConversion.h"
#include "Network/Session/DisplayClusterSession.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Game/IPDisplayClusterGameManager.h"
#include "Config/IPDisplayClusterConfigManager.h"

#include "Cluster/Controller/IDisplayClusterNodeController.h"
#include "Cluster/DisplayClusterClusterEvent.h"

#include "DisplayClusterConfigurationTypes.h"

#include "Misc/DisplayClusterAppExit.h"
#include "Misc/QualifiedFrameTime.h"
#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterLog.h"

#include "DisplayClusterEnums.h"

#include "Interfaces/IPv4/IPv4Endpoint.h"


FDisplayClusterClusterSyncService::FDisplayClusterClusterSyncService()
	: FDisplayClusterService(FString("SRV_CS"))
	, BarrierGameStart  (GDisplayCluster->GetPrivateClusterMgr()->GetNodesAmount(), FString("GameStart_barrier"),  GDisplayCluster->GetConfigMgr()->GetConfig()->Cluster->Network.GameStartBarrierTimeout)
	, BarrierFrameStart (GDisplayCluster->GetPrivateClusterMgr()->GetNodesAmount(), FString("FrameStart_barrier"), GDisplayCluster->GetConfigMgr()->GetConfig()->Cluster->Network.FrameStartBarrierTimeout)
	, BarrierFrameEnd   (GDisplayCluster->GetPrivateClusterMgr()->GetNodesAmount(), FString("FrameEnd_barrier"),   GDisplayCluster->GetConfigMgr()->GetConfig()->Cluster->Network.FrameEndBarrierTimeout)
{
}

FDisplayClusterClusterSyncService::~FDisplayClusterClusterSyncService()
{
	Shutdown();
}


bool FDisplayClusterClusterSyncService::Start(const FString& Address, int32 Port)
{
	BarrierGameStart.Activate();
	BarrierFrameStart.Activate();
	BarrierFrameEnd.Activate();

	return FDisplayClusterServer::Start(Address, Port);
}

void FDisplayClusterClusterSyncService::Shutdown()
{
	BarrierGameStart.Deactivate();
	BarrierFrameStart.Deactivate();
	BarrierFrameEnd.Deactivate();

	return FDisplayClusterServer::Shutdown();
}

TUniquePtr<IDisplayClusterSession> FDisplayClusterClusterSyncService::CreateSession(FSocket* Socket, const FIPv4Endpoint& Endpoint, uint64 SessionId)
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
// IDisplayClusterSessionListener
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterSyncService::NotifySessionClose(uint64 SessionId)
{
	// Unblock waiting threads to allow current Tick() finish
	BarrierGameStart.Deactivate();
	BarrierFrameStart.Deactivate();
	BarrierFrameEnd.Deactivate();

	FDisplayClusterService::NotifySessionClose(SessionId);
}

TSharedPtr<FDisplayClusterPacketInternal> FDisplayClusterClusterSyncService::ProcessPacket(const TSharedPtr<FDisplayClusterPacketInternal>& Request)
{
	// Check the pointer
	if (!Request.IsValid())
	{
		UE_LOG(LogDisplayClusterNetwork, Error, TEXT("%s - Invalid request data (nullptr)"), *GetName());
		return nullptr;
	}

	UE_LOG(LogDisplayClusterNetwork, Verbose, TEXT("%s - Processing packet: %s"), *GetName(), *Request->ToLogString());

	// Check protocol and type
	if (Request->GetProtocol() != DisplayClusterClusterSyncStrings::ProtocolName || Request->GetType() != DisplayClusterClusterSyncStrings::TypeRequest)
	{
		UE_LOG(LogDisplayClusterNetwork, Error, TEXT("%s - Unsupported packet type: %s"), *GetName(), *Request->ToLogString());
		return nullptr;
	}

	// Initialize response packet
	TSharedPtr<FDisplayClusterPacketInternal> Response = MakeShared<FDisplayClusterPacketInternal>(Request->GetName(), DisplayClusterClusterSyncStrings::TypeResponse, Request->GetProtocol());

	// Dispatch the packet
	const FString ReqName = Request->GetName();
	if (ReqName == DisplayClusterClusterSyncStrings::WaitForGameStart::Name)
	{
		WaitForGameStart();
		return Response;
	}
	else if (ReqName == DisplayClusterClusterSyncStrings::WaitForFrameStart::Name)
	{
		WaitForFrameStart();
		return Response;
	}
	else if (ReqName == DisplayClusterClusterSyncStrings::WaitForFrameEnd::Name)
	{
		WaitForFrameEnd();
		return Response;
	}
	else if (ReqName == DisplayClusterClusterSyncStrings::GetTimeData::Name)
	{
		float DeltaTime = 0.0f;
		double GameTime = 0.0f;
		TOptional<FQualifiedFrameTime> FrameTime;

		GetTimeData(DeltaTime, GameTime, FrameTime);

		// Convert to hex strings
		const FString StrDeltaTime = DisplayClusterTypesConverter::template ToHexString<float> (DeltaTime);
		const FString StrGameTime  = DisplayClusterTypesConverter::template ToHexString<double>(GameTime);

		Response->SetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::GetTimeData::ArgDeltaTime, StrDeltaTime);
		Response->SetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::GetTimeData::ArgGameTime, StrGameTime);
		Response->SetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::GetTimeData::ArgIsFrameTimeValid, FrameTime.IsSet());
		
		if (FrameTime.IsSet())
		{
			Response->SetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::GetTimeData::ArgFrameTime, FrameTime.GetValue());
		}

		return Response;
	}
	else if (ReqName == DisplayClusterClusterSyncStrings::GetSyncData::Name)
	{
		TMap<FString, FString> SyncData;
		int SyncGroupNum = 0;
		Request->GetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::GetSyncData::ArgSyncGroup, SyncGroupNum);
		EDisplayClusterSyncGroup SyncGroup = (EDisplayClusterSyncGroup)SyncGroupNum;

		GetSyncData(SyncData, SyncGroup);

		Response->SetTextArgs(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, SyncData);
		return Response;
	}
	else if (ReqName == DisplayClusterClusterSyncStrings::GetEventsData::Name)
	{
		TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>   JsonEvents;
		TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>> BinaryEvents;
		
		GetEventsData(JsonEvents, BinaryEvents);

		DisplayClusterNetworkDataConversion::JsonEventsToInternalPacket(JsonEvents, Response);
		DisplayClusterNetworkDataConversion::BinaryEventsToInternalPacket(BinaryEvents, Response);

		return Response;
	}
	else if (ReqName == DisplayClusterClusterSyncStrings::GetNativeInputData::Name)
	{
		TMap<FString, FString> NativeInputData;
		GetNativeInputData(NativeInputData);

		Response->SetTextArgs(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, NativeInputData);
		return Response;
	}

	// Being here means that we have no appropriate dispatch logic for this packet
	UE_LOG(LogDisplayClusterNetwork, Warning, TEXT("%s - No dispatcher found for packet '%s'"), *GetName(), *Request->GetName());

	return nullptr;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterProtocolClusterSync
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterSyncService::WaitForGameStart()
{
	if (BarrierGameStart.Wait() != FDisplayClusterBarrier::WaitResult::Ok)
	{
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::NormalSoft, FString("Error/timeout on game start barrier. Exit required."));
	}
}

void FDisplayClusterClusterSyncService::WaitForFrameStart()
{
	if (BarrierFrameStart.Wait() != FDisplayClusterBarrier::WaitResult::Ok)
	{
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::NormalSoft, FString("Error/timeout on frame start barrier. Exit required."));
	}
}

void FDisplayClusterClusterSyncService::WaitForFrameEnd()
{
	if (BarrierFrameEnd.Wait() != FDisplayClusterBarrier::WaitResult::Ok)
	{
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::NormalSoft, FString("Error/timeout on frame end barrier. Exit required."));
	}
}

void FDisplayClusterClusterSyncService::GetTimeData(float& InOutDeltaTime, double& InOutGameTime, TOptional<FQualifiedFrameTime>& InOutFrameTime)
{
	static IDisplayClusterNodeController* const NodeController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	return NodeController->GetTimeData(InOutDeltaTime, InOutGameTime, InOutFrameTime);
}

void FDisplayClusterClusterSyncService::GetSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup)
{
	static IDisplayClusterNodeController* const NodeController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	NodeController->GetSyncData(SyncData, SyncGroup);
}

void FDisplayClusterClusterSyncService::GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson, ESPMode::ThreadSafe>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary, ESPMode::ThreadSafe>>& BinaryEvents)
{
	static IDisplayClusterNodeController* const NodeController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	return NodeController->GetEventsData(JsonEvents, BinaryEvents);
}

void FDisplayClusterClusterSyncService::GetNativeInputData(TMap<FString, FString>& NativeInputData)
{
	static IDisplayClusterNodeController* const NodeController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	return NodeController->GetNativeInputData(NativeInputData);
}
