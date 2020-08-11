// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprints/IDisplayClusterBlueprintAPI.h"
#include "DisplayClusterBlueprintAPIImpl.generated.h"

class UDisplayClusterRootComponent;

struct FDisplayClusterClusterEvent;
struct FPostProcessSettings;


/**
 * Blueprint API interface implementation
 */
UCLASS()
class DISPLAYCLUSTER_API UDisplayClusterBlueprintAPIImpl
	: public UObject
	, public IDisplayClusterBlueprintAPI
{
	GENERATED_BODY()

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// DisplayCluster module API
	//////////////////////////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is module initialized"), Category = "DisplayCluster")
	virtual bool IsModuleInitialized() override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get operation mode"), Category = "DisplayCluster")
	virtual EDisplayClusterOperationMode GetOperationMode() override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Cluster API
	//////////////////////////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is master node"), Category = "DisplayCluster|Cluster")
	virtual bool IsMaster() override;
	
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is slave node"), Category = "DisplayCluster|Cluster")
	virtual bool IsSlave() override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is cluster mode"), Category = "DisplayCluster|Cluster")
	virtual bool IsCluster() override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is standalone mode"), Category = "DisplayCluster|Cluster")
	virtual bool IsStandalone() override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get node ID"), Category = "DisplayCluster|Cluster")
	virtual FString GetNodeId() override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get nodes amount"), Category = "DisplayCluster|Cluster")
	virtual int32 GetNodesAmount() override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add cluster event listener"), Category = "DisplayCluster|Cluster")
	virtual void AddClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove cluster event listener"), Category = "DisplayCluster|Cluster")
	virtual void RemoveClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Emit cluster event"), Category = "DisplayCluster|Cluster")
	virtual void EmitClusterEvent(const FDisplayClusterClusterEvent& Event, bool MasterOnly) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Config API
	//////////////////////////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get local viewports"), Category = "DisplayCluster|Config")
	virtual void GetLocalViewports(bool IsRTT, TArray<FString>& ViewportIDs, TArray<FString>& ViewportTypes, TArray<FIntPoint>& ViewportLocations, TArray<FIntPoint>& ViewportSizes) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Game API
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Root
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get root actor"), Category = "DisplayCluster|Game")
	virtual ADisplayClusterRootActor* GetRootActor() override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get root component"), Category = "DisplayCluster|Game")
	virtual UDisplayClusterRootComponent* GetRootComponent() override;

	// Screens
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get screen by ID"), Category = "DisplayCluster|Game")
	virtual UDisplayClusterScreenComponent* GetScreenById(const FString& ScreenID) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get all screens"), Category = "DisplayCluster|Game")
	virtual TArray<UDisplayClusterScreenComponent*> GetAllScreens() override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of screens"), Category = "DisplayCluster|Game")
	virtual int32 GetScreensAmount() override;

	// Cameras
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get all cameras"), Category = "DisplayCluster|Game")
	virtual TArray<UDisplayClusterCameraComponent*> GetAllCameras() override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get camera by ID"), Category = "DisplayCluster|Game")
	virtual UDisplayClusterCameraComponent* GetCameraById(const FString& CameraID) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get cameras amount"), Category = "DisplayCluster|Game")
	virtual int32 GetCamerasAmount() override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get default camera"), Category = "DisplayCluster|Game")
	virtual UDisplayClusterCameraComponent* GetDefaultCamera() override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set default camera by ID"), Category = "DisplayCluster|Game")
	virtual void SetDefaultCameraById(const FString& CameraID) override;

	// Nodes
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get node by ID"), Category = "DisplayCluster|Game")
	virtual UDisplayClusterSceneComponent* GetNodeById(const FString& SceneNodeID) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get all nodes"), Category = "DisplayCluster|Game")
	virtual TArray<UDisplayClusterSceneComponent*> GetAllNodes() override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Input API
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Device information
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN axis devices"), Category = "DisplayCluster|Input")
	virtual int32 GetAxisDeviceAmount() override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN button devices"), Category = "DisplayCluster|Input")
	virtual int32 GetButtonDeviceAmount() override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN tracker devices"), Category = "DisplayCluster|Input")
	virtual int32 GetTrackerDeviceAmount() override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN axis devices"), Category = "DisplayCluster|Input")
	virtual void GetAxisDeviceIds(TArray<FString>& DeviceIDs) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN button devices"), Category = "DisplayCluster|Input")
	virtual void GetButtonDeviceIds(TArray<FString>& DeviceIDs) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of keyboard devices"), Category = "DisplayCluster|Input")
	virtual void GetKeyboardDeviceIds(TArray<FString>& DeviceIDs) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN tracker devices"), Category = "DisplayCluster|Input")
	virtual void GetTrackerDeviceIds(TArray<FString>& DeviceIDs) override;

	// Buttons
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN button state"), Category = "DisplayCluster|Input")
	virtual void GetButtonState(const FString& DeviceID, uint8 DeviceChannel, bool& CurrentState, bool& IsChannelAvailable) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is VRPN button pressed"), Category = "DisplayCluster|Input")
	virtual void IsButtonPressed(const FString& DeviceID, uint8 DeviceChannel, bool& IsPressedCurrently, bool& IsChannelAvailable) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is VRPN button released"), Category = "DisplayCluster|Input")
	virtual void IsButtonReleased(const FString& DeviceID, uint8 DeviceChannel, bool& IsReleasedCurrently, bool& IsChannelAvailable) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Was VRPN button pressed"), Category = "DisplayCluster|Input")
	virtual void WasButtonPressed(const FString& DeviceID, uint8 DeviceChannel, bool& WasPressed, bool& IsChannelAvailable) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Was VRPN button released"), Category = "DisplayCluster|Input")
	virtual void WasButtonReleased(const FString& DeviceID, uint8 DeviceChannel, bool& WasReleased, bool& IsChannelAvailable) override;

	// Axes
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN axis value"), Category = "DisplayCluster|Input")
	virtual void GetAxis(const FString& DeviceID, uint8 DeviceChannel, float& Value, bool& IsAvailable) override;

	// Trackers
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN tracker location"), Category = "DisplayCluster|Input")
	virtual void GetTrackerLocation(const FString& DeviceID, uint8 DeviceChannel, FVector& Location, bool& IsChannelAvailable) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN tracker rotation (as quaternion)"), Category = "DisplayCluster|Input")
	virtual void GetTrackerQuat(const FString& DeviceID, uint8 DeviceChannel, FQuat& Rotation, bool& IsChannelAvailable) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Render API
	//////////////////////////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set viewport camera"), Category = "DisplayCluster|Render")
	virtual void SetViewportCamera(const FString& CameraId, const FString& ViewportId) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get viewport's buffer ratio"), Category = "DisplayCluster|Render")
	virtual bool GetBufferRatio(const FString& ViewportId, float& BufferRatio) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set viewport's buffer ratio"), Category = "DisplayCluster|Render")
	virtual bool SetBufferRatio(const FString& ViewportId, float BufferRatio) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set start post processing settings for viewport"), Category = "DisplayCluster|Render")
	virtual void SetStartPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& StartPostProcessingSettings) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set start post override settings for viewport"), Category = "DisplayCluster|Render")
	virtual void SetOverridePostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight = 1.0f) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set start post processing settings for viewport"), Category = "DisplayCluster|Render")
	virtual void SetFinalPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& FinalPostProcessingSettings) override;

	/** Overrides postprocess settings for specified viewport. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Updates Cinecamera"), Category = "DisplayCluster|Render")
	virtual FPostProcessSettings GetUpdatedCinecameraPostProcessing(float DeltaSeconds, UCineCameraComponent* CineCamera);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Updated Post Processing"), Category = "DisplayCluster|Render")
	virtual bool GetViewportRect(const FString& ViewportID, FIntPoint& ViewportLoc, FIntPoint& ViewportSize) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Render/Camera API
	//////////////////////////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get interpuppillary distance"), Category = "DisplayCluster|Render|Camera")
	virtual float GetInterpupillaryDistance(const FString& CameraId) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set interpuppillary distance"), Category = "DisplayCluster|Render|Camera")
	virtual void SetInterpupillaryDistance(const FString& CameraId, float EyeDistance) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get eye swap"), Category = "DisplayCluster|Render|Camera")
	virtual bool GetEyesSwap(const FString& CameraId) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set eye swap"), Category = "DisplayCluster|Render|Camera")
	virtual void SetEyesSwap(const FString& CameraId, bool EyeSwapped) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Toggle eye swap"), Category = "DisplayCluster|Render|Camera")
	virtual bool ToggleEyesSwap(const FString& CameraId) override;
};
