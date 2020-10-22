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
#include "Components/DisplayClusterRootComponent.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterMeshComponent.h"
#include "Components/DisplayClusterSceneComponent.h"
#include "Components/DisplayClusterScreenComponent.h"
#include "Components/DisplayClusterXformComponent.h"
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
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is module initialized"), Category = "DisplayCluster")
	virtual bool IsModuleInitialized() const = 0;

	/** Returns current operation mode. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get operation mode"), Category = "DisplayCluster")
	virtual EDisplayClusterOperationMode GetOperationMode() const = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Cluster API
	//////////////////////////////////////////////////////////////////////////////////////////////
	/** Returns true if current node is a master computer in a cluster. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is master node"), Category = "DisplayCluster|Cluster")
	virtual bool IsMaster() const = 0;
	
	/** Returns true if current node is a slave computer in a cluster. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is slave node"), Category = "DisplayCluster|Cluster")
	virtual bool IsSlave() const = 0;

	/** Returns true if current application is running in cluster mode. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is cluster mode", DeprecatedFunction, DeprecationMessage = "This feature is no longer supported."), Category = "DisplayCluster|Cluster")
	virtual bool IsCluster()
	{
		return false;
	}

	/** Returns true if current application is running in standalone mode. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is standalone mode", DeprecatedFunction, DeprecationMessage = "This feature is no longer supported."), Category = "DisplayCluster|Cluster")
	virtual bool IsStandalone()
	{
		return false;
	}

	/** Returns cluster node name of the current application instance. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get node ID"), Category = "DisplayCluster|Cluster")
	virtual FString GetNodeId() const = 0;

	/** Returns amount of nodes in a cluster. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get nodes amount"), Category = "DisplayCluster|Cluster")
	virtual int32 GetNodesAmount() const = 0;

	/** Adds cluster event listener. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add cluster event listener"), Category = "DisplayCluster|Cluster")
	virtual void AddClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) = 0;

	/** Removes cluster event listener. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove cluster event listener"), Category = "DisplayCluster|Cluster")
	virtual void RemoveClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) = 0;

	/** Emits cluster event. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Emit cluster event", DeprecatedFunction, DeprecationMessage = "Please, use EmitClusterEventJson"), Category = "DisplayCluster|Cluster")
	virtual void EmitClusterEvent(const FDisplayClusterClusterEvent& Event, bool MasterOnly)
	{ }

	/** Emits cluster event. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Emit JSON cluster event"), Category = "DisplayCluster|Cluster")
	virtual void EmitClusterEventJson(const FDisplayClusterClusterEventJson& Event, bool bMasterOnly) = 0;

	/** Emits cluster event. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Emit binary cluster event"), Category = "DisplayCluster|Cluster")
	virtual void EmitClusterEventBinary(const FDisplayClusterClusterEventBinary& Event, bool bMasterOnly) = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Config API
	//////////////////////////////////////////////////////////////////////////////////////////////
	/** Return current configuration data */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get config data"), Category = "DisplayCluster|Cluster")
	virtual UDisplayClusterConfigurationData* GetConfig() const = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Game API
	//////////////////////////////////////////////////////////////////////////////////////////////
	/** Returns DisplayCluster root actor. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get root actor"), Category = "DisplayCluster|Game")
	virtual ADisplayClusterRootActor* GetRootActor() const = 0;

	/** Returns DisplayCluster root component. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get root component", DeprecatedFunction, DeprecationMessage = "UDisplayClusterRootComponent has been deprecated. Please use ADisplayClusterRootActor."), Category = "DisplayCluster|Game")
	virtual UDisplayClusterRootComponent* GetRootComponent() const
	{
		return nullptr;
	}

	// Screens
	/** Returns screen component by ID. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get screen by ID", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "DisplayCluster|Game")
	virtual UDisplayClusterScreenComponent* GetScreenById(const FString& ScreenID)
	{
		return nullptr;
	}

	/** Returns array of all screen components. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get all screens", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "DisplayCluster|Game")
	virtual TArray<UDisplayClusterScreenComponent*> GetAllScreens()
	{
		return TArray<UDisplayClusterScreenComponent*>();
	}

	/** Returns amount of screens defined in current configuration file. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of screens", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "DisplayCluster|Game")
	virtual int32 GetScreensAmount()
	{
		return 0;
	}

	// Cameras
	/** Returns array of all available camera components. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get all cameras", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "DisplayCluster|Game")
	virtual TArray<UDisplayClusterCameraComponent*> GetAllCameras()
	{
		return TArray<UDisplayClusterCameraComponent*>();
	}

	/** Returns camera component with specified ID. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get camera by ID", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "DisplayCluster|Game")
	virtual UDisplayClusterCameraComponent* GetCameraById(const FString& CameraID)
	{
		return nullptr;
	}

	/** Returns amount of cameras. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get cameras amount", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "DisplayCluster|Game")
	virtual int32 GetCamerasAmount()
	{
		return 0;
	}

	/** Returns default camera component. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get default camera", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "DisplayCluster|Game")
	virtual UDisplayClusterCameraComponent* GetDefaultCamera()
	{
		return nullptr;
	}

	/** Sets default camera component specified by ID. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set default camera by ID", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "DisplayCluster|Game")
	virtual void SetDefaultCameraById(const FString& CameraID)
	{ }

	// Scene components
	/** Returns scene component by its ID. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get node by ID", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "DisplayCluster|Game")
	virtual UDisplayClusterSceneComponent* GetNodeById(const FString& SceneNodeID)
	{
		return nullptr;
	}

	/** Returns array of all scene components (nodes). */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get all nodes", DeprecatedFunction, DeprecationMessage = "This function has been moved to ADisplayClusterRootActor."), Category = "DisplayCluster|Game")
	virtual TArray<UDisplayClusterSceneComponent*> GetAllNodes()
	{
		return TArray<UDisplayClusterSceneComponent*>();
	}

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Input API
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Device information
	/** Returns amount of VRPN axis devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN axis devices"), Category = "DisplayCluster|Input")
	virtual int32 GetAxisDeviceAmount() const = 0;

	/** Returns amount of VRPN button devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN button devices"), Category = "DisplayCluster|Input")
	virtual int32 GetButtonDeviceAmount() const = 0;

	/** Returns amount of VRPN tracker devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN tracker devices"), Category = "DisplayCluster|Input")
	virtual int32 GetTrackerDeviceAmount() const = 0;

	/** Returns array of names of all VRPN axis devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN axis devices"), Category = "DisplayCluster|Input")
	virtual void GetAxisDeviceIds(TArray<FString>& DeviceIDs) const = 0;

	/** Returns array of names of all VRPN button devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN button devices"), Category = "DisplayCluster|Input")
	virtual void GetButtonDeviceIds(TArray<FString>& DeviceIDs) const = 0;

	/** Returns array of names of all keyboard devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of keyboard devices"), Category = "DisplayCluster|Input")
	virtual void GetKeyboardDeviceIds(TArray<FString>& DeviceIDs) const = 0;

	/** Returns array of names of all VRPN tracker devices. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN tracker devices"), Category = "DisplayCluster|Input")
	virtual void GetTrackerDeviceIds(TArray<FString>& DeviceIDs) const = 0;

	// Buttons
	/** Returns state of VRPN button at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN button state"), Category = "DisplayCluster|Input")
	virtual void GetButtonState(const FString& DeviceID, int32 DeviceChannel, bool& CurrentState, bool& IsChannelAvailable) const = 0;

	/** Returns whether VRPN button is pressed at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is VRPN button pressed"), Category = "DisplayCluster|Input")
	virtual void IsButtonPressed(const FString& DeviceID, int32 DeviceChannel, bool& IsPressedCurrently, bool& IsChannelAvailable) const = 0;

	/** Returns whether VRPN button is released at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is VRPN button released"), Category = "DisplayCluster|Input")
	virtual void IsButtonReleased(const FString& DeviceID, int32 DeviceChannel, bool& IsReleasedCurrently, bool& IsChannelAvailable) const = 0;

	/** Returns whether VRPN button was released at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Was VRPN button pressed"), Category = "DisplayCluster|Input")
	virtual void WasButtonPressed(const FString& DeviceID, int32 DeviceChannel, bool& WasPressed, bool& IsChannelAvailable) const = 0;

	/** Returns whether VRPN button was released at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Was VRPN button released"), Category = "DisplayCluster|Input")
	virtual void WasButtonReleased(const FString& DeviceID, int32 DeviceChannel, bool& WasReleased, bool& IsChannelAvailable) const = 0;

	// Axes
	/** Returns axis value at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN axis value"), Category = "DisplayCluster|Input")
	virtual void GetAxis(const FString& DeviceID, int32 DeviceChannel, float& Value, bool& IsAvailable) const = 0;

	// Trackers
	/** Returns tracker location values at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN tracker location"), Category = "DisplayCluster|Input")
	virtual void GetTrackerLocation(const FString& DeviceID, int32 DeviceChannel, FVector& Location, bool& IsChannelAvailable) const = 0;

	/** Returns tracker quaternion values at specified device and channel. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN tracker rotation (as quaternion)"), Category = "DisplayCluster|Input")
	virtual void GetTrackerQuat(const FString& DeviceID, int32 DeviceChannel, FQuat& Rotation, bool& IsChannelAvailable) const = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Render API
	//////////////////////////////////////////////////////////////////////////////////////////////
	/** Binds camera to a viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set viewport camera"), Category = "DisplayCluster|Render")
	virtual void SetViewportCamera(const FString& CameraID, const FString& ViewportID) = 0;

	/** Returns current buffer ratio for specified viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get viewport's buffer ratio"), Category = "DisplayCluster|Render")
	virtual bool GetBufferRatio(const FString& ViewportID, float& BufferRatio) const = 0;
	
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

	/** Returns location and size of specified viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get viewport rectangle"), Category = "DisplayCluster|Render")
	virtual bool GetViewportRect(const FString& ViewportID, FIntPoint& ViewportLoc, FIntPoint& ViewportSize) const = 0;

	/** Returns list of local viewports. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get local viewports"), Category = "DisplayCluster|Config")
	virtual void GetLocalViewports(TArray<FString>& ViewportIDs, TArray<FString>& ViewportTypes, TArray<FIntPoint>& ViewportLocations, TArray<FIntPoint>& ViewportSizes) const = 0;

	/** Returns a functor that determines if any given scene view extension should be active in the given context for the current frame */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Scene View Extension Is Active In Context Function"), Category = "DisplayCluster|Render")
	virtual void SceneViewExtensionIsActiveInContextFunction(const TArray<FString>& ViewportIDs, FSceneViewExtensionIsActiveFunctor& OutIsActiveFunction) const = 0;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Render/Camera API
	//////////////////////////////////////////////////////////////////////////////////////////////
	/** Returns interpupillary distance (eye separation) for stereoscopic rendering. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get interpuppillary distance", DeprecatedFunction, DeprecationMessage = "This function has been moved to UDisplayClusterCameraComponent."), Category = "DisplayCluster|Render|Camera")
	virtual float GetInterpupillaryDistance(const FString& CameraID)
	{
		return 0.f;
	}

	/** Sets interpupillary distance (eye separation) for stereoscopic rendering. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set interpuppillary distance", DeprecatedFunction, DeprecationMessage = "This function has been moved to UDisplayClusterCameraComponent."), Category = "DisplayCluster|Render|Camera")
	virtual void SetInterpupillaryDistance(const FString& CameraID, float EyeDistance)
	{ }

	/** Gets Swap eye rendering state. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get eye swap", DeprecatedFunction, DeprecationMessage = "This function has been moved to UDisplayClusterCameraComponent."), Category = "DisplayCluster|Render|Camera")
	virtual bool GetEyesSwap(const FString& CameraID)
	{
		return false;
	}

	/** Sets Swap eye rendering state. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set eye swap", DeprecatedFunction, DeprecationMessage = "This function has been moved to UDisplayClusterCameraComponent."), Category = "DisplayCluster|Render|Camera")
	virtual void SetEyesSwap(const FString& CameraID, bool EyeSwapped)
	{ }

	/** Toggles current eye swap state. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Toggle eye swap", DeprecatedFunction, DeprecationMessage = "This function has been moved to UDisplayClusterCameraComponent."), Category = "DisplayCluster|Render|Camera")
	virtual bool ToggleEyesSwap(const FString& CameraID)
	{
		return false;
	}
};
