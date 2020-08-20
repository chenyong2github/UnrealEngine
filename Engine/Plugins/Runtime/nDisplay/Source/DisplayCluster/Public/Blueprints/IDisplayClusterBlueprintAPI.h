// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "DisplayClusterEnums.h"
#include "Cluster/DisplayClusterClusterEvent.h"
#include "Cluster/IDisplayClusterClusterEventListener.h"

#include "Config/DisplayClusterConfigTypes.h"
#include "Engine/Scene.h"

#include "IDisplayClusterBlueprintAPI.generated.h"

class UDisplayClusterRootComponent;
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
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is module initialized"), Category = "DisplayCluster")
	virtual bool IsModuleInitialized() = 0;

	/** Returns current operation mode. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get operation mode"), Category = "DisplayCluster")
	virtual EDisplayClusterOperationMode GetOperationMode() = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Cluster API
	//////////////////////////////////////////////////////////////////////////////////////////////
	/** Returns true if current node is a master computer in a cluster. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is master node"), Category = "DisplayCluster|Cluster")
	virtual bool IsMaster() = 0;
	
	/** Returns true if current node is a slave computer in a cluster. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is slave node"), Category = "DisplayCluster|Cluster")
	virtual bool IsSlave() = 0;

	/** Returns true if current application is running in cluster mode. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is cluster mode"), Category = "DisplayCluster|Cluster")
	virtual bool IsCluster() = 0;

	/** Returns true if current application is running in standalone mode. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is standalone mode"), Category = "DisplayCluster|Cluster")
	virtual bool IsStandalone() = 0;

	/** Returns cluster node name of the current application instance. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get node ID"), Category = "DisplayCluster|Cluster")
	virtual FString GetNodeId() = 0;

	/** Returns amount of nodes in a cluster. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get nodes amount"), Category = "DisplayCluster|Cluster")
	virtual int32 GetNodesAmount() = 0;

	/** Adds cluster event listener. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add cluster event listener"), Category = "DisplayCluster|Cluster")
	virtual void AddClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) = 0;

	/** Removes cluster event listener. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove cluster event listener"), Category = "DisplayCluster|Cluster")
	virtual void RemoveClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) = 0;

	/** Emits cluster event. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Emit cluster event"), Category = "DisplayCluster|Cluster")
	virtual void EmitClusterEvent(const FDisplayClusterClusterEvent& Event, bool MasterOnly) = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Config API
	//////////////////////////////////////////////////////////////////////////////////////////////
	/** Returns list of local viewports. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get local viewports"), Category = "DisplayCluster|Config")
	virtual void GetLocalViewports(bool IsRTT, TArray<FString>& ViewportIDs, TArray<FString>& ViewportTypes, TArray<FIntPoint>& ViewportLocations, TArray<FIntPoint>& ViewportSizes) = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Game API
	//////////////////////////////////////////////////////////////////////////////////////////////
	/** Returns DisplayCluster root actor. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get root actor"), Category = "DisplayCluster|Game")
	virtual ADisplayClusterRootActor* GetRootActor() = 0;

	/** Returns DisplayCluster root component. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get root component"), Category = "DisplayCluster|Game")
	virtual UDisplayClusterRootComponent* GetRootComponent() = 0;

	// Screens
	/** Returns screen component by ID. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get screen by ID"), Category = "DisplayCluster|Game")
	virtual UDisplayClusterScreenComponent* GetScreenById(const FString& ScreenID) = 0;

	/** Returns array of all screen components. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get all screens"), Category = "DisplayCluster|Game")
	virtual TArray<UDisplayClusterScreenComponent*> GetAllScreens() = 0;

	/** Returns amount of screens defined in current configuration file. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of screens"), Category = "DisplayCluster|Game")
	virtual int32 GetScreensAmount() = 0;

	// Cameras
	/** Returns array of all available camera components. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get all cameras"), Category = "DisplayCluster|Game")
	virtual TArray<UDisplayClusterCameraComponent*> GetAllCameras() = 0;

	/** Returns camera component with specified ID. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get camera by ID"), Category = "DisplayCluster|Game")
	virtual UDisplayClusterCameraComponent* GetCameraById(const FString& CameraID) = 0;

	/** Returns amount of cameras. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get cameras amount"), Category = "DisplayCluster|Game")
	virtual int32 GetCamerasAmount() = 0;

	/** Returns default camera component. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get default camera"), Category = "DisplayCluster|Game")
	virtual UDisplayClusterCameraComponent* GetDefaultCamera() = 0;

	/** Sets default camera component specified by ID. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set default camera by ID"), Category = "DisplayCluster|Game")
	virtual void SetDefaultCameraById(const FString& CameraID) = 0;

	// Nodes
	/** Returns scene component by its ID. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get node by ID"), Category = "DisplayCluster|Game")
	virtual UDisplayClusterSceneComponent* GetNodeById(const FString& SceneNodeID) = 0;

	/** Returns array of all scene components (nodes). */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get all nodes"), Category = "DisplayCluster|Game")
	virtual TArray<UDisplayClusterSceneComponent*> GetAllNodes() = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Input API
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Device information
	/** Returns amount of VRPN axis devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN axis devices"), Category = "DisplayCluster|Input")
	virtual int32 GetAxisDeviceAmount() = 0;

	/** Returns amount of VRPN button devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN button devices"), Category = "DisplayCluster|Input")
	virtual int32 GetButtonDeviceAmount() = 0;

	/** Returns amount of VRPN tracker devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN tracker devices"), Category = "DisplayCluster|Input")
	virtual int32 GetTrackerDeviceAmount() = 0;

	/** Returns array of names of all VRPN axis devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN axis devices"), Category = "DisplayCluster|Input")
	virtual void GetAxisDeviceIds(TArray<FString>& DeviceIDs) = 0;

	/** Returns array of names of all VRPN button devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN button devices"), Category = "DisplayCluster|Input")
	virtual void GetButtonDeviceIds(TArray<FString>& DeviceIDs) = 0;

	/** Returns array of names of all keyboard devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of keyboard devices"), Category = "DisplayCluster|Input")
	virtual void GetKeyboardDeviceIds(TArray<FString>& DeviceIDs) = 0;

	/** Returns array of names of all VRPN tracker devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN tracker devices"), Category = "DisplayCluster|Input")
	virtual void GetTrackerDeviceIds(TArray<FString>& DeviceIDs) = 0;

	// Buttons
	/** Returns state of VRPN button at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN button state"), Category = "DisplayCluster|Input")
	virtual void GetButtonState(const FString& DeviceID, uint8 DeviceChannel, bool& CurrentState, bool& IsChannelAvailable) = 0;

	/** Returns whether VRPN button is pressed at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is VRPN button pressed"), Category = "DisplayCluster|Input")
	virtual void IsButtonPressed(const FString& DeviceID, uint8 DeviceChannel, bool& IsPressedCurrently, bool& IsChannelAvailable) = 0;

	/** Returns whether VRPN button is released at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is VRPN button released"), Category = "DisplayCluster|Input")
	virtual void IsButtonReleased(const FString& DeviceID, uint8 DeviceChannel, bool& IsReleasedCurrently, bool& IsChannelAvailable) = 0;

	/** Returns whether VRPN button was released at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Was VRPN button pressed"), Category = "DisplayCluster|Input")
	virtual void WasButtonPressed(const FString& DeviceID, uint8 DeviceChannel, bool& WasPressed, bool& IsChannelAvailable) = 0;

	/** Returns whether VRPN button was released at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Was VRPN button released"), Category = "DisplayCluster|Input")
	virtual void WasButtonReleased(const FString& DeviceID, uint8 DeviceChannel, bool& WasReleased, bool& IsChannelAvailable) = 0;

	// Axes
	/** Returns axis value at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN axis value"), Category = "DisplayCluster|Input")
	virtual void GetAxis(const FString& DeviceID, uint8 DeviceChannel, float& Value, bool& IsAvailable) = 0;

	// Trackers
	/** Returns tracker location values at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN tracker location"), Category = "DisplayCluster|Input")
	virtual void GetTrackerLocation(const FString& DeviceID, uint8 DeviceChannel, FVector& Location, bool& IsChannelAvailable) = 0;

	/** Returns tracker quaternion values at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN tracker rotation (as quaternion)"), Category = "DisplayCluster|Input")
	virtual void GetTrackerQuat(const FString& DeviceID, uint8 DeviceChannel, FQuat& Rotation, bool& IsChannelAvailable) = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Render API
	//////////////////////////////////////////////////////////////////////////////////////////////
	/** Binds camera to a viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set viewport camera"), Category = "DisplayCluster|Render")
	virtual void SetViewportCamera(const FString& CameraID, const FString& ViewportID) = 0;

	/** Returns current buffer ratio for specified viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get viewport's buffer ratio"), Category = "DisplayCluster|Render")
	virtual bool GetBufferRatio(const FString& ViewportID, float& BufferRatio) = 0;
	
	/** Sets buffer ratio for specified viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set viewport's buffer ratio"), Category = "DisplayCluster|Render")
	virtual bool SetBufferRatio(const FString& ViewportID, float BufferRatio) = 0;

	/** Overrides postprocess settings for specified viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set start post processing settings for viewport"), Category = "DisplayCluster|Render")
	virtual void SetStartPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& StartPostProcessingSettings) = 0;
	
	/** Overrides postprocess settings for specified viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set override post processing settings for viewport"), Category = "DisplayCluster|Render")
	virtual void SetOverridePostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight = 1.0f) = 0;
	
	/** Overrides postprocess settings for specified viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set final post processing settings for viewport"), Category = "DisplayCluster|Render")
	virtual void SetFinalPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& FinalPostProcessingSettings) = 0;

	/** Overrides postprocess settings for specified viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Updated Post Processing"), Category = "DisplayCluster|Render")
	virtual FPostProcessSettings GetUpdatedCinecameraPostProcessing(float DeltaSeconds, UCineCameraComponent* CineCamera) = 0;

	/** Returns location and size of specified viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get viewport rectangle"), Category = "DisplayCluster|Render")
	virtual bool GetViewportRect(const FString& ViewportID, FIntPoint& ViewportLoc, FIntPoint& ViewportSize) = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Render/Camera API
	//////////////////////////////////////////////////////////////////////////////////////////////
	/** Returns interpupillary distance (eye separation) for stereoscopic rendering. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get interpuppillary distance"), Category = "DisplayCluster|Render|Camera")
	virtual float GetInterpupillaryDistance(const FString& CameraID) = 0;

	/** Sets interpupillary distance (eye separation) for stereoscopic rendering. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set interpuppillary distance"), Category = "DisplayCluster|Render|Camera")
	virtual void SetInterpupillaryDistance(const FString& CameraID, float EyeDistance) = 0;

	/** Gets Swap eye rendering state. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get eye swap"), Category = "DisplayCluster|Render|Camera")
	virtual bool GetEyesSwap(const FString& CameraID) = 0;

	/** Sets Swap eye rendering state. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set eye swap"), Category = "DisplayCluster|Render|Camera")
	virtual void SetEyesSwap(const FString& CameraID, bool EyeSwapped) = 0;

	/** Toggles current eye swap state. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Toggle eye swap"), Category = "DisplayCluster|Render|Camera")
	virtual bool ToggleEyesSwap(const FString& CameraID) = 0;
};
