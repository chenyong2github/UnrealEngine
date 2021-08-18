// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "DisplayClusterEnums.h"
#include "Cluster/DisplayClusterClusterEvent.h"

#include "Engine/Scene.h"
#include "SceneViewExtension.h"

// Forward declarations for the following classes leads to a build error
#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterOriginComponent.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterScreenComponent.h"
#include "DisplayClusterConfigurationTypes.h"

#include "IDisplayClusterBlueprintAPI.generated.h"


class IDisplayClusterClusterEventListener;
class UCineCameraComponent;
struct FPostProcessSettings;


UINTERFACE(meta = (CannotImplementInterfaceInBlueprint))
class DISPLAYCLUSTER_API UDisplayClusterBlueprintAPI : public UInterface
{
	GENERATED_BODY()
};


/**
 * Blueprint API interface
 */
class DISPLAYCLUSTER_API IDisplayClusterBlueprintAPI
{
	GENERATED_BODY()

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// DisplayCluster module API
	//////////////////////////////////////////////////////////////////////////////////////////////
	/* Returns true if the module has been initialized. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is module initialized"), Category = "NDisplay")
	virtual bool IsModuleInitialized() const = 0;

	/** Returns current operation mode. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get operation mode"), Category = "NDisplay")
	virtual EDisplayClusterOperationMode GetOperationMode() const = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Cluster API
	//////////////////////////////////////////////////////////////////////////////////////////////
	/** Returns true if current node is a master node in a cluster. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is master node"), Category = "NDisplay|Cluster")
	virtual bool IsMaster() const = 0;
	
	/** Returns true if current node is a slave node in a cluster. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is slave node"), Category = "NDisplay|Cluster")
	virtual bool IsSlave() const = 0;

	/** Returns true if current node is a backup node in a cluster. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is backup node"), Category = "NDisplay|Cluster")
	virtual bool IsBackup() const = 0;

	/** Returns the role of the current cluster node. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get cluster role"), Category = "NDisplay|Cluster")
	virtual EDisplayClusterNodeRole GetClusterRole() const = 0;

	/** Returns the role of the current cluster node. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get cluster node IDs"), Category = "NDisplay|Cluster")
	virtual void GetNodeIds(TArray<FString>& OutNodeIds) const = 0;

	/** Returns cluster node name of the current application instance. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get node ID"), Category = "NDisplay|Cluster")
	virtual FString GetNodeId() const = 0;

	/** Returns amount of nodes in a cluster. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get nodes amount"), Category = "NDisplay|Cluster")
	virtual int32 GetNodesAmount() const = 0;

	/** Adds cluster event listener. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add cluster event listener"), Category = "NDisplay|Cluster")
	virtual void AddClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) = 0;

	/** Removes cluster event listener. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove cluster event listener"), Category = "NDisplay|Cluster")
	virtual void RemoveClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) = 0;

	/** Emits JSON cluster event. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Emit JSON cluster event"), Category = "NDisplay|Cluster")
	virtual void EmitClusterEventJson(const FDisplayClusterClusterEventJson& Event, bool bMasterOnly) = 0;

	/** Emits binary cluster event. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Emit binary cluster event"), Category = "NDisplay|Cluster")
	virtual void EmitClusterEventBinary(const FDisplayClusterClusterEventBinary& Event, bool bMasterOnly) = 0;

	/** Sends JSON cluster event to a specific target (outside of the cluster). */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Send JSON event to a specific host"), Category = "NDisplay|Cluster")
	virtual void SendClusterEventJsonTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventJson& Event, bool bMasterOnly) = 0;

	/** Sends binary cluster event to a specific target (outside of the cluster). */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Send binary event to a specific host"), Category = "NDisplay|Cluster")
	virtual void SendClusterEventBinaryTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventBinary& Event, bool bMasterOnly) = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Config API
	//////////////////////////////////////////////////////////////////////////////////////////////
	/** Return current configuration data */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get config data"), Category = "NDisplay|Cluster")
	virtual UDisplayClusterConfigurationData* GetConfig() const = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Game API
	//////////////////////////////////////////////////////////////////////////////////////////////
	/** Returns DisplayCluster root actor. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get root actor"), Category = "NDisplay|Game")
	virtual ADisplayClusterRootActor* GetRootActor() const = 0;

	// Screens
	/** Returns screen component by ID. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get screen by ID", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "NDisplay|Game")
	virtual UDisplayClusterScreenComponent* GetScreenById(const FString& ScreenID)
	{
		return nullptr;
	}

	/** Returns array of all screen components. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get all screens", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "NDisplay|Game")
	virtual TArray<UDisplayClusterScreenComponent*> GetAllScreens()
	{
		return TArray<UDisplayClusterScreenComponent*>();
	}

	/** Returns amount of screens defined in current configuration file. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of screens", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "NDisplay|Game")
	virtual int32 GetScreensAmount()
	{
		return 0;
	}

	// Cameras
	/** Returns array of all available camera components. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get all cameras", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "NDisplay|Game")
	virtual TArray<UDisplayClusterCameraComponent*> GetAllCameras()
	{
		return TArray<UDisplayClusterCameraComponent*>();
	}

	/** Returns camera component with specified ID. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get camera by ID", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "NDisplay|Game")
	virtual UDisplayClusterCameraComponent* GetCameraById(const FString& CameraID)
	{
		return nullptr;
	}

	/** Returns amount of cameras. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get cameras amount", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "NDisplay|Game")
	virtual int32 GetCamerasAmount()
	{
		return 0;
	}

	/** Returns default camera component. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get default camera", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "NDisplay|Game")
	virtual UDisplayClusterCameraComponent* GetDefaultCamera()
	{
		return nullptr;
	}

	/** Sets default camera component specified by ID. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set default camera by ID", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "NDisplay|Game")
	virtual void SetDefaultCameraById(const FString& CameraID)
	{ }

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Input API
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Device information
	/** Returns amount of VRPN axis devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN axis devices", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual int32 GetAxisDeviceAmount() const = 0;

	/** Returns amount of VRPN button devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN button devices", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual int32 GetButtonDeviceAmount() const = 0;

	/** Returns amount of VRPN tracker devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN tracker devices", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual int32 GetTrackerDeviceAmount() const = 0;

	/** Returns array of names of all VRPN axis devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN axis devices", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void GetAxisDeviceIds(TArray<FString>& DeviceIDs) const = 0;

	/** Returns array of names of all VRPN button devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN button devices", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void GetButtonDeviceIds(TArray<FString>& DeviceIDs) const = 0;

	/** Returns array of names of all keyboard devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of keyboard devices", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void GetKeyboardDeviceIds(TArray<FString>& DeviceIDs) const = 0;

	/** Returns array of names of all VRPN tracker devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN tracker devices", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void GetTrackerDeviceIds(TArray<FString>& DeviceIDs) const = 0;

	// Buttons
	/** Returns state of VRPN button at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN button state", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void GetButtonState(const FString& DeviceID, int32 DeviceChannel, bool& CurrentState, bool& IsChannelAvailable) const = 0;

	/** Returns whether VRPN button is pressed at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is VRPN button pressed", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void IsButtonPressed(const FString& DeviceID, int32 DeviceChannel, bool& IsPressedCurrently, bool& IsChannelAvailable) const = 0;

	/** Returns whether VRPN button is released at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is VRPN button released", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void IsButtonReleased(const FString& DeviceID, int32 DeviceChannel, bool& IsReleasedCurrently, bool& IsChannelAvailable) const = 0;

	/** Returns whether VRPN button was released at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Was VRPN button pressed", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void WasButtonPressed(const FString& DeviceID, int32 DeviceChannel, bool& WasPressed, bool& IsChannelAvailable) const = 0;

	/** Returns whether VRPN button was released at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Was VRPN button released", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void WasButtonReleased(const FString& DeviceID, int32 DeviceChannel, bool& WasReleased, bool& IsChannelAvailable) const = 0;

	// Axes
	/** Returns axis value at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN axis value", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void GetAxis(const FString& DeviceID, int32 DeviceChannel, float& Value, bool& IsAvailable) const = 0;

	// Trackers
	/** Returns tracker location values at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN tracker location", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void GetTrackerLocation(const FString& DeviceID, int32 DeviceChannel, FVector& Location, bool& IsChannelAvailable) const = 0;

	/** Returns tracker quaternion values at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN tracker rotation (as quaternion)", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void GetTrackerQuat(const FString& DeviceID, int32 DeviceChannel, FQuat& Rotation, bool& IsChannelAvailable) const = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Render API
	//////////////////////////////////////////////////////////////////////////////////////////////
	/** Binds camera to a viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set viewport camera", DeprecatedFunction, DeprecationMessage = "Use Configuration structures"), Category = "NDisplay|Render")
	virtual void SetViewportCamera(const FString& CameraId, const FString& ViewportId) = 0;

	/** Returns current buffer ratio for specified viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get viewport's buffer ratio", DeprecatedFunction, DeprecationMessage = "Use Configuration structures"), Category = "NDisplay|Render")
	virtual bool GetBufferRatio(const FString& ViewportId, float& BufferRatio) const = 0;

	/** Sets buffer ratio for specified viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set viewport's buffer ratio", DeprecatedFunction, DeprecationMessage = "Use Configuration structures"), Category = "NDisplay|Render")
	virtual bool SetBufferRatio(const FString& ViewportId, float BufferRatio) = 0;

	/** Overrides postprocess settings for specified viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set start post processing settings for viewport", DeprecatedFunction, DeprecationMessage = "Use Configuration structures"), Category = "NDisplay|Render")
	virtual void SetStartPostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& StartPostProcessingSettings) = 0;

	/** Overrides postprocess settings for specified viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set override post processing settings for viewport", DeprecatedFunction, DeprecationMessage = "Use Configuration structures"), Category = "NDisplay|Render")
	virtual void SetOverridePostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight = 1.0f) = 0;

	/** Overrides postprocess settings for specified viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set final post processing settings for viewport", DeprecatedFunction, DeprecationMessage = "Use Configuration structures"), Category = "NDisplay|Render")
	virtual void SetFinalPostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& FinalPostProcessingSettings) = 0;

	/** Returns location and size of specified viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get viewport rectangle", DeprecatedFunction, DeprecationMessage = "Use new api"), Category = "NDisplay|Render")
	virtual bool GetViewportRect(const FString& ViewportId, FIntPoint& ViewportLoc, FIntPoint& ViewportSize) const = 0;

	/** Returns list of local viewports. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get local viewports", DeprecatedFunction, DeprecationMessage = "Use new api"), Category = "NDisplay|Config")
	virtual void GetLocalViewports(TArray<FString>& ViewportIDs, TArray<FString>& ProjectionTypes, TArray<FIntPoint>& ViewportLocations, TArray<FIntPoint>& ViewportSizes) const = 0;

	/** Returns a functor that determines if any given scene view extension should be active in the given context for the current frame */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Scene View Extension Is Active In Context Function", DeprecatedFunction, DeprecationMessage = "Use Configuration structures"), Category = "NDisplay|Render")
	virtual void SceneViewExtensionIsActiveInContextFunction(const TArray<FString>& ViewportIDs, FSceneViewExtensionIsActiveFunctor& OutIsActiveFunction) const = 0;
};
