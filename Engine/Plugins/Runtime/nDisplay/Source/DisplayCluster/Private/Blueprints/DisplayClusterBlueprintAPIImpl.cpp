// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/DisplayClusterBlueprintAPIImpl.h"

#include "IDisplayCluster.h"

#include "DisplayClusterLog.h"
#include "Cluster/DisplayClusterClusterEvent.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"
#include "Input/IDisplayClusterInputManager.h"
#include "Render/IDisplayClusterRenderManager.h"

#include "DisplayClusterRootComponent.h"

#include "Config/DisplayClusterConfigTypes.h"
#include "Misc/DisplayClusterHelpers.h"

#include "DisplayClusterGlobals.h"


//////////////////////////////////////////////////////////////////////////////////////////////
// DisplayCluster module API
//////////////////////////////////////////////////////////////////////////////////////////////
/** Return if the module has been initialized. */
bool UDisplayClusterBlueprintAPIImpl::IsModuleInitialized()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);
	return IDisplayCluster::Get().IsModuleInitialized();
}

EDisplayClusterOperationMode UDisplayClusterBlueprintAPIImpl::GetOperationMode()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);
	return IDisplayCluster::Get().GetOperationMode();
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Cluster API
//////////////////////////////////////////////////////////////////////////////////////////////
bool UDisplayClusterBlueprintAPIImpl::IsMaster()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterClusterManager* const Manager = IDisplayCluster::Get().GetClusterMgr();
	if (Manager)
	{
		return Manager->IsMaster();
	}

	return false;
}

bool UDisplayClusterBlueprintAPIImpl::IsSlave()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	return !IsMaster();
}

bool UDisplayClusterBlueprintAPIImpl::IsCluster()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterClusterManager* const Manager = IDisplayCluster::Get().GetClusterMgr();
	if (Manager)
	{
		return Manager->IsCluster();
	}

	return false;
}

bool UDisplayClusterBlueprintAPIImpl::IsStandalone()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	return !IsCluster();
}

FString UDisplayClusterBlueprintAPIImpl::GetNodeId()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterClusterManager* const Manager = IDisplayCluster::Get().GetClusterMgr();
	if (Manager)
	{
		return Manager->GetNodeId();
	}

	return FString();
}

int32 UDisplayClusterBlueprintAPIImpl::GetNodesAmount()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterClusterManager* const Manager = IDisplayCluster::Get().GetClusterMgr();
	if (Manager)
	{
		return Manager->GetNodesAmount();
	}

	return 0;
}

void UDisplayClusterBlueprintAPIImpl::AddClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterClusterManager* const Manager = IDisplayCluster::Get().GetClusterMgr();
	if (Manager)
	{
		return Manager->AddClusterEventListener(Listener);
	}
}

void UDisplayClusterBlueprintAPIImpl::RemoveClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterClusterManager* const Manager = IDisplayCluster::Get().GetClusterMgr();
	if (Manager)
	{
		return Manager->RemoveClusterEventListener(Listener);
	}
}

void UDisplayClusterBlueprintAPIImpl::EmitClusterEvent(const FDisplayClusterClusterEvent& Event, bool MasterOnly)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterClusterManager* const Manager = IDisplayCluster::Get().GetClusterMgr();
	if (Manager)
	{
		return Manager->EmitClusterEvent(Event, MasterOnly);
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Config API
//////////////////////////////////////////////////////////////////////////////////////////////
void UDisplayClusterBlueprintAPIImpl::GetLocalViewports(bool IsRTT, TArray<FString>& ViewportIDs, TArray<FString>& ViewportTypes, TArray<FIntPoint>& ViewportLocations, TArray<FIntPoint>& ViewportSizes)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	TArray<FDisplayClusterConfigViewport> SelectedViewports = DisplayClusterHelpers::config::GetLocalViewports().FilterByPredicate([IsRTT](const FDisplayClusterConfigViewport& Item)
	{
		return Item.IsRTT == IsRTT;
	});

	for (const FDisplayClusterConfigViewport& Item : SelectedViewports)
	{
		FDisplayClusterConfigProjection CfgProjection;
		if (IDisplayCluster::Get().GetConfigMgr()->GetProjection(Item.ProjectionId, CfgProjection))
		{
			ViewportIDs.Add(Item.Id);
			ViewportTypes.Add(CfgProjection.Type);
			ViewportLocations.Add(Item.Loc);
			ViewportSizes.Add(Item.Size);
		}
	}	
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Game API
//////////////////////////////////////////////////////////////////////////////////////////////
// Root
ADisplayClusterRootActor* UDisplayClusterBlueprintAPIImpl::GetRootActor()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterGameManager* const Manager = IDisplayCluster::Get().GetGameMgr();
	if (Manager)
	{
		return Manager->GetRootActor();
	}

	return nullptr;
}

UDisplayClusterRootComponent* UDisplayClusterBlueprintAPIImpl::GetRootComponent()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterGameManager* const Manager = IDisplayCluster::Get().GetGameMgr();
	if (Manager)
	{
		return Manager->GetRootComponent();
	}

	return nullptr;
}

// Screens
UDisplayClusterScreenComponent* UDisplayClusterBlueprintAPIImpl::GetScreenById(const FString& id)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterGameManager* const Manager = IDisplayCluster::Get().GetGameMgr();
	if (Manager)
	{
		return Manager->GetScreenById(id);
	}

	return nullptr;
}

TArray<UDisplayClusterScreenComponent*> UDisplayClusterBlueprintAPIImpl::GetAllScreens()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterGameManager* const Manager = IDisplayCluster::Get().GetGameMgr();
	if (Manager)
	{
		return Manager->GetAllScreens();
	}

	return TArray<UDisplayClusterScreenComponent*>();
}

int32 UDisplayClusterBlueprintAPIImpl::GetScreensAmount()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterGameManager* const Manager = IDisplayCluster::Get().GetGameMgr();
	if (Manager)
	{
		return Manager->GetScreensAmount();
	}

	return 0;
}

// Cameras
TArray<UDisplayClusterCameraComponent*> UDisplayClusterBlueprintAPIImpl::GetAllCameras()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterGameManager* const Manager = IDisplayCluster::Get().GetGameMgr();
	if (Manager)
	{
		return Manager->GetAllCameras();
	}

	return TArray<UDisplayClusterCameraComponent*>();
}

UDisplayClusterCameraComponent* UDisplayClusterBlueprintAPIImpl::GetCameraById(const FString& id)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterGameManager* const Manager = IDisplayCluster::Get().GetGameMgr();
	if (Manager)
	{
		return Manager->GetCameraById(id);
	}

	return nullptr;
}

int32 UDisplayClusterBlueprintAPIImpl::GetCamerasAmount()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterGameManager* const Manager = IDisplayCluster::Get().GetGameMgr();
	if (Manager)
	{
		return Manager->GetCamerasAmount();
	}

	return 0;
}

UDisplayClusterCameraComponent* UDisplayClusterBlueprintAPIImpl::GetDefaultCamera()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterGameManager* const Manager = IDisplayCluster::Get().GetGameMgr();
	if (Manager)
	{
		return Manager->GetDefaultCamera();
	}

	return nullptr;
}

void UDisplayClusterBlueprintAPIImpl::SetDefaultCameraById(const FString& id)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterGameManager* const Manager = IDisplayCluster::Get().GetGameMgr();
	if (Manager)
	{
		return Manager->SetDefaultCamera(id);
	}
}


// Nodes
UDisplayClusterSceneComponent* UDisplayClusterBlueprintAPIImpl::GetNodeById(const FString& id)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterGameManager* const Manager = IDisplayCluster::Get().GetGameMgr();
	if (Manager)
	{
		return Manager->GetNodeById(id);
	}

	return nullptr;
}

TArray<UDisplayClusterSceneComponent*> UDisplayClusterBlueprintAPIImpl::GetAllNodes()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterGameManager* const Manager = IDisplayCluster::Get().GetGameMgr();
	if (Manager)
	{
		return Manager->GetAllNodes();
	}

	return TArray<UDisplayClusterSceneComponent*>();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Input API
//////////////////////////////////////////////////////////////////////////////////////////////
// Device information
int32 UDisplayClusterBlueprintAPIImpl::GetAxisDeviceAmount()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterInputManager* const Manager = IDisplayCluster::Get().GetInputMgr();
	if (Manager)
	{
		Manager->GetAxisDeviceAmount();
	}

	return 0;
}

int32 UDisplayClusterBlueprintAPIImpl::GetButtonDeviceAmount()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterInputManager* const Manager = IDisplayCluster::Get().GetInputMgr();
	if (Manager)
	{
		Manager->GetButtonDeviceAmount();
	}

	return 0;
}

int32 UDisplayClusterBlueprintAPIImpl::GetTrackerDeviceAmount()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterInputManager* const Manager = IDisplayCluster::Get().GetInputMgr();
	if (Manager)
	{
		Manager->GetTrackerDeviceAmount();
	}

	return 0;
}

bool UDisplayClusterBlueprintAPIImpl::GetAxisDeviceIds(TArray<FString>& IDs)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	TArray<FString> result;
	IDisplayClusterInputManager* const Manager = IDisplayCluster::Get().GetInputMgr();
	if (Manager)
	{
		return Manager->GetAxisDeviceIds(IDs);
	}

	return false;
}

bool UDisplayClusterBlueprintAPIImpl::GetButtonDeviceIds(TArray<FString>& IDs)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterInputManager* const Manager = IDisplayCluster::Get().GetInputMgr();
	if (Manager)
	{
		return Manager->GetButtonDeviceIds(IDs);
	}

	return false;
}

bool UDisplayClusterBlueprintAPIImpl::GetTrackerDeviceIds(TArray<FString>& IDs)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterInputManager* const Manager = IDisplayCluster::Get().GetInputMgr();
	if (Manager)
	{
		return Manager->GetTrackerDeviceIds(IDs);
	}

	return false;
}

// Buttons
void UDisplayClusterBlueprintAPIImpl::GetButtonState(const FString& DeviceId, uint8 DeviceChannel, bool& CurState, bool& IsChannelAvailable)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterInputManager* const Manager = IDisplayCluster::Get().GetInputMgr();
	if (Manager)
	{
		IsChannelAvailable = Manager->GetButtonState(DeviceId, DeviceChannel, CurState);
	}
}

void UDisplayClusterBlueprintAPIImpl::IsButtonPressed(const FString& DeviceId, uint8 DeviceChannel, bool& CurPressed, bool& IsChannelAvailable)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterInputManager* const Manager = IDisplayCluster::Get().GetInputMgr();
	if (Manager)
	{
		IsChannelAvailable = Manager->IsButtonPressed(DeviceId, DeviceChannel, CurPressed);
	}
}

void UDisplayClusterBlueprintAPIImpl::IsButtonReleased(const FString& DeviceId, uint8 DeviceChannel, bool& CurReleased, bool& IsChannelAvailable)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterInputManager* const Manager = IDisplayCluster::Get().GetInputMgr();
	if (Manager)
	{
		IsChannelAvailable = Manager->IsButtonReleased(DeviceId, DeviceChannel, CurReleased);
	}
}

void UDisplayClusterBlueprintAPIImpl::WasButtonPressed(const FString& DeviceId, uint8 DeviceChannel, bool& WasPressed, bool& IsChannelAvailable)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterInputManager* const Manager = IDisplayCluster::Get().GetInputMgr();
	if (Manager)
	{
		IsChannelAvailable = Manager->WasButtonPressed(DeviceId, DeviceChannel, WasPressed);
	}
}

void UDisplayClusterBlueprintAPIImpl::WasButtonReleased(const FString& DeviceId, uint8 DeviceChannel, bool& WasReleased, bool& IsChannelAvailable)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterInputManager* const Manager = IDisplayCluster::Get().GetInputMgr();
	if (Manager)
	{
		IsChannelAvailable = Manager->WasButtonReleased(DeviceId, DeviceChannel, WasReleased);
	}
}

// Axes
void UDisplayClusterBlueprintAPIImpl::GetAxis(const FString& DeviceId, uint8 DeviceChannel, float& Value, bool& IsChannelAvailable)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterInputManager* const Manager = IDisplayCluster::Get().GetInputMgr();
	if (Manager)
	{
		IsChannelAvailable = Manager->GetAxis(DeviceId, DeviceChannel, Value);
	}
}

// Trackers
void UDisplayClusterBlueprintAPIImpl::GetTrackerLocation(const FString& DeviceId, uint8 DeviceChannel, FVector& Location, bool& IsChannelAvailable)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterInputManager* const Manager = IDisplayCluster::Get().GetInputMgr();
	if (Manager)
	{
		IsChannelAvailable = Manager->GetTrackerLocation(DeviceId, DeviceChannel, Location);
	}
}

void UDisplayClusterBlueprintAPIImpl::GetTrackerQuat(const FString& DeviceId, uint8 DeviceChannel, FQuat& Rotation, bool& IsChannelAvailable)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterInputManager* const Manager = IDisplayCluster::Get().GetInputMgr();
	if (Manager)
	{
		IsChannelAvailable = Manager->GetTrackerQuat(DeviceId, DeviceChannel, Rotation);
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Render API
//////////////////////////////////////////////////////////////////////////////////////////////
void UDisplayClusterBlueprintAPIImpl::SetViewportCamera(const FString& InCameraId, const FString& InViewportId)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterRenderManager* const Manager = IDisplayCluster::Get().GetRenderMgr();
	if (Manager)
	{
		Manager->SetViewportCamera(InCameraId, InViewportId);
	}

	return;
}

void UDisplayClusterBlueprintAPIImpl::GetBufferRatio(const FString& InViewportId, float &OutBufferRatio)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterRenderManager* const Manager = IDisplayCluster::Get().GetRenderMgr();
	if (Manager)
	{
		Manager->GetBufferRatio(InViewportId, OutBufferRatio);
	}

	return;
}

void UDisplayClusterBlueprintAPIImpl::SetBufferRatio(const FString& InViewportId, float InBufferRatio)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterRenderManager* const Manager = IDisplayCluster::Get().GetRenderMgr();
	if (Manager)
	{
		Manager->SetBufferRatio(InViewportId, InBufferRatio);
	}

	return;
}

void UDisplayClusterBlueprintAPIImpl::SetStartPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& StartPostProcessingSettings)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterRenderManager* const Manager = IDisplayCluster::Get().GetRenderMgr();
	if (Manager)
	{
		Manager->SetStartPostProcessingSettings(ViewportID, StartPostProcessingSettings);
	}
}

void UDisplayClusterBlueprintAPIImpl::SetOverridePostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterRenderManager* const Manager = IDisplayCluster::Get().GetRenderMgr();
	if (Manager)
	{
		Manager->SetOverridePostProcessingSettings(ViewportID, OverridePostProcessingSettings, BlendWeight);
	}
}

void UDisplayClusterBlueprintAPIImpl::SetFinalPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& FinalPostProcessingSettings)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterRenderManager* const Manager = IDisplayCluster::Get().GetRenderMgr();
	if (Manager)
	{
		Manager->SetFinalPostProcessingSettings(ViewportID, FinalPostProcessingSettings);
	}
}

bool UDisplayClusterBlueprintAPIImpl::GetViewportRect(const FString& ViewportID, FIntPoint& ViewportLoc, FIntPoint& ViewportSize)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterRenderManager* const Manager = IDisplayCluster::Get().GetRenderMgr();
	if (Manager)
	{
		FIntRect ViewportRect;
		if (Manager->GetViewportRect(ViewportID, ViewportRect))
		{
			ViewportLoc  = ViewportRect.Min;
			ViewportSize = ViewportRect.Size();
			return true;
		}
	}

	return false;
}


float UDisplayClusterBlueprintAPIImpl::GetInterpupillaryDistance(const FString& CameraId)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterRenderManager* const Manager = IDisplayCluster::Get().GetRenderMgr();
	if (Manager)
	{
		return Manager->GetInterpupillaryDistance(CameraId);
	}

	return 0.f;
}

void  UDisplayClusterBlueprintAPIImpl::SetInterpupillaryDistance(const FString& CameraId, float EyeDistance)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterRenderManager* const Manager = IDisplayCluster::Get().GetRenderMgr();
	if (Manager)
	{
		return Manager->SetInterpupillaryDistance(CameraId, EyeDistance);
	}

	return;
}

bool UDisplayClusterBlueprintAPIImpl::GetEyesSwap(const FString& CameraId)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterRenderManager* const Manager = IDisplayCluster::Get().GetRenderMgr();
	if (Manager)
	{
		return Manager->GetEyesSwap(CameraId);
	}

	return false;
}

void UDisplayClusterBlueprintAPIImpl::SetEyesSwap(const FString& CameraId, bool EyeSwapped)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterRenderManager* const Manager = IDisplayCluster::Get().GetRenderMgr();
	if (Manager)
	{
		return Manager->SetEyesSwap(CameraId, EyeSwapped);
	}

	return;
}

bool UDisplayClusterBlueprintAPIImpl::ToggleEyesSwap(const FString& CameraId)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterBlueprint);

	IDisplayClusterRenderManager* const Manager = IDisplayCluster::Get().GetRenderMgr();
	if (Manager)
	{
		return Manager->ToggleEyesSwap(CameraId);
	}

	return false;
}
