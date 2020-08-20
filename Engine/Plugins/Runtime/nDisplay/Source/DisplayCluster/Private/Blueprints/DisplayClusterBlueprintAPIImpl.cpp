// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/DisplayClusterBlueprintAPIImpl.h"

#include "IDisplayCluster.h"

#include "Cluster/DisplayClusterClusterEvent.h"
#include "Cluster/IDisplayClusterClusterManager.h"
#include "Config/IDisplayClusterConfigManager.h"
#include "Game/IDisplayClusterGameManager.h"
#include "Input/IDisplayClusterInputManager.h"
#include "Render/IDisplayClusterRenderManager.h"

#include "Config/DisplayClusterConfigTypes.h"

#include "Misc/DisplayClusterCommonHelpers.h"
#include "Misc/DisplayClusterCommonTypesConverter.h"
#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"
#include "Misc/DisplayClusterStrings.h"

#include "DisplayClusterRootActor.h"
#include "DisplayClusterRootComponent.h"
#include "DisplayClusterCameraComponent.h"
#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterScreenComponent.h"
#include "DisplayClusterEnums.h"

#include "Math/IntRect.h"

#include "CineCameraComponent.h"

//////////////////////////////////////////////////////////////////////////////////////////////
// DisplayCluster module API
//////////////////////////////////////////////////////////////////////////////////////////////
/** Return if the module has been initialized. */
bool UDisplayClusterBlueprintAPIImpl::IsModuleInitialized()
{
	const bool bInitialized = IDisplayCluster::Get().IsModuleInitialized();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("IsModuleInitialized - %s"), *FDisplayClusterTypesConverter::ToString(bInitialized));
	return bInitialized;
}

EDisplayClusterOperationMode UDisplayClusterBlueprintAPIImpl::GetOperationMode()
{
	const EDisplayClusterOperationMode OpMode = IDisplayCluster::Get().GetOperationMode();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("Operation mode - %s"), *FDisplayClusterTypesConverter::ToString(OpMode));
	return OpMode;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Cluster API
//////////////////////////////////////////////////////////////////////////////////////////////
bool UDisplayClusterBlueprintAPIImpl::IsMaster()
{
	const bool bIsMaster = IDisplayCluster::Get().GetClusterMgr()->IsMaster();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("IsMaster - %s"), *FDisplayClusterTypesConverter::ToString(bIsMaster));
	return bIsMaster;
}

bool UDisplayClusterBlueprintAPIImpl::IsSlave()
{
	const bool bIsSlave = !IsMaster();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("IsSlave - %s"), *FDisplayClusterTypesConverter::ToString(bIsSlave));
	return bIsSlave;
}

bool UDisplayClusterBlueprintAPIImpl::IsCluster()
{
	const bool bIsCluster = IDisplayCluster::Get().GetClusterMgr()->IsSlave();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("IsCluster - %s"), *FDisplayClusterTypesConverter::ToString(bIsCluster));
	return bIsCluster;
}

bool UDisplayClusterBlueprintAPIImpl::IsStandalone()
{
	const bool bIsStandalone = !IsCluster();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("IsStandalone - %s"), *FDisplayClusterTypesConverter::ToString(bIsStandalone));
	return bIsStandalone;
}

FString UDisplayClusterBlueprintAPIImpl::GetNodeId()
{
	const FString NodeId = IDisplayCluster::Get().GetClusterMgr()->GetNodeId();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetNodeId - NodeId=%s"), *NodeId);
	return NodeId;
}

int32 UDisplayClusterBlueprintAPIImpl::GetNodesAmount()
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

void UDisplayClusterBlueprintAPIImpl::EmitClusterEvent(const FDisplayClusterClusterEvent& Event, bool MasterOnly)
{
	UE_LOG(LogDisplayClusterBlueprint, Verbose,     TEXT("EmitClusterEvent - emitting cluster event, IsMasterOnly=%s"), *FDisplayClusterTypesConverter::ToString(MasterOnly));
	UE_LOG(LogDisplayClusterBlueprint, VeryVerbose, TEXT("EmitClusterEvent - emitting cluster event, Category='%s' Type='%s' Name='%s'"), *Event.Category, *Event.Type, *Event.Name);
	IDisplayCluster::Get().GetClusterMgr()->EmitClusterEvent(Event, MasterOnly);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Config API
//////////////////////////////////////////////////////////////////////////////////////////////
void UDisplayClusterBlueprintAPIImpl::GetLocalViewports(bool IsRTT, TArray<FString>& ViewportIDs, TArray<FString>& ViewportTypes, TArray<FIntPoint>& ViewportLocations, TArray<FIntPoint>& ViewportSizes)
{
	TArray<FDisplayClusterConfigViewport> SelectedViewports = DisplayClusterHelpers::config::GetLocalViewports().FilterByPredicate([IsRTT](const FDisplayClusterConfigViewport& Item)
	{
		return Item.bIsRTT == IsRTT;
	});

	IDisplayClusterConfigManager* const ConfigMgr = IDisplayCluster::Get().GetConfigMgr();
	if (ConfigMgr)
	{
		for (const FDisplayClusterConfigViewport& Item : SelectedViewports)
		{
			FDisplayClusterConfigProjection CfgProjection;
			if (ConfigMgr->GetProjection(Item.ProjectionId, CfgProjection))
			{
				ViewportIDs.Add(Item.Id);
				ViewportTypes.Add(CfgProjection.Type);
				ViewportLocations.Add(Item.Loc);
				ViewportSizes.Add(Item.Size);
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Game API
//////////////////////////////////////////////////////////////////////////////////////////////
// Root
ADisplayClusterRootActor* UDisplayClusterBlueprintAPIImpl::GetRootActor()
{
	ADisplayClusterRootActor* const RootActor = IDisplayCluster::Get().GetGameMgr()->GetRootActor();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetRootActor - %s"), RootActor ? *RootActor->GetHumanReadableName() : DisplayClusterStrings::log::NotFound);
	return RootActor;
}

UDisplayClusterRootComponent* UDisplayClusterBlueprintAPIImpl::GetRootComponent()
{
	UDisplayClusterRootComponent* const RootComp = IDisplayCluster::Get().GetGameMgr()->GetRootComponent();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetRootComponent - %s"), RootComp ? *RootComp->GetReadableName() : DisplayClusterStrings::log::NotFound);
	return RootComp;
}

// Screens
UDisplayClusterScreenComponent* UDisplayClusterBlueprintAPIImpl::GetScreenById(const FString& ScreenID)
{
	UDisplayClusterScreenComponent* const ScreenComp = IDisplayCluster::Get().GetGameMgr()->GetScreenById(ScreenID);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetScreenById(%s) - %s"), *ScreenID, ScreenComp ? *ScreenComp->GetReadableName() : DisplayClusterStrings::log::NotFound);
	return ScreenComp;
}

TArray<UDisplayClusterScreenComponent*> UDisplayClusterBlueprintAPIImpl::GetAllScreens()
{
	const TArray<UDisplayClusterScreenComponent*> ScreenComponents = IDisplayCluster::Get().GetGameMgr()->GetAllScreens();

	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetAllScreens - found %d screens"), ScreenComponents.Num());
	for (int i = 0; i < ScreenComponents.Num(); ++i)
	{
		UE_LOG(LogDisplayClusterBlueprint, VeryVerbose, TEXT("GetAllScreens - %d: %s"), i, ScreenComponents[i] ? *ScreenComponents[i]->GetReadableName() : DisplayClusterStrings::log::NotFound);
	}

	return ScreenComponents;
}

int32 UDisplayClusterBlueprintAPIImpl::GetScreensAmount()
{
	const int32 ScreensAmount = IDisplayCluster::Get().GetGameMgr()->GetScreensAmount();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetScreensAmount - %d screens available"), ScreensAmount);
	return ScreensAmount;
}

// Cameras
TArray<UDisplayClusterCameraComponent*> UDisplayClusterBlueprintAPIImpl::GetAllCameras()
{
	const TArray<UDisplayClusterCameraComponent*> Cameras = IDisplayCluster::Get().GetGameMgr()->GetAllCameras();

	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetAllCameras - found %d cameras"), Cameras.Num());
	for (int i = 0; i < Cameras.Num(); ++i)
	{
		UE_LOG(LogDisplayClusterBlueprint, VeryVerbose, TEXT("GetAllCameras - %d: %s"), i, Cameras[i] ? *Cameras[i]->GetReadableName() : DisplayClusterStrings::log::NotFound);
	}

	return Cameras;
}

UDisplayClusterCameraComponent* UDisplayClusterBlueprintAPIImpl::GetCameraById(const FString& CameraID)
{
	UDisplayClusterCameraComponent* const CameraComp = IDisplayCluster::Get().GetGameMgr()->GetCameraById(CameraID);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetCameraById(%s) - %s"), *CameraID, CameraComp ? *CameraComp->GetReadableName() : DisplayClusterStrings::log::NotFound);
	return CameraComp;
}

int32 UDisplayClusterBlueprintAPIImpl::GetCamerasAmount()
{
	const int32 CamerasAmount = IDisplayCluster::Get().GetGameMgr()->GetCamerasAmount();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetCamerasAmount - %d cameras available"), CamerasAmount);
	return CamerasAmount;
}

UDisplayClusterCameraComponent* UDisplayClusterBlueprintAPIImpl::GetDefaultCamera()
{
	UDisplayClusterCameraComponent* const CameraComp = IDisplayCluster::Get().GetGameMgr()->GetDefaultCamera();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetDefaultCamera - %s"), CameraComp ? *CameraComp->GetReadableName() : DisplayClusterStrings::log::NotFound);
	return CameraComp;
}

void UDisplayClusterBlueprintAPIImpl::SetDefaultCameraById(const FString& CameraID)
{
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("SetDefaultCameraById - %s"), *CameraID);
	IDisplayCluster::Get().GetGameMgr()->SetDefaultCamera(CameraID);
}


// Scene nodes
UDisplayClusterSceneComponent* UDisplayClusterBlueprintAPIImpl::GetNodeById(const FString& SceneNodeID)
{
	UDisplayClusterSceneComponent* const SceneNodeComp = IDisplayCluster::Get().GetGameMgr()->GetNodeById(SceneNodeID);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetNodeById(%s) - %s"), *SceneNodeID, SceneNodeComp ? *SceneNodeComp->GetReadableName() : DisplayClusterStrings::log::NotFound);
	return SceneNodeComp;
}

TArray<UDisplayClusterSceneComponent*> UDisplayClusterBlueprintAPIImpl::GetAllNodes()
{
	const TArray<UDisplayClusterSceneComponent*> SceneNodes = IDisplayCluster::Get().GetGameMgr()->GetAllNodes();

	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetAllNodes - found %d scene nodes"), SceneNodes.Num());
	for (int i = 0; i < SceneNodes.Num(); ++i)
	{
		UE_LOG(LogDisplayClusterBlueprint, VeryVerbose, TEXT("GetAllNodes - %d: %s"), i, SceneNodes[i] ? *SceneNodes[i]->GetReadableName() : DisplayClusterStrings::log::NotFound);
	}

	return SceneNodes;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Input API
//////////////////////////////////////////////////////////////////////////////////////////////
// Device information
int32 UDisplayClusterBlueprintAPIImpl::GetAxisDeviceAmount()
{
	const int32 AxisDevicesAmount = IDisplayCluster::Get().GetInputMgr()->GetAxisDeviceAmount();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetAxisDeviceAmount - %d"), AxisDevicesAmount);
	return AxisDevicesAmount;
}

int32 UDisplayClusterBlueprintAPIImpl::GetButtonDeviceAmount()
{
	const int32 ButtonDevicesAmount = IDisplayCluster::Get().GetInputMgr()->GetButtonDeviceAmount();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetButtonDeviceAmount - %d"), ButtonDevicesAmount);
	return ButtonDevicesAmount;
}

int32 UDisplayClusterBlueprintAPIImpl::GetTrackerDeviceAmount()
{
	const int32 TrackerDevicesAmount = IDisplayCluster::Get().GetInputMgr()->GetTrackerDeviceAmount();
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetTrackerDeviceAmount - %d"), TrackerDevicesAmount);
	return TrackerDevicesAmount;
}

void UDisplayClusterBlueprintAPIImpl::GetAxisDeviceIds(TArray<FString>& DeviceIDs)
{
	IDisplayCluster::Get().GetInputMgr()->GetAxisDeviceIds(DeviceIDs);

	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetAxisDeviceIds - found %d devices"), DeviceIDs.Num());
	for (int i = 0; i < DeviceIDs.Num(); ++i)
	{
		UE_LOG(LogDisplayClusterBlueprint, VeryVerbose, TEXT("GetAxisDeviceIds - %d: %s"), i, *DeviceIDs[i]);
	}
}

void UDisplayClusterBlueprintAPIImpl::GetButtonDeviceIds(TArray<FString>& DeviceIDs)
{
	IDisplayCluster::Get().GetInputMgr()->GetButtonDeviceIds(DeviceIDs);

	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetButtonDeviceIds - found %d devices"), DeviceIDs.Num());
	for (int i = 0; i < DeviceIDs.Num(); ++i)
	{
		UE_LOG(LogDisplayClusterBlueprint, VeryVerbose, TEXT("GetButtonDeviceIds - %d: %s"), i, *DeviceIDs[i]);
	}
}

void UDisplayClusterBlueprintAPIImpl::GetKeyboardDeviceIds(TArray<FString>& DeviceIDs)
{
	IDisplayCluster::Get().GetInputMgr()->GetKeyboardDeviceIds(DeviceIDs);

	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetKeyboardDeviceIds - found %d devices"), DeviceIDs.Num());
	for (int i = 0; i < DeviceIDs.Num(); ++i)
	{
		UE_LOG(LogDisplayClusterBlueprint, VeryVerbose, TEXT("GetKeyboardDeviceIds - %d: %s"), i, *DeviceIDs[i]);
	}
}

void UDisplayClusterBlueprintAPIImpl::GetTrackerDeviceIds(TArray<FString>& DeviceIDs)
{
	IDisplayCluster::Get().GetInputMgr()->GetTrackerDeviceIds(DeviceIDs);

	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetTrackerDeviceIds - found %d devices"), DeviceIDs.Num());
	for (int i = 0; i < DeviceIDs.Num(); ++i)
	{
		UE_LOG(LogDisplayClusterBlueprint, VeryVerbose, TEXT("GetTrackerDeviceIds - %d: %s"), i, *DeviceIDs[i]);
	}
}

// Buttons
void UDisplayClusterBlueprintAPIImpl::GetButtonState(const FString& DeviceID, uint8 DeviceChannel, bool& CurrentState, bool& IsChannelAvailable)
{
	IsChannelAvailable = IDisplayCluster::Get().GetInputMgr()->GetButtonState(DeviceID, DeviceChannel, CurrentState);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetButtonState - %s@%d avail=%d state=%d"), *DeviceID, DeviceChannel, IsChannelAvailable ? 1 : 0, CurrentState ? 1 : 0);
}

void UDisplayClusterBlueprintAPIImpl::IsButtonPressed(const FString& DeviceID, uint8 DeviceChannel, bool& IsPressedCurrently, bool& IsChannelAvailable)
{
	IsChannelAvailable = IDisplayCluster::Get().GetInputMgr()->IsButtonPressed(DeviceID, DeviceChannel, IsPressedCurrently);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("IsButtonPressed - %s@%d avail=%d pressed=%d"), *DeviceID, DeviceChannel, IsChannelAvailable ? 1 : 0, IsPressedCurrently ? 1 : 0);
}

void UDisplayClusterBlueprintAPIImpl::IsButtonReleased(const FString& DeviceID, uint8 DeviceChannel, bool& IsReleasedCurrently, bool& IsChannelAvailable)
{
	IsChannelAvailable = IDisplayCluster::Get().GetInputMgr()->IsButtonReleased(DeviceID, DeviceChannel, IsReleasedCurrently);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("IsButtonReleased - %s@%d avail=%d released=%d"), *DeviceID, DeviceChannel, IsChannelAvailable ? 1 : 0, IsReleasedCurrently ? 1 : 0);
}

void UDisplayClusterBlueprintAPIImpl::WasButtonPressed(const FString& DeviceID, uint8 DeviceChannel, bool& WasPressed, bool& IsChannelAvailable)
{
	IsChannelAvailable = IDisplayCluster::Get().GetInputMgr()->WasButtonPressed(DeviceID, DeviceChannel, WasPressed);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("WasButtonPressed - %s@%d avail=%d just_pressed=%d"), *DeviceID, DeviceChannel, IsChannelAvailable ? 1 : 0, WasPressed ? 1 : 0);
}

void UDisplayClusterBlueprintAPIImpl::WasButtonReleased(const FString& DeviceID, uint8 DeviceChannel, bool& WasReleased, bool& IsChannelAvailable)
{
	IsChannelAvailable = IDisplayCluster::Get().GetInputMgr()->WasButtonReleased(DeviceID, DeviceChannel, WasReleased);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("WasButtonReleased - %s@%d avail=%d just_released=%d"), *DeviceID, DeviceChannel, IsChannelAvailable ? 1 : 0, WasReleased ? 1 : 0);
}

// Axes
void UDisplayClusterBlueprintAPIImpl::GetAxis(const FString& DeviceID, uint8 DeviceChannel, float& Value, bool& IsChannelAvailable)
{
	IsChannelAvailable = IDisplayCluster::Get().GetInputMgr()->GetAxis(DeviceID, DeviceChannel, Value);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetAxis - %s@%d avail=%d value=%f"), *DeviceID, DeviceChannel, IsChannelAvailable ? 1 : 0, Value);
}

// Trackers
void UDisplayClusterBlueprintAPIImpl::GetTrackerLocation(const FString& DeviceID, uint8 DeviceChannel, FVector& Location, bool& IsChannelAvailable)
{
	IsChannelAvailable = IDisplayCluster::Get().GetInputMgr()->GetTrackerLocation(DeviceID, DeviceChannel, Location);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetTrackerLocation - %s@%d avail=%d loc=%f"), *DeviceID, DeviceChannel, IsChannelAvailable ? 1 : 0, *FDisplayClusterTypesConverter::ToString(Location));
}

void UDisplayClusterBlueprintAPIImpl::GetTrackerQuat(const FString& DeviceID, uint8 DeviceChannel, FQuat& Rotation, bool& IsChannelAvailable)
{
	IsChannelAvailable = IDisplayCluster::Get().GetInputMgr()->GetTrackerQuat(DeviceID, DeviceChannel, Rotation);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetTrackerQuat - %s@%d avail=%d rot=%f"), *DeviceID, DeviceChannel, IsChannelAvailable ? 1 : 0, *FDisplayClusterTypesConverter::ToString(Rotation));
}


//////////////////////////////////////////////////////////////////////////////////////////////
// Render API
//////////////////////////////////////////////////////////////////////////////////////////////
void UDisplayClusterBlueprintAPIImpl::SetViewportCamera(const FString& CameraID, const FString& ViewportID)
{
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("SetViewportCamera - assigning camera '%s' to viewport '%s'"), *CameraID, *ViewportID);
	IDisplayCluster::Get().GetRenderMgr()->SetViewportCamera(CameraID, ViewportID);
}

bool UDisplayClusterBlueprintAPIImpl::GetBufferRatio(const FString& ViewportID, float& BufferRatio)
{
	const bool bResult = IDisplayCluster::Get().GetRenderMgr()->GetBufferRatio(ViewportID, BufferRatio);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetBufferRatio - viewport '%s' (%s) has buffer ratio %f"), *ViewportID, bResult ? DisplayClusterStrings::log::Found : DisplayClusterStrings::log::NotFound, BufferRatio);
	return bResult;
}

bool UDisplayClusterBlueprintAPIImpl::SetBufferRatio(const FString& ViewportID, float BufferRatio)
{
	const bool bResult = IDisplayCluster::Get().GetRenderMgr()->SetBufferRatio(ViewportID, BufferRatio);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("SetBufferRatio - set buffer ratio %f to viewport '%s'"), BufferRatio, *ViewportID);
	return bResult;
}

void UDisplayClusterBlueprintAPIImpl::SetStartPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& StartPostProcessingSettings)
{
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("SetStartPostProcessingSettings - id=%s"), *ViewportID);
	IDisplayCluster::Get().GetRenderMgr()->SetStartPostProcessingSettings(ViewportID, StartPostProcessingSettings);
}

void UDisplayClusterBlueprintAPIImpl::SetOverridePostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight)
{
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("SetOverridePostProcessingSettings - id=%s, weight=%f"), *ViewportID, BlendWeight);
	IDisplayCluster::Get().GetRenderMgr()->SetOverridePostProcessingSettings(ViewportID, OverridePostProcessingSettings, BlendWeight);
}

void UDisplayClusterBlueprintAPIImpl::SetFinalPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& FinalPostProcessingSettings)
{
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("SetFinalPostProcessingSettings - id=%s"), *ViewportID);
	IDisplayCluster::Get().GetRenderMgr()->SetFinalPostProcessingSettings(ViewportID, FinalPostProcessingSettings);
}

FPostProcessSettings UDisplayClusterBlueprintAPIImpl::GetUpdatedCinecameraPostProcessing(float DeltaSeconds, UCineCameraComponent* CineCamera)
{	
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetUpdatedCinecameraPostProcessing - dt=%f"), DeltaSeconds);
	return IDisplayCluster::Get().GetRenderMgr()->GetUpdatedCinecameraPostProcessing(DeltaSeconds, CineCamera);
}

bool UDisplayClusterBlueprintAPIImpl::GetViewportRect(const FString& ViewportID, FIntPoint& ViewportLoc, FIntPoint& ViewportSize)
{
	FIntRect ViewportRect;
	const bool bResult = IDisplayCluster::Get().GetRenderMgr()->GetViewportRect(ViewportID, ViewportRect);
	if (bResult)
	{
		ViewportLoc = ViewportRect.Min;
		ViewportSize = ViewportRect.Size();
	}

	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetViewportRect - id=%s, loc=%s, size=%s"), *ViewportID, *FDisplayClusterTypesConverter::ToString(ViewportLoc), *FDisplayClusterTypesConverter::ToString(ViewportSize));
	return bResult;
}

float UDisplayClusterBlueprintAPIImpl::GetInterpupillaryDistance(const FString& CameraID)
{
	const float EyeDistance = IDisplayCluster::Get().GetRenderMgr()->GetInterpupillaryDistance(CameraID);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetInterpupillaryDistance - camera=%s, dist=%f"), *CameraID, EyeDistance);
	return EyeDistance;
}

void  UDisplayClusterBlueprintAPIImpl::SetInterpupillaryDistance(const FString& CameraID, float EyeDistance)
{
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("SetInterpupillaryDistance - camera=%s, dist=%f"), *CameraID, EyeDistance);
	IDisplayCluster::Get().GetRenderMgr()->SetInterpupillaryDistance(CameraID, EyeDistance);
}

bool UDisplayClusterBlueprintAPIImpl::GetEyesSwap(const FString& CameraID)
{
	const bool bIsEyesSwapped = IDisplayCluster::Get().GetRenderMgr()->GetEyesSwap(CameraID);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("GetEyesSwap - camera=%s, swapped=%d"), *CameraID, bIsEyesSwapped ? 1 : 0);
	return bIsEyesSwapped;
}

void UDisplayClusterBlueprintAPIImpl::SetEyesSwap(const FString& CameraID, bool EyeSwapped)
{
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("SetEyesSwap - camera=%s, swapped=%d"), *CameraID, EyeSwapped ? 1 : 0);
	IDisplayCluster::Get().GetRenderMgr()->SetEyesSwap(CameraID, EyeSwapped);
}

bool UDisplayClusterBlueprintAPIImpl::ToggleEyesSwap(const FString& CameraID)
{
	const bool bIsEyeSwapped = IDisplayCluster::Get().GetRenderMgr()->ToggleEyesSwap(CameraID);
	UE_LOG(LogDisplayClusterBlueprint, Verbose, TEXT("ToggleEyesSwap - camera=%s, new swap state is %d"), *CameraID, bIsEyeSwapped ? 1 : 0);
	return bIsEyeSwapped;
}
