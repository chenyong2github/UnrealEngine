// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/Service/ClusterSync/DisplayClusterClusterSyncService.h"
#include "Network/Service/ClusterSync/DisplayClusterClusterSyncStrings.h"
#include "Network/Conversion/DisplayClusterNetworkDataConversion.h"
#include "Network/Session/DisplayClusterSession.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Game/IPDisplayClusterGameManager.h"
#include "Config/IPDisplayClusterConfigManager.h"
#include "Input/IPDisplayClusterInputManager.h"

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
	return MakeUnique<FDisplayClusterSession<FDisplayClusterPacketInternal, true, true>>(Socket, this, this, SessionId, FString::Printf(TEXT("%s_session_%lu_%s"), *GetName(), SessionId, *Endpoint.ToString()));
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
		double ThreadTime  = 0.f;
		double BarrierTime = 0.f;

		WaitForGameStart(&ThreadTime, &BarrierTime);

		Response->SetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::WaitForGameStart::ArgThreadTime,  ThreadTime);
		Response->SetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::WaitForGameStart::ArgBarrierTime, BarrierTime);

		return Response;
	}
	else if (ReqName == DisplayClusterClusterSyncStrings::WaitForFrameStart::Name)
	{
		double ThreadTime  = 0.f;
		double BarrierTime = 0.f;

		WaitForFrameStart(&ThreadTime, &BarrierTime);

		Response->SetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::WaitForGameStart::ArgThreadTime,  ThreadTime);
		Response->SetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::WaitForGameStart::ArgBarrierTime, BarrierTime);

		return Response;
	}
	else if (ReqName == DisplayClusterClusterSyncStrings::WaitForFrameEnd::Name)
	{
		double ThreadTime  = 0.f;
		double BarrierTime = 0.f;

		WaitForFrameEnd(&ThreadTime, &BarrierTime);

		Response->SetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::WaitForGameStart::ArgThreadTime,  ThreadTime);
		Response->SetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::WaitForGameStart::ArgBarrierTime, BarrierTime);

		return Response;
	}
	else if (ReqName == DisplayClusterClusterSyncStrings::GetDeltaTime::Name)
	{
		// Get delta (float)
		float DeltaSeconds = 0.0f;
		GetDeltaTime(DeltaSeconds);

		// Convert to hex string
		const FString StrDeltaSeconds = DisplayClusterTypesConverter::template ToHexString(DeltaSeconds);

		// Send the response
		Response->SetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::GetDeltaTime::ArgDeltaSeconds, StrDeltaSeconds);
		return Response;
	}
	else if (ReqName == DisplayClusterClusterSyncStrings::GetFrameTime::Name)
	{
		TOptional<FQualifiedFrameTime> frameTime;
		GetFrameTime(frameTime);

		Response->SetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::GetFrameTime::ArgIsValid, frameTime.IsSet());
		if (frameTime.IsSet())
		{
			Response->SetTextArg(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, DisplayClusterClusterSyncStrings::GetFrameTime::ArgFrameTime, frameTime.GetValue());
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
	else if (ReqName == DisplayClusterClusterSyncStrings::GetInputData::Name)
	{
		TMap<FString, FString> InputData;
		GetInputData(InputData);

		Response->SetTextArgs(DisplayClusterClusterSyncStrings::ArgumentsDefaultCategory, InputData);
		return Response;
	}
	else if (ReqName == DisplayClusterClusterSyncStrings::GetEventsData::Name)
	{
		TArray<TSharedPtr<FDisplayClusterClusterEventJson>>   JsonEvents;
		TArray<TSharedPtr<FDisplayClusterClusterEventBinary>> BinaryEvents;
		
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
void FDisplayClusterClusterSyncService::WaitForGameStart(double* ThreadWaitTime, double* BarrierWaitTime)
{
	if (BarrierGameStart.Wait(ThreadWaitTime, BarrierWaitTime) != FDisplayClusterBarrier::WaitResult::Ok)
	{
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::NormalSoft, FString("Error/timeout on game start barrier. Exit required."));
	}
}

void FDisplayClusterClusterSyncService::WaitForFrameStart(double* ThreadWaitTime, double* BarrierWaitTime)
{
	if (BarrierFrameStart.Wait(ThreadWaitTime, BarrierWaitTime) != FDisplayClusterBarrier::WaitResult::Ok)
	{
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::NormalSoft, FString("Error/timeout on frame start barrier. Exit required."));
	}
}

void FDisplayClusterClusterSyncService::WaitForFrameEnd(double* ThreadWaitTime, double* BarrierWaitTime)
{
	if (BarrierFrameEnd.Wait(ThreadWaitTime, BarrierWaitTime) != FDisplayClusterBarrier::WaitResult::Ok)
	{
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::EExitType::NormalSoft, FString("Error/timeout on frame end barrier. Exit required."));
	}
}

void FDisplayClusterClusterSyncService::GetDeltaTime(float& DeltaSeconds)
{
	static IDisplayClusterNodeController* const NodeController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	return NodeController->GetDeltaTime(DeltaSeconds);
}

void FDisplayClusterClusterSyncService::GetFrameTime(TOptional<FQualifiedFrameTime>& FrameTime)
{
	static IDisplayClusterNodeController* const NodeController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	return NodeController->GetFrameTime(FrameTime);
}

void FDisplayClusterClusterSyncService::GetSyncData(TMap<FString, FString>& SyncData, EDisplayClusterSyncGroup SyncGroup)
{
	static IDisplayClusterNodeController* const NodeController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	NodeController->GetSyncData(SyncData, SyncGroup);
}

void FDisplayClusterClusterSyncService::GetInputData(TMap<FString, FString>& InputData)
{
	static IDisplayClusterNodeController* const NodeController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	return NodeController->GetInputData(InputData);
}

void FDisplayClusterClusterSyncService::GetEventsData(TArray<TSharedPtr<FDisplayClusterClusterEventJson>>& JsonEvents, TArray<TSharedPtr<FDisplayClusterClusterEventBinary>>& BinaryEvents)
{
	static IDisplayClusterNodeController* const NodeController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	return NodeController->GetEventsData(JsonEvents, BinaryEvents);
}

void FDisplayClusterClusterSyncService::GetNativeInputData(TMap<FString, FString>& NativeInputData)
{
	static IDisplayClusterNodeController* const NodeController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	return NodeController->GetNativeInputData(NativeInputData);
}
