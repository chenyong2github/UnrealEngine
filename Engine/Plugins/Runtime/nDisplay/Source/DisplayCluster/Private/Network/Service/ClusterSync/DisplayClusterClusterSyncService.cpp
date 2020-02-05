// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/Service/ClusterSync/DisplayClusterClusterSyncService.h"
#include "Network/Service/ClusterSync/DisplayClusterClusterSyncMsg.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Game/IPDisplayClusterGameManager.h"
#include "Config/IPDisplayClusterConfigManager.h"
#include "Input/IPDisplayClusterInputManager.h"

#include "Cluster/Controller/IPDisplayClusterNodeController.h"

#include "Network/Session/DisplayClusterSessionInternal.h"

#include "Misc/DisplayClusterAppExit.h"
#include "Misc/QualifiedFrameTime.h"

#include "DisplayClusterEnums.h"
#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"


FDisplayClusterClusterSyncService::FDisplayClusterClusterSyncService(const FString& InAddr, const int32 InPort) :
	FDisplayClusterService(FString("SRV_CS"), InAddr, InPort),
	BarrierGameStart  (GDisplayCluster->GetPrivateClusterMgr()->GetNodesAmount(), FString("GameStart_barrier"),  GDisplayCluster->GetConfigMgr()->GetConfigNetwork().BarrierGameStartWaitTimeout),
	BarrierFrameStart (GDisplayCluster->GetPrivateClusterMgr()->GetNodesAmount(), FString("FrameStart_barrier"), GDisplayCluster->GetConfigMgr()->GetConfigNetwork().BarrierWaitTimeout),
	BarrierFrameEnd   (GDisplayCluster->GetPrivateClusterMgr()->GetNodesAmount(), FString("FrameEnd_barrier"),   GDisplayCluster->GetConfigMgr()->GetConfigNetwork().BarrierWaitTimeout),
	BarrierTickEnd    (GDisplayCluster->GetPrivateClusterMgr()->GetNodesAmount(), FString("TickEnd_barrier"),    GDisplayCluster->GetConfigMgr()->GetConfigNetwork().BarrierWaitTimeout)
{
}


FDisplayClusterClusterSyncService::~FDisplayClusterClusterSyncService()
{
	Shutdown();
}


bool FDisplayClusterClusterSyncService::Start()
{
	BarrierGameStart.Activate();
	BarrierFrameStart.Activate();
	BarrierFrameEnd.Activate();
	BarrierTickEnd.Activate();

	return FDisplayClusterServer::Start();
}

void FDisplayClusterClusterSyncService::Shutdown()
{
	BarrierGameStart.Deactivate();
	BarrierFrameStart.Deactivate();
	BarrierFrameEnd.Deactivate();
	BarrierTickEnd.Deactivate();

	return FDisplayClusterServer::Shutdown();
}

TSharedPtr<FDisplayClusterSessionBase> FDisplayClusterClusterSyncService::CreateSession(FSocket* InSocket, const FIPv4Endpoint& InEP)
{
	return TSharedPtr<FDisplayClusterSessionBase>(new FDisplayClusterSessionInternal(InSocket, this, GetName() + FString("_session_") + InEP.ToString()));
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterSessionListener
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterSyncService::NotifySessionOpen(FDisplayClusterSessionBase* InSession)
{
	FDisplayClusterService::NotifySessionOpen(InSession);
}

void FDisplayClusterClusterSyncService::NotifySessionClose(FDisplayClusterSessionBase* InSession)
{
	// Unblock waiting threads to allow current Tick() finish
	BarrierGameStart.Deactivate();
	BarrierFrameStart.Deactivate();
	BarrierFrameEnd.Deactivate();
	BarrierTickEnd.Deactivate();

	FDisplayClusterService::NotifySessionClose(InSession);
	FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::ExitType::NormalSoft, GetName() + FString(" - Connection interrupted. Application exit requested."));
}

TSharedPtr<FDisplayClusterMessage> FDisplayClusterClusterSyncService::ProcessMessage(const TSharedPtr<FDisplayClusterMessage>& Request)
{
	// Check the pointer
	if (Request.IsValid() == false)
	{
		UE_LOG(LogDisplayClusterNetworkMsg, Error, TEXT("%s - Couldn't process the message"), *GetName());
		return nullptr;
	}

	UE_LOG(LogDisplayClusterNetwork, Verbose, TEXT("%s - Processing message %s"), *GetName(), *Request->ToString());

	// Check protocol and type
	if (Request->GetProtocol() != FDisplayClusterClusterSyncMsg::ProtocolName || Request->GetType() != FDisplayClusterClusterSyncMsg::TypeRequest)
	{
		UE_LOG(LogDisplayClusterNetworkMsg, Error, TEXT("%s - Unsupported message type: %s"), *GetName(), *Request->ToString());
		return nullptr;
	}

	// Initialize response message
	TSharedPtr<FDisplayClusterMessage> Response = MakeShareable(new FDisplayClusterMessage(Request->GetName(), FDisplayClusterClusterSyncMsg::TypeResponse, Request->GetProtocol()));

	// Dispatch the message
	const FString ReqName = Request->GetName();
	if (ReqName == FDisplayClusterClusterSyncMsg::WaitForGameStart::name)
	{
		WaitForGameStart();
		return Response;
	}
	else if (ReqName == FDisplayClusterClusterSyncMsg::WaitForFrameStart::name)
	{
		WaitForFrameStart();
		return Response;
	}
	else if (ReqName == FDisplayClusterClusterSyncMsg::WaitForFrameEnd::name)
	{
		WaitForFrameEnd();
		return Response;
	}
	else if (ReqName == FDisplayClusterClusterSyncMsg::WaitForTickEnd::name)
	{
		WaitForTickEnd();
		return Response;
	}
	else if (ReqName == FDisplayClusterClusterSyncMsg::GetDeltaTime::name)
	{
		float DeltaSeconds = 0.0f;
		GetDeltaTime(DeltaSeconds);
		Response->SetArg(FDisplayClusterClusterSyncMsg::GetDeltaTime::argDeltaSeconds, DeltaSeconds);
		return Response;
	}
	else if (ReqName == FDisplayClusterClusterSyncMsg::GetFrameTime::name)
	{
		TOptional<FQualifiedFrameTime> frameTime;
		GetFrameTime(frameTime);

		Response->SetArg(FDisplayClusterClusterSyncMsg::GetFrameTime::argIsValid, frameTime.IsSet());
		if (frameTime.IsSet())
		{
			Response->SetArg(FDisplayClusterClusterSyncMsg::GetFrameTime::argFrameTime, frameTime.GetValue());
		}
		return Response;
	}
	else if (ReqName == FDisplayClusterClusterSyncMsg::GetSyncData::name)
	{
		FDisplayClusterMessage::DataType SyncData;
		int SyncGroupNum = 0;
		Request->GetArg(FDisplayClusterClusterSyncMsg::GetSyncData::argSyncGroup, SyncGroupNum);
		EDisplayClusterSyncGroup SyncGroup = (EDisplayClusterSyncGroup)SyncGroupNum;

		GetSyncData(SyncData, SyncGroup);

		Response->SetArgs(SyncData);
		return Response;
	}
	else if (ReqName == FDisplayClusterClusterSyncMsg::GetInputData::name)
	{
		FDisplayClusterMessage::DataType InputData;
		GetInputData(InputData);

		Response->SetArgs(InputData);
		return Response;
	}
	else if (ReqName == FDisplayClusterClusterSyncMsg::GetEventsData::name)
	{
		FDisplayClusterMessage::DataType EventsData;
		GetEventsData(EventsData);

		Response->SetArgs(EventsData);
		return Response;
	}
	else if (ReqName == FDisplayClusterClusterSyncMsg::GetNativeInputData::name)
	{
		FDisplayClusterMessage::DataType NativeInputData;
		GetNativeInputData(NativeInputData);

		Response->SetArgs(NativeInputData);
		return Response;
	}

	// Being here means that we have no appropriate dispatch logic for this message
	UE_LOG(LogDisplayClusterNetworkMsg, Warning, TEXT("%s - A dispatcher for this message hasn't been implemented yet <%s>"), *GetName(), *Request->ToString());
	return nullptr;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IPDisplayClusterClusterSyncProtocol
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterClusterSyncService::WaitForGameStart()
{
	if (BarrierGameStart.Wait() != FDisplayClusterBarrier::WaitResult::Ok)
	{
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::ExitType::NormalSoft, FString("Error on game start barrier. Exit required."));
	}
}

void FDisplayClusterClusterSyncService::WaitForFrameStart()
{
	if (BarrierFrameStart.Wait() != FDisplayClusterBarrier::WaitResult::Ok)
	{
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::ExitType::NormalSoft, FString("Error on frame start barrier. Exit required."));
	}
}

void FDisplayClusterClusterSyncService::WaitForFrameEnd()
{
	if (BarrierFrameEnd.Wait() != FDisplayClusterBarrier::WaitResult::Ok)
	{
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::ExitType::NormalSoft, FString("Error on frame end barrier. Exit required."));
	}
}

void FDisplayClusterClusterSyncService::WaitForTickEnd()
{
	if (BarrierTickEnd.Wait() != FDisplayClusterBarrier::WaitResult::Ok)
	{
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::ExitType::NormalSoft, FString("Error on tick end barrier. Exit required."));
	}
}

void FDisplayClusterClusterSyncService::GetDeltaTime(float& DeltaSeconds)
{
	static IPDisplayClusterNodeController* const NodeController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	return NodeController->GetDeltaTime(DeltaSeconds);
}

void FDisplayClusterClusterSyncService::GetFrameTime(TOptional<FQualifiedFrameTime>& FrameTime)
{
	static IPDisplayClusterNodeController* const NodeController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	return NodeController->GetFrameTime(FrameTime);
}

void FDisplayClusterClusterSyncService::GetSyncData(FDisplayClusterMessage::DataType& SyncData, EDisplayClusterSyncGroup SyncGroup)
{
	static IPDisplayClusterNodeController* const NodeController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	NodeController->GetSyncData(SyncData, SyncGroup);
}

void FDisplayClusterClusterSyncService::GetInputData(FDisplayClusterMessage::DataType& InputData)
{
	static IPDisplayClusterNodeController* const NodeController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	return NodeController->GetInputData(InputData);
}

void FDisplayClusterClusterSyncService::GetEventsData(FDisplayClusterMessage::DataType& EventsData)
{
	GDisplayCluster->GetPrivateClusterMgr()->ExportEventsData(EventsData);
}

void FDisplayClusterClusterSyncService::GetNativeInputData(FDisplayClusterMessage::DataType& NativeInputData)
{
	static IPDisplayClusterNodeController* const NodeController = GDisplayCluster->GetPrivateClusterMgr()->GetController();
	return NodeController->GetNativeInputData(NativeInputData);
}
