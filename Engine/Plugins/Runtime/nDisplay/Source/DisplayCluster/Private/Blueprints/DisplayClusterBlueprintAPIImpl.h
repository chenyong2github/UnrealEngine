// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Blueprints/IDisplayClusterBlueprintAPI.h"
#include "DisplayClusterBlueprintAPIImpl.generated.h"


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
	virtual bool IsModuleInitialized() const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get operation mode"), Category = "DisplayCluster")
	virtual EDisplayClusterOperationMode GetOperationMode() const override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Cluster API
	//////////////////////////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is master node"), Category = "DisplayCluster|Cluster")
	virtual bool IsMaster() const override;
	
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is slave node"), Category = "DisplayCluster|Cluster")
	virtual bool IsSlave() const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get node ID"), Category = "DisplayCluster|Cluster")
	virtual FString GetNodeId() const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get nodes amount"), Category = "DisplayCluster|Cluster")
	virtual int32 GetNodesAmount() const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add cluster event listener"), Category = "DisplayCluster|Cluster")
	virtual void AddClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove cluster event listener"), Category = "DisplayCluster|Cluster")
	virtual void RemoveClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Emit JSON cluster event"), Category = "DisplayCluster|Cluster")
	virtual void EmitClusterEventJson(const FDisplayClusterClusterEventJson& Event, bool bMasterOnly) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Emit binary cluster event"), Category = "DisplayCluster|Cluster")
	virtual void EmitClusterEventBinary(const FDisplayClusterClusterEventBinary& Event, bool bMasterOnly) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Config API
	//////////////////////////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get config data"), Category = "DisplayCluster|Cluster")
	virtual UDisplayClusterConfigurationData* GetConfig() const override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Game API
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Root
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get root actor"), Category = "DisplayCluster|Game")
	virtual ADisplayClusterRootActor* GetRootActor() const override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Input API
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Device information
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN axis devices"), Category = "DisplayCluster|Input")
	virtual int32 GetAxisDeviceAmount() const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN button devices"), Category = "DisplayCluster|Input")
	virtual int32 GetButtonDeviceAmount() const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN tracker devices"), Category = "DisplayCluster|Input")
	virtual int32 GetTrackerDeviceAmount() const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN axis devices"), Category = "DisplayCluster|Input")
	virtual void GetAxisDeviceIds(TArray<FString>& DeviceIDs) const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN button devices"), Category = "DisplayCluster|Input")
	virtual void GetButtonDeviceIds(TArray<FString>& DeviceIDs) const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of keyboard devices"), Category = "DisplayCluster|Input")
	virtual void GetKeyboardDeviceIds(TArray<FString>& DeviceIDs) const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN tracker devices"), Category = "DisplayCluster|Input")
	virtual void GetTrackerDeviceIds(TArray<FString>& DeviceIDs) const override;

	// Buttons
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN button state"), Category = "DisplayCluster|Input")
	virtual void GetButtonState(const FString& DeviceID, int32 DeviceChannel, bool& CurrentState, bool& IsChannelAvailable) const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is VRPN button pressed"), Category = "DisplayCluster|Input")
	virtual void IsButtonPressed(const FString& DeviceID, int32 DeviceChannel, bool& IsPressedCurrently, bool& IsChannelAvailable) const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is VRPN button released"), Category = "DisplayCluster|Input")
	virtual void IsButtonReleased(const FString& DeviceID, int32 DeviceChannel, bool& IsReleasedCurrently, bool& IsChannelAvailable) const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Was VRPN button pressed"), Category = "DisplayCluster|Input")
	virtual void WasButtonPressed(const FString& DeviceID, int32 DeviceChannel, bool& WasPressed, bool& IsChannelAvailable) const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Was VRPN button released"), Category = "DisplayCluster|Input")
	virtual void WasButtonReleased(const FString& DeviceID, int32 DeviceChannel, bool& WasReleased, bool& IsChannelAvailable) const override;

	// Axes
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN axis value"), Category = "DisplayCluster|Input")
	virtual void GetAxis(const FString& DeviceID, int32 DeviceChannel, float& Value, bool& IsAvailable) const override;

	// Trackers
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN tracker location"), Category = "DisplayCluster|Input")
	virtual void GetTrackerLocation(const FString& DeviceID, int32 DeviceChannel, FVector& Location, bool& IsChannelAvailable) const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN tracker rotation (as quaternion)"), Category = "DisplayCluster|Input")
	virtual void GetTrackerQuat(const FString& DeviceID, int32 DeviceChannel, FQuat& Rotation, bool& IsChannelAvailable) const override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Render API
	//////////////////////////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set viewport camera"), Category = "DisplayCluster|Render")
	virtual void SetViewportCamera(const FString& CameraId, const FString& ViewportId) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get viewport's buffer ratio"), Category = "DisplayCluster|Render")
	virtual bool GetBufferRatio(const FString& ViewportId, float& BufferRatio) const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set viewport's buffer ratio"), Category = "DisplayCluster|Render")
	virtual bool SetBufferRatio(const FString& ViewportId, float BufferRatio) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set start post processing settings for viewport"), Category = "DisplayCluster|Render")
	virtual void SetStartPostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& StartPostProcessingSettings) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set override post processing settings for viewport"), Category = "DisplayCluster|Render")
	virtual void SetOverridePostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight = 1.0f) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set final post processing settings for viewport"), Category = "DisplayCluster|Render")
	virtual void SetFinalPostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& FinalPostProcessingSettings) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Updated Post Processing"), Category = "DisplayCluster|Render")
	virtual bool GetViewportRect(const FString& ViewportId, FIntPoint& ViewportLoc, FIntPoint& ViewportSize) const override;

	/** Returns list of local viewports. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get local viewports"), Category = "DisplayCluster|Config")
	virtual void GetLocalViewports(TArray<FString>& ViewportIDs, TArray<FString>& ProjectionTypes, TArray<FIntPoint>& ViewportLocations, TArray<FIntPoint>& ViewportSizes) const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Scene View Extension Is Active In Context Function"), Category = "DisplayCluster|Render")
	virtual void SceneViewExtensionIsActiveInContextFunction(const TArray<FString>& ViewportIDs, FSceneViewExtensionIsActiveFunctor& OutIsActiveFunction) const override;
};
