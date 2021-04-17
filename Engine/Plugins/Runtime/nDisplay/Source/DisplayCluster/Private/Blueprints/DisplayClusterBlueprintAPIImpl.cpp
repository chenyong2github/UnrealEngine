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
#include "Components/DisplayClusterMeshComponent.h"
#include "Components/DisplayClusterSceneComponent.h"
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
	const bool bIsSlave = !IsMaster();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("IsSlave - %s"), *DisplayClusterTypesConverter::template ToString(bIsSlave));
	return bIsSlave;
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
UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get config"), Category = "DisplayCluster|Config")
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


//////////////////////////////////////////////////////////////////////////////////////////////
// Render API
//////////////////////////////////////////////////////////////////////////////////////////////

IDisplayClusterViewport* ImplFindViewport(const FString& ViewportId)
{
	IDisplayClusterRenderManager* DCRenderManager = IDisplayCluster::Get().GetRenderMgr();
	if (DCRenderManager)
	{
		IDisplayClusterRenderDevice* DCRenderDevice = DCRenderManager->GetRenderDevice();
		if (DCRenderDevice)
		{
			return DCRenderDevice->GetViewportManager().FindViewport(ViewportId);
		}
	}

	return nullptr;
}

const TArrayView<IDisplayClusterViewport*> ImplGetViewports()
{
	IDisplayClusterRenderManager* DCRenderManager = IDisplayCluster::Get().GetRenderMgr();
	if (DCRenderManager)
	{
		IDisplayClusterRenderDevice* DCRenderDevice = DCRenderManager->GetRenderDevice();
		if (DCRenderDevice)
		{
			return DCRenderDevice->GetViewportManager().GetViewports();
		}
	}

	return TArrayView<IDisplayClusterViewport*>();
}

void UDisplayClusterBlueprintAPIImpl::SetViewportCamera(const FString& CameraId, const FString& ViewportId)
{
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("SetViewportCamera - assigning camera '%s' to viewport '%s'"), *CameraId, *ViewportId);

	// Assign to all viewports if camera ID is empty (default camera will be used by all viewports)
	if (ViewportId.IsEmpty())
	{
		for (IDisplayClusterViewport* Viewport : ImplGetViewports())
		{
			Viewport->GetRenderSettings().CameraId = CameraId;
		}

		UE_LOG(LogDisplayClusterBlueprint, Log, TEXT("Camera '%s' was assigned to all viewports"), *CameraId);

		return;
	}

	IDisplayClusterViewport* DesiredViewport = ImplFindViewport(ViewportId);

	// Check if requested viewport exists
	if (DesiredViewport)
	{
		DesiredViewport->GetRenderSettings().CameraId = CameraId;
		UE_LOG(LogDisplayClusterBlueprint, Log, TEXT("Camera '%s' was assigned to '%s' viewport"), *CameraId, *ViewportId);

		return;
	}

	UE_LOG(LogDisplayClusterBlueprint, Warning, TEXT("Couldn't assign '%s' camera. Viewport '%s' not found"), *CameraId, *ViewportId);
}

bool UDisplayClusterBlueprintAPIImpl::GetBufferRatio(const FString& InViewportID, float& OutBufferRatio) const
{
	IDisplayClusterViewport* DesiredViewport = ImplFindViewport(InViewportID);
	if (DesiredViewport)
	{
		OutBufferRatio = DesiredViewport->GetRenderSettings().BufferRatio;
		UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("Viewport '%s' has buffer ratio %f"), *InViewportID, OutBufferRatio);
		return true;
	}

	UE_LOG(LogDisplayClusterBlueprint, Warning, TEXT("Couldn't get buffer ratio. Viewport '%s' not found"), *InViewportID);
	return false;
}

bool UDisplayClusterBlueprintAPIImpl::SetBufferRatio(const FString& InViewportID, float InBufferRatio)
{
	IDisplayClusterViewport* DesiredViewport = ImplFindViewport(InViewportID);
	if (DesiredViewport)
	{
		UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("Set buffer ratio %f for viewport '%s'"), InBufferRatio, *InViewportID);

		DesiredViewport->GetRenderSettings().BufferRatio = InBufferRatio;
		return true;
	}

	UE_LOG(LogDisplayClusterBlueprint, Warning, TEXT("Couldn't set buffer ratio. Viewport '%s' not found"), *InViewportID);
	return false;
}

void UDisplayClusterBlueprintAPIImpl::SetStartPostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& StartPostProcessingSettings)
{
	IDisplayClusterViewport* DesiredViewport = ImplFindViewport(ViewportId);
	if (DesiredViewport)
	{
		UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("SetStartPostProcessingSettings - id=%s"), *ViewportId);

		DesiredViewport->GetViewport_CustomPostProcessSettings().AddCustomPostProcess(IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Start, StartPostProcessingSettings);
		return;
	}

	UE_LOG(LogDisplayClusterBlueprint, Warning, TEXT("Couldn't set SetStartPostProcessingSettings. Viewport '%s' not found"), *ViewportId);
}

void UDisplayClusterBlueprintAPIImpl::SetOverridePostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight)
{
	IDisplayClusterViewport* DesiredViewport = ImplFindViewport(ViewportId);
	if (DesiredViewport)
	{
		UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("SetOverridePostProcessingSettings - id=%s, weight=%f"), *ViewportId, BlendWeight);

		DesiredViewport->GetViewport_CustomPostProcessSettings().AddCustomPostProcess(IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Override, OverridePostProcessingSettings, BlendWeight);
		return;
	}

	UE_LOG(LogDisplayClusterBlueprint, Warning, TEXT("Couldn't set SetOverridePostProcessingSettings. Viewport '%s' not found"), *ViewportId);
}

void UDisplayClusterBlueprintAPIImpl::SetFinalPostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& FinalPostProcessingSettings)
{
	IDisplayClusterViewport* DesiredViewport = ImplFindViewport(ViewportId);
	if (DesiredViewport)
	{
		UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("SetFinalPostProcessingSettings - id=%s"), *ViewportId);

		DesiredViewport->GetViewport_CustomPostProcessSettings().AddCustomPostProcess(IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Final, FinalPostProcessingSettings);
		return;
	}

	UE_LOG(LogDisplayClusterBlueprint, Warning, TEXT("Couldn't set SetFinalPostProcessingSettings. Viewport '%s' not found"), *ViewportId);
}

bool UDisplayClusterBlueprintAPIImpl::GetViewportRect(const FString& ViewportId, FIntPoint& ViewportLoc, FIntPoint& ViewportSize) const
{
	IDisplayClusterViewport* DesiredViewport = ImplFindViewport(ViewportId);
	if (DesiredViewport)
	{
		FIntRect ViewportRect = DesiredViewport->GetRenderSettings().Rect;
		ViewportLoc = ViewportRect.Min;
		ViewportSize = ViewportRect.Size();

		UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetViewportRect - id=%s, loc=%s, size=%s"), *ViewportId, *DisplayClusterTypesConverter::template ToString(ViewportLoc), *DisplayClusterTypesConverter::template ToString(ViewportSize));
		return true;
	}

	return false;
}

void UDisplayClusterBlueprintAPIImpl::GetLocalViewports(TArray<FString>& ViewportIDs, TArray<FString>& ProjectionTypes, TArray<FIntPoint>& ViewportLocations, TArray<FIntPoint>& ViewportSizes) const
{
	// Clean output containers
	ViewportIDs.Empty();
	ProjectionTypes.Empty();
	ViewportLocations.Empty();
	ViewportSizes.Empty();

	for (IDisplayClusterViewport* Viewport : ImplGetViewports())
	{
		ViewportIDs.Add(Viewport->GetId());
		ViewportLocations.Add(Viewport->GetRenderSettings().Rect.Min);
		ViewportSizes.Add(Viewport->GetRenderSettings().Rect.Size());

		if ((Viewport->GetProjectionPolicy().IsValid()))
		{
			ProjectionTypes.Add(Viewport->GetProjectionPolicy()->GetTypeId());
		}
		else
		{
			ProjectionTypes.Add(FString(TEXT("None")));
		}
	}
}

void UDisplayClusterBlueprintAPIImpl::SceneViewExtensionIsActiveInContextFunction(const TArray<FString>& ViewportIDs, FSceneViewExtensionIsActiveFunctor& OutIsActiveFunction) const
{
	OutIsActiveFunction.IsActiveFunction = [ViewportIDs](const ISceneViewExtension* SceneViewExtension, const FSceneViewExtensionContext& Context) 
	{
		// If the context is not a known one, offer no opinion.
		{
			if (!Context.IsA(FDisplayClusterSceneViewExtensionContext()))
			{
				return TOptional<bool>();
			}
		}

		const FDisplayClusterSceneViewExtensionContext& DisplayContext = static_cast<const FDisplayClusterSceneViewExtensionContext&>(Context);
		
		// If no nDisplay viewport ids are given, assume this Scene View Extension should apply to all viewports.
		if (!ViewportIDs.Num())
		{
			return TOptional<bool>(true);
		}
		
		// Return true/false depending on whether the contextual nDisplay Viewport is found in the given array of ids or not.
		return TOptional<bool>(!!ViewportIDs.FindByKey(DisplayContext.ViewportId));
	};
}

