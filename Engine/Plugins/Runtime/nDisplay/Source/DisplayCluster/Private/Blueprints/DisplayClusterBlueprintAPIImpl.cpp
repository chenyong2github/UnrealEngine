// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/DisplayClusterBlueprintAPIImpl.h"

#include "IDisplayCluster.h"

#include "Cluster/DisplayClusterClusterEvent.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"
#include "Input/IDisplayClusterInputManager.h"
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
// Input API
//////////////////////////////////////////////////////////////////////////////////////////////
// Device information
int32 UDisplayClusterBlueprintAPIImpl::GetAxisDeviceAmount() const
{
	const int32 AxisDevicesAmount = IDisplayCluster::Get().GetInputMgr()->GetAxisDeviceAmount();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetAxisDeviceAmount - %d"), AxisDevicesAmount);
	return AxisDevicesAmount;
}

int32 UDisplayClusterBlueprintAPIImpl::GetButtonDeviceAmount() const
{
	const int32 ButtonDevicesAmount = IDisplayCluster::Get().GetInputMgr()->GetButtonDeviceAmount();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetButtonDeviceAmount - %d"), ButtonDevicesAmount);
	return ButtonDevicesAmount;
}

int32 UDisplayClusterBlueprintAPIImpl::GetTrackerDeviceAmount() const
{
	const int32 TrackerDevicesAmount = IDisplayCluster::Get().GetInputMgr()->GetTrackerDeviceAmount();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetTrackerDeviceAmount - %d"), TrackerDevicesAmount);
	return TrackerDevicesAmount;
}

void UDisplayClusterBlueprintAPIImpl::GetAxisDeviceIds(TArray<FString>& DeviceIDs) const
{
	IDisplayCluster::Get().GetInputMgr()->GetAxisDeviceIds(DeviceIDs);

	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetAxisDeviceIds - found %d devices"), DeviceIDs.Num());
	for (int i = 0; i < DeviceIDs.Num(); ++i)
	{
		UE_LOG(LogDisplayClusterBlueprint, VeryVerbose, TEXT("GetAxisDeviceIds - %d: %s"), i, *DeviceIDs[i]);
	}
}

void UDisplayClusterBlueprintAPIImpl::GetButtonDeviceIds(TArray<FString>& DeviceIDs) const
{
	IDisplayCluster::Get().GetInputMgr()->GetButtonDeviceIds(DeviceIDs);

	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetButtonDeviceIds - found %d devices"), DeviceIDs.Num());
	for (int i = 0; i < DeviceIDs.Num(); ++i)
	{
		UE_LOG(LogDisplayClusterBlueprint, VeryVerbose, TEXT("GetButtonDeviceIds - %d: %s"), i, *DeviceIDs[i]);
	}
}

void UDisplayClusterBlueprintAPIImpl::GetKeyboardDeviceIds(TArray<FString>& DeviceIDs) const
{
	IDisplayCluster::Get().GetInputMgr()->GetKeyboardDeviceIds(DeviceIDs);

	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetKeyboardDeviceIds - found %d devices"), DeviceIDs.Num());
	for (int i = 0; i < DeviceIDs.Num(); ++i)
	{
		UE_LOG(LogDisplayClusterBlueprint, VeryVerbose, TEXT("GetKeyboardDeviceIds - %d: %s"), i, *DeviceIDs[i]);
	}
}

void UDisplayClusterBlueprintAPIImpl::GetTrackerDeviceIds(TArray<FString>& DeviceIDs) const
{
	IDisplayCluster::Get().GetInputMgr()->GetTrackerDeviceIds(DeviceIDs);

	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetTrackerDeviceIds - found %d devices"), DeviceIDs.Num());
	for (int i = 0; i < DeviceIDs.Num(); ++i)
	{
		UE_LOG(LogDisplayClusterBlueprint, VeryVerbose, TEXT("GetTrackerDeviceIds - %d: %s"), i, *DeviceIDs[i]);
	}
}

// Buttons
void UDisplayClusterBlueprintAPIImpl::GetButtonState(const FString& DeviceId, int32 DeviceChannel, bool& bCurrentState, bool& bIsChannelAvailable) const
{
	bIsChannelAvailable = IDisplayCluster::Get().GetInputMgr()->GetButtonState(DeviceId, DeviceChannel, bCurrentState);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetButtonState - %s@%d avail=%d state=%d"), *DeviceId, DeviceChannel, bIsChannelAvailable ? 1 : 0, bCurrentState ? 1 : 0);
}

void UDisplayClusterBlueprintAPIImpl::IsButtonPressed(const FString& DeviceId, int32 DeviceChannel, bool& bIsPressedCurrently, bool& bIsChannelAvailable) const
{
	bIsChannelAvailable = IDisplayCluster::Get().GetInputMgr()->IsButtonPressed(DeviceId, DeviceChannel, bIsPressedCurrently);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("IsButtonPressed - %s@%d avail=%d pressed=%d"), *DeviceId, DeviceChannel, bIsChannelAvailable ? 1 : 0, bIsPressedCurrently ? 1 : 0);
}

void UDisplayClusterBlueprintAPIImpl::IsButtonReleased(const FString& DeviceId, int32 DeviceChannel, bool& bIsReleasedCurrently, bool& bIsChannelAvailable) const
{
	bIsChannelAvailable = IDisplayCluster::Get().GetInputMgr()->IsButtonReleased(DeviceId, DeviceChannel, bIsReleasedCurrently);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("IsButtonReleased - %s@%d avail=%d released=%d"), *DeviceId, DeviceChannel, bIsChannelAvailable ? 1 : 0, bIsReleasedCurrently ? 1 : 0);
}

void UDisplayClusterBlueprintAPIImpl::WasButtonPressed(const FString& DeviceId, int32 DeviceChannel, bool& bWasPressed, bool& bIsChannelAvailable) const
{
	bIsChannelAvailable = IDisplayCluster::Get().GetInputMgr()->WasButtonPressed(DeviceId, DeviceChannel, bWasPressed);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("WasButtonPressed - %s@%d avail=%d just_pressed=%d"), *DeviceId, DeviceChannel, bIsChannelAvailable ? 1 : 0, bWasPressed ? 1 : 0);
}

void UDisplayClusterBlueprintAPIImpl::WasButtonReleased(const FString& DeviceId, int32 DeviceChannel, bool& bWasReleased, bool& bIsChannelAvailable) const
{
	bIsChannelAvailable = IDisplayCluster::Get().GetInputMgr()->WasButtonReleased(DeviceId, DeviceChannel, bWasReleased);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("WasButtonReleased - %s@%d avail=%d just_released=%d"), *DeviceId, DeviceChannel, bIsChannelAvailable ? 1 : 0, bWasReleased ? 1 : 0);
}

// Axes
void UDisplayClusterBlueprintAPIImpl::GetAxis(const FString& DeviceId, int32 DeviceChannel, float& Value, bool& bIsChannelAvailable) const
{
	bIsChannelAvailable = IDisplayCluster::Get().GetInputMgr()->GetAxis(DeviceId, DeviceChannel, Value);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetAxis - %s@%d avail=%d value=%f"), *DeviceId, DeviceChannel, bIsChannelAvailable ? 1 : 0, Value);
}

// Trackers
void UDisplayClusterBlueprintAPIImpl::GetTrackerLocation(const FString& DeviceId, int32 DeviceChannel, FVector& Location, bool& bIsChannelAvailable) const
{
	bIsChannelAvailable = IDisplayCluster::Get().GetInputMgr()->GetTrackerLocation(DeviceId, DeviceChannel, Location);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetTrackerLocation - %s@%d avail=%d loc=%f"), *DeviceId, DeviceChannel, bIsChannelAvailable ? 1 : 0, *DisplayClusterTypesConverter::template ToString(Location));
}

void UDisplayClusterBlueprintAPIImpl::GetTrackerQuat(const FString& DeviceId, int32 DeviceChannel, FQuat& Rotation, bool& bIsChannelAvailable) const
{
	bIsChannelAvailable = IDisplayCluster::Get().GetInputMgr()->GetTrackerQuat(DeviceId, DeviceChannel, Rotation);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetTrackerQuat - %s@%d avail=%d rot=%f"), *DeviceId, DeviceChannel, bIsChannelAvailable ? 1 : 0, *DisplayClusterTypesConverter::template ToString(Rotation));
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

