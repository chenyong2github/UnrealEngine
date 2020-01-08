// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterGameEngine.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Cluster/Controller/IPDisplayClusterNodeController.h"
#include "Config/IPDisplayClusterConfigManager.h"
#include "Input/IPDisplayClusterInputManager.h"

#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DisplayClusterAppExit.h"
#include "Misc/Parse.h"

#include "Misc/DisplayClusterHelpers.h"

#include "DisplayClusterBuildConfig.h"
#include "DisplayClusterEnums.h"
#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"
#include "DisplayClusterStrings.h"

#include "Stats/Stats.h"

#include "Misc/CoreDelegates.h"


void UDisplayClusterGameEngine::Init(class IEngineLoop* InEngineLoop)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterEngine);

	// Detect requested operation mode
	OperationMode = DetectOperationMode();

	if (OperationMode == EDisplayClusterOperationMode::Cluster ||
		OperationMode == EDisplayClusterOperationMode::Standalone)
	{
		// Instantiate the tickable helper
		TickableHelper = NewObject<UDisplayClusterGameEngineTickableHelper>();
	}

	// Initialize Display Cluster
	if (!GDisplayCluster->Init(OperationMode))
	{
		FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::ExitType::KillImmediately, FString("Couldn't initialize DisplayCluster module"));
	}

	FString cfgPath;
	FString nodeId;

	if (OperationMode == EDisplayClusterOperationMode::Cluster)
	{
		// Extract config path from command line
		if (!FParse::Value(FCommandLine::Get(), DisplayClusterStrings::args::Config, cfgPath))
		{
			UE_LOG(LogDisplayClusterEngine, Error, TEXT("No config file specified"));
			FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::ExitType::KillImmediately, FString("Cluster mode requires config file"));
		}

		// Extract node ID from command line
		if (!FParse::Value(FCommandLine::Get(), DisplayClusterStrings::args::Node, nodeId))
		{
#ifdef DISPLAY_CLUSTER_USE_AUTOMATIC_NODE_ID_RESOLVE
			UE_LOG(LogDisplayClusterEngine, Log, TEXT("Node ID is not specified"));
#else
			UE_LOG(LogDisplayClusterEngine, Warning, TEXT("Node ID is not specified"));
			FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::ExitType::KillImmediately, FString("Cluster mode requires node ID"));
#endif
		}
	}
	else if (OperationMode == EDisplayClusterOperationMode::Standalone)
	{
#ifdef DISPLAY_CLUSTER_USE_DEBUG_STANDALONE_CONFIG
		// Save config path from command line
		cfgPath = DisplayClusterStrings::misc::DbgStubConfig;
		nodeId  = DisplayClusterStrings::misc::DbgStubNodeId;
#endif
	}

	if (OperationMode == EDisplayClusterOperationMode::Cluster ||
		OperationMode == EDisplayClusterOperationMode::Standalone)
	{
		DisplayClusterHelpers::str::TrimStringValue(cfgPath);
		DisplayClusterHelpers::str::TrimStringValue(nodeId);

		// Start game session
		if (!GDisplayCluster->StartSession(cfgPath, nodeId))
		{
			FDisplayClusterAppExit::ExitApplication(FDisplayClusterAppExit::ExitType::KillImmediately, FString("Couldn't start DisplayCluster session"));
		}

		// Initialize internals
		InitializeInternals();
	}

	// Initialize base stuff.
	UGameEngine::Init(InEngineLoop);
}

EDisplayClusterOperationMode UDisplayClusterGameEngine::DetectOperationMode()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterEngine);

	EDisplayClusterOperationMode OpMode = EDisplayClusterOperationMode::Disabled;
	if (FParse::Param(FCommandLine::Get(), DisplayClusterStrings::args::Cluster))
	{
		OpMode = EDisplayClusterOperationMode::Cluster;
	}
	else if (FParse::Param(FCommandLine::Get(), DisplayClusterStrings::args::Standalone))
	{
		OpMode = EDisplayClusterOperationMode::Standalone;
	}

	UE_LOG(LogDisplayClusterEngine, Log, TEXT("Detected operation mode: %s"), *FDisplayClusterTypesConverter::ToString(OpMode));

	return OpMode;
}

bool UDisplayClusterGameEngine::InitializeInternals()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterEngine);

	// Store debug settings locally
	CfgDebug = GDisplayCluster->GetPrivateConfigMgr()->GetConfigDebug();
	
	InputMgr       = GDisplayCluster->GetPrivateInputMgr();
	ClusterMgr     = GDisplayCluster->GetPrivateClusterMgr();
	NodeController = ClusterMgr->GetController();

	FDisplayClusterConfigClusterNode LocalClusterNode;
	if (DisplayClusterHelpers::config::GetLocalClusterNode(LocalClusterNode))
	{
		UE_LOG(LogDisplayClusterEngine, Log, TEXT("Configuring sound enabled: %s"), *FDisplayClusterTypesConverter::ToString(LocalClusterNode.SoundEnabled));
		bUseSound = LocalClusterNode.SoundEnabled;
	}

	check(ClusterMgr);
	check(InputMgr);
		
	return true;
}

void UDisplayClusterGameEngine::PreExit()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterEngine);

	if (OperationMode == EDisplayClusterOperationMode::Cluster ||
		OperationMode == EDisplayClusterOperationMode::Standalone)
	{
		// Close current DisplayCluster session
		GDisplayCluster->EndSession();
	}

	// Release the engine
	UGameEngine::PreExit();
}

bool UDisplayClusterGameEngine::LoadMap(FWorldContext& WorldContext, FURL URL, class UPendingNetGame* Pending, FString& Error)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterEngine);

	if (OperationMode == EDisplayClusterOperationMode::Cluster ||
		OperationMode == EDisplayClusterOperationMode::Standalone)
	{
		// Finish previous scene
		GDisplayCluster->EndScene();

		// Perform map loading
		if (!Super::LoadMap(WorldContext, URL, Pending, Error))
		{
			return false;
		}

		// Start new scene
		GDisplayCluster->StartScene(WorldContext.World());

		// Game start barrier
		if (NodeController)
		{
			NodeController->WaitForGameStart();
		}
	}
	else
	{
		return Super::LoadMap(WorldContext, URL, Pending, Error);
	}

	return true;
}

void UDisplayClusterGameEngine::Tick(float DeltaSeconds, bool bIdleMode)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterEngine);

	if (OperationMode == EDisplayClusterOperationMode::Cluster ||
		OperationMode == EDisplayClusterOperationMode::Standalone)
	{
		FTimecode Timecode;
		FFrameRate FrameRate;

		//////////////////////////////////////////////////////////////////////////////////////////////
		// Frame start barrier
		NodeController->WaitForFrameStart();
		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("Sync frame start"));

		// Perform StartFrame notification
		GDisplayCluster->StartFrame(GFrameCounter);

		// Sync DeltaSeconds
		NodeController->GetDeltaTime(DeltaSeconds);
		FApp::SetDeltaTime(DeltaSeconds);
		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("DisplayCluster delta seconds: %f"), DeltaSeconds);

		// Sync timecode and framerate
		NodeController->GetTimecode(Timecode, FrameRate);
		FApp::SetTimecodeAndFrameRate(Timecode, FrameRate);
		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("DisplayCluster timecode: %s | %s"), *Timecode.ToString(), *FrameRate.ToPrettyText().ToString());

		// Perform PreTick for DisplayCluster module
		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("Perform PreTick()"));
		GDisplayCluster->PreTick(DeltaSeconds);

		// Perform UGameEngine::Tick() calls for scene actors
		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("Perform UGameEngine::Tick()"));
		Super::Tick(DeltaSeconds, bIdleMode);

		// Perform PostTick for DisplayCluster module
		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("Perform PostTick()"));
		GDisplayCluster->PostTick(DeltaSeconds);

		if (CfgDebug.LagSimulateEnabled)
		{
			const float lag = CfgDebug.LagMaxTime;
			UE_LOG(LogDisplayClusterEngine, Log, TEXT("Simulating lag: %f seconds"), lag);
#if 1
			FPlatformProcess::Sleep(FMath::RandRange(0.f, lag));
#else
			FPlatformProcess::Sleep(lag);
#endif
		}

#if 0
		//////////////////////////////////////////////////////////////////////////////////////////////
		// Tick end barrier
		NodeController->WaitForTickEnd();
#endif

		//////////////////////////////////////////////////////////////////////////////////////////////
		// Frame end barrier
		NodeController->WaitForFrameEnd();

		// Perform EndFrame notification
		GDisplayCluster->EndFrame(GFrameCounter);

		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("Sync frame end"));
	}
	else
	{
		Super::Tick(DeltaSeconds, bIdleMode);
	}
}

TStatId UDisplayClusterGameEngineTickableHelper::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDisplayClusterGameEngineTickableHelper, STATGROUP_Tickables);
}

void UDisplayClusterGameEngineTickableHelper::Tick(float DeltaSeconds)
{
	static const EDisplayClusterOperationMode OperationMode = GDisplayCluster->GetOperationMode();

	if (OperationMode == EDisplayClusterOperationMode::Cluster ||
		OperationMode == EDisplayClusterOperationMode::Standalone)
	{
		UE_LOG(LogDisplayClusterEngine, Verbose, TEXT("Perform Tick()"));
		GDisplayCluster->Tick(DeltaSeconds);
	}
}
