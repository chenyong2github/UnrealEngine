// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/DisplayClusterBlueprintAPIImpl.h"

#include "IDisplayCluster.h"

#include "Cluster/DisplayClusterClusterEvent.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"
#include "Render/IDisplayClusterRenderManager.h"
#include "Render/Device/IDisplayClusterRenderDevice.h"
#include "DisplayClusterSceneViewExtensions.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterTypesConverter.h"
#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"

#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterScreenComponent.h"

#include "Render/Projection/IDisplayClusterProjectionPolicy.h"

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterEnums.h"

#include "Math/IntRect.h"

#include "CineCameraComponent.h"

#include "Render/Viewport/IDisplayClusterViewportManager.h"
#include "Render/Viewport/IDisplayClusterViewport.h"


//////////////////////////////////////////////////////////////////////////////////////////////
// DisplayCluster module API
//////////////////////////////////////////////////////////////////////////////////////////////
/** Return if the module has been initialized. */
bool UDisplayClusterBlueprintAPIImpl::IsModuleInitialized() const
{
	const bool bInitialized = IDisplayCluster::Get().IsModuleInitialized();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("IsModuleInitialized - %s"), *DisplayClusterTypesConverter::template ToString(bInitialized));
	return bInitialized;
}

EDisplayClusterOperationMode UDisplayClusterBlueprintAPIImpl::GetOperationMode() const
{
	const EDisplayClusterOperationMode OpMode = IDisplayCluster::Get().GetOperationMode();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("Operation mode - %s"), *DisplayClusterTypesConverter::template ToString(OpMode));
	return OpMode;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Cluster API
//////////////////////////////////////////////////////////////////////////////////////////////
bool UDisplayClusterBlueprintAPIImpl::IsMaster() const
{
	const bool bIsMaster = IDisplayCluster::Get().GetClusterMgr()->IsMaster();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("IsMaster - %s"), *DisplayClusterTypesConverter::template ToString(bIsMaster));
	return bIsMaster;
}

bool UDisplayClusterBlueprintAPIImpl::IsSlave() const
{
	const bool bIsSlave = IDisplayCluster::Get().GetClusterMgr()->IsSlave();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("IsSlave - %s"), *DisplayClusterTypesConverter::template ToString(bIsSlave));
	return bIsSlave;
}

bool UDisplayClusterBlueprintAPIImpl::IsBackup() const
{
	const bool bIsBackup = IDisplayCluster::Get().GetClusterMgr()->IsBackup();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("IsBackup - %s"), *DisplayClusterTypesConverter::template ToString(bIsBackup));
	return bIsBackup;
}

EDisplayClusterNodeRole UDisplayClusterBlueprintAPIImpl::GetClusterRole() const
{
	const EDisplayClusterNodeRole ClusterRole = IDisplayCluster::Get().GetClusterMgr()->GetClusterRole();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetClusterRole - %d"), static_cast<int32>(ClusterRole));
	return ClusterRole;
}

void UDisplayClusterBlueprintAPIImpl::GetNodeIds(TArray<FString>& OutNodeIds) const
{
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetNodeIds"));
	IDisplayCluster::Get().GetClusterMgr()->GetNodeIds(OutNodeIds);
}

FString UDisplayClusterBlueprintAPIImpl::GetNodeId() const
{
	const FString NodeId = IDisplayCluster::Get().GetClusterMgr()->GetNodeId();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetNodeId - NodeId=%s"), *NodeId);
	return NodeId;
}

int32 UDisplayClusterBlueprintAPIImpl::GetNodesAmount() const
{
	const int32 NodesAmount = IDisplayCluster::Get().GetClusterMgr()->GetNodesAmount();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetNodesAmount - %d"), NodesAmount);
	return NodesAmount;
}

void UDisplayClusterBlueprintAPIImpl::AddClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener)
{
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("AddClusterEventListener - adding cluster event listener..."));
	IDisplayCluster::Get().GetClusterMgr()->AddClusterEventListener(Listener);
}

void UDisplayClusterBlueprintAPIImpl::RemoveClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener)
{
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("RemoveClusterEventListener - removing cluster event listener..."));
	IDisplayCluster::Get().GetClusterMgr()->RemoveClusterEventListener(Listener);
}

void UDisplayClusterBlueprintAPIImpl::EmitClusterEventJson(const FDisplayClusterClusterEventJson& Event, bool bMasterOnly)
{
	UE_LOG(LogDisplayClusterBlueprint, Verbose,     TEXT("EmitClusterEventJson - emitting cluster event, bMasterOnly=%s"), *DisplayClusterTypesConverter::template ToString(bMasterOnly));
	UE_LOG(LogDisplayClusterBlueprint, VeryVerbose, TEXT("EmitClusterEventJson - emitting cluster event, Category='%s' Type='%s' Name='%s'"), *Event.Category, *Event.Type, *Event.Name);
	IDisplayCluster::Get().GetClusterMgr()->EmitClusterEventJson(Event, bMasterOnly);
}

void UDisplayClusterBlueprintAPIImpl::EmitClusterEventBinary(const FDisplayClusterClusterEventBinary& Event, bool bMasterOnly)
{
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("EmitClusterEventBinary - emitting cluster event, bMasterOnly=%s"), *DisplayClusterTypesConverter::template ToString(bMasterOnly));
	UE_LOG(LogDisplayClusterBlueprint, VeryVerbose, TEXT("EmitClusterEventBinary - emitting cluster event, EventId='%d'"), Event.EventId);
	IDisplayCluster::Get().GetClusterMgr()->EmitClusterEventBinary(Event, bMasterOnly);
}

void UDisplayClusterBlueprintAPIImpl::SendClusterEventJsonTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventJson& Event, bool bMasterOnly)
{
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("SendClusterEventJsonTo - sending json event to %s:%d"), *Address, Port);
	UE_LOG(LogDisplayClusterBlueprint, VeryVerbose, TEXT("SendClusterEventJsonTo - sending json event to %s:%d, Category='%s' Type='%s' Name='%s'"), *Address, Port, *Event.Category, *Event.Type, *Event.Name);
	IDisplayCluster::Get().GetClusterMgr()->SendClusterEventTo(Address, Port, Event, bMasterOnly);
}

void UDisplayClusterBlueprintAPIImpl::SendClusterEventBinaryTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventBinary& Event, bool bMasterOnly)
{
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("SendClusterEventBinaryTo - sending binary cluster event to %s:%d"), *Address, Port);
	UE_LOG(LogDisplayClusterBlueprint, VeryVerbose, TEXT("SendClusterEventBinaryTo - sending binary event to %s:%d, EventId='%d'"), *Address, Port, Event.EventId);
	IDisplayCluster::Get().GetClusterMgr()->SendClusterEventTo(Address, Port, Event, bMasterOnly);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Config API
//////////////////////////////////////////////////////////////////////////////////////////////
UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get config"), Category = "NDisplay|Config")
UDisplayClusterConfigurationData* UDisplayClusterBlueprintAPIImpl::GetConfig() const
{
	return const_cast<UDisplayClusterConfigurationData*>(IDisplayCluster::Get().GetConfigMgr()->GetConfig());
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Game API
//////////////////////////////////////////////////////////////////////////////////////////////
// Root
ADisplayClusterRootActor* UDisplayClusterBlueprintAPIImpl::GetRootActor() const
{
	ADisplayClusterRootActor* const RootActor = IDisplayCluster::Get().GetGameMgr()->GetRootActor();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetRootActor - %s"), RootActor ? *RootActor->GetHumanReadableName() : DisplayClusterStrings::log::NotFound);
	return RootActor;
}

// @todo: implement new BP api
// @todo: new BP api stuff before release
#if 0

//////////////////////////////////////////////////////////////////////////////////////////////
// New Render API protoype
//////////////////////////////////////////////////////////////////////////////////////////////

IDisplayClusterViewport* ImplFindViewport(const FString& ViewportId)
{
	IDisplayClusterRenderManager* DCRenderManager = IDisplayCluster::Get().GetRenderMgr();
	if (DCRenderManager && DCRenderManager->GetViewportManager())
	{
		return DCRenderManager->GetViewportManager()->FindViewport(ViewportId);
	}

	return nullptr;
}

const TArrayView<IDisplayClusterViewport*> ImplGetViewports()
{
	IDisplayClusterRenderManager* DCRenderManager = IDisplayCluster::Get().GetRenderMgr();
	if (DCRenderManager && DCRenderManager->GetViewportManager())
	{
		return DCRenderManager->GetViewportManager()->GetViewports();
	}

	return TArrayView<IDisplayClusterViewport*>();
}

FDisplayClusterViewportContext ImplGetViewportContext(const IDisplayClusterViewport& Viewport)
{
	FDisplayClusterViewportContext OutContext;

	OutContext.ViewportID = Viewport.GetId();

	OutContext.RectLocation = Viewport.GetRenderSettings().Rect.Min;
	OutContext.RectSize     = Viewport.GetRenderSettings().Rect.Size();

	const TArray<FDisplayClusterViewport_Context>& Contexts = Viewport.GetContexts();

	if (Contexts.Num() > 0)
	{
		OutContext.ViewLocation     = Contexts[0].ViewLocation;
		OutContext.ViewRotation     = Contexts[0].ViewRotation;
		OutContext.ProjectionMatrix = Contexts[0].ProjectionMatrix;

		OutContext.bIsRendering = Contexts[0].bDisableRender == false;
	}
	else
		{
		OutContext.bIsRendering = false;
		}

	return OutContext;
}

FDisplayClusterViewportStereoContext ImplGetViewportStereoContext(const IDisplayClusterViewport& Viewport)
{
	FDisplayClusterViewportStereoContext OutContext;

	OutContext.ViewportID = Viewport.GetId();

	OutContext.RectLocation = Viewport.GetRenderSettings().Rect.Min;
	OutContext.RectSize = Viewport.GetRenderSettings().Rect.Size();

	const TArray<FDisplayClusterViewport_Context>& Contexts = Viewport.GetContexts();

	if (Contexts.Num() > 0)
	{
		for (const FDisplayClusterViewport_Context& InContext : Contexts)
		{
			OutContext.ViewLocation.Add(InContext.ViewLocation);
			OutContext.ViewRotation.Add(InContext.ViewRotation);
			OutContext.ProjectionMatrix.Add(InContext.ProjectionMatrix);
	}

		OutContext.bIsRendering = Contexts[0].bDisableRender == false;
	}
	else
	{
		OutContext.bIsRendering = false;
	}

	return OutContext;
}

bool UDisplayClusterBlueprintAPIImpl::GetLocalViewportConfiguration(const FString& ViewportID, TWeakObjectPtr<UDisplayClusterConfigurationViewport>& ConfigurationViewport)
	{
	IDisplayCluster& DisplayCluster = IDisplayCluster::Get();

	ADisplayClusterRootActor* RootActor = DisplayCluster.GetGameMgr()->GetRootActor();
	if (RootActor)
	{
		return RootActor->GetLocalViewportConfiguration(DisplayCluster.GetConfigMgr()->GetLocalNodeId(), ViewportID, ConfigurationViewport);
	}

	return false;
}

/** Return local viewports names */
void UDisplayClusterBlueprintAPIImpl::GetLocalViewports(TArray<FString>& ViewportIDs) const
{
	const TArrayView<IDisplayClusterViewport*> LocalViewports = ImplGetViewports();
	ViewportIDs.Reserve(LocalViewports.Num());

	for (const IDisplayClusterViewport* Viewport : LocalViewports)
	{
		if (Viewport && Viewport->GetRenderSettings().bVisible)
		{
			ViewportIDs.Add(Viewport->GetId());
		}
	}
	}

/** Return local viewports runtime contexts */
void UDisplayClusterBlueprintAPIImpl::GetLocalViewportsContext(TArray<FDisplayClusterViewportContext>& ViewportContexts) const
{
	const TArrayView<IDisplayClusterViewport*> LocalViewports = ImplGetViewports();
	ViewportContexts.Reserve(LocalViewports.Num());

	for (const IDisplayClusterViewport* Viewport : LocalViewports)
	{
		if (Viewport && Viewport->GetRenderSettings().bVisible)
		{
			ViewportContexts.Add(ImplGetViewportContext(*Viewport));
		}
	}
}

/** Return local viewports runtime stereo contexts */
void UDisplayClusterBlueprintAPIImpl::GetLocalViewportsStereoContext(TArray<FDisplayClusterViewportStereoContext>& ViewportStereoContexts) const
{
	const TArrayView<IDisplayClusterViewport*> LocalViewports = ImplGetViewports();
	ViewportStereoContexts.Reserve(LocalViewports.Num());

	for (const IDisplayClusterViewport* Viewport : LocalViewports)
	{
		if (Viewport && Viewport->GetRenderSettings().bVisible)
		{
			ViewportStereoContexts.Add(ImplGetViewportStereoContext(*Viewport));
		}
	}
}

/** Return viewport runtime context (last frame viewport data) */
bool UDisplayClusterBlueprintAPIImpl::GetLocalViewportContext(const FString& ViewportId, FDisplayClusterViewportContext& ViewportContext) const
{
	IDisplayClusterViewport* DesiredViewport = ImplFindViewport(ViewportId);
	if (DesiredViewport)
	{
		ViewportContext = ImplGetViewportContext(*DesiredViewport);
		return true;
	}

	UE_LOG(LogDisplayClusterBlueprint, Warning, TEXT("Couldn't GetLocalViewportContext. Viewport '%s' not found"), *ViewportId);
	return false;
}

/** Return viewport stereo contexts (last frame viewport data) */
bool UDisplayClusterBlueprintAPIImpl::GetLocalViewportStereoContext(const FString& ViewportId, FDisplayClusterViewportStereoContext& ViewportStereoContext) const
{
	IDisplayClusterViewport* DesiredViewport = ImplFindViewport(ViewportId);
	if (DesiredViewport)
	{
		ViewportStereoContext = ImplGetViewportStereoContext(*DesiredViewport);
		return true;
	}

	UE_LOG(LogDisplayClusterBlueprint, Warning, TEXT("Couldn't GetLocalViewportStereoContext. Viewport '%s' not found"), *ViewportId);
	return false;
}
#endif
