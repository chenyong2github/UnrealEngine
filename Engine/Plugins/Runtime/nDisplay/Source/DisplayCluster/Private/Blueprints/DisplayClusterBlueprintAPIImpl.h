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
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is module initialized"), Category = "NDisplay")
	virtual bool IsModuleInitialized() const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get operation mode"), Category = "NDisplay")
	virtual EDisplayClusterOperationMode GetOperationMode() const override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Cluster API
	//////////////////////////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is master node"), Category = "NDisplay|Cluster")
	virtual bool IsMaster() const override;
	
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is slave node"), Category = "NDisplay|Cluster")
	virtual bool IsSlave() const override;

	/** Returns true if current node is a backup node in a cluster. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is backup node"), Category = "NDisplay|Cluster")
	virtual bool IsBackup() const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get cluster role"), Category = "NDisplay|Cluster")
	virtual EDisplayClusterNodeRole GetClusterRole() const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get cluster node IDs"), Category = "NDisplay|Cluster")
	virtual void GetNodeIds(TArray<FString>& OutNodeIds) const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get node ID"), Category = "NDisplay|Cluster")
	virtual FString GetNodeId() const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get nodes amount"), Category = "NDisplay|Cluster")
	virtual int32 GetNodesAmount() const override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add cluster event listener"), Category = "NDisplay|Cluster")
	virtual void AddClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Remove cluster event listener"), Category = "NDisplay|Cluster")
	virtual void RemoveClusterEventListener(TScriptInterface<IDisplayClusterClusterEventListener> Listener) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Emit JSON cluster event"), Category = "NDisplay|Cluster")
	virtual void EmitClusterEventJson(const FDisplayClusterClusterEventJson& Event, bool bMasterOnly) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Emit binary cluster event"), Category = "NDisplay|Cluster")
	virtual void EmitClusterEventBinary(const FDisplayClusterClusterEventBinary& Event, bool bMasterOnly) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Emit JSON cluster event"), Category = "NDisplay|Cluster")
	virtual void SendClusterEventJsonTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventJson& Event, bool bMasterOnly) override;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Emit binary cluster event"), Category = "NDisplay|Cluster")
	virtual void SendClusterEventBinaryTo(const FString& Address, const int32 Port, const FDisplayClusterClusterEventBinary& Event, bool bMasterOnly) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Config API
	//////////////////////////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get config data"), Category = "NDisplay|Cluster")
	virtual UDisplayClusterConfigurationData* GetConfig() const override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Game API
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Root
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get root actor"), Category = "NDisplay|Game")
	virtual ADisplayClusterRootActor* GetRootActor() const override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Input API
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Device information
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN axis devices", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual int32 GetAxisDeviceAmount() const override
	{
		return 0;
	}

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN button devices", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual int32 GetButtonDeviceAmount() const override
	{
		return 0;
	}

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get amount of VRPN tracker devices", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual int32 GetTrackerDeviceAmount() const override
	{
		return 0;
	}

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN axis devices", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void GetAxisDeviceIds(TArray<FString>& DeviceIDs) const override
	{ }

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN button devices", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void GetButtonDeviceIds(TArray<FString>& DeviceIDs) const override
	{ }

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of keyboard devices", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void GetKeyboardDeviceIds(TArray<FString>& DeviceIDs) const override
	{ }

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get IDs of VRPN tracker devices", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void GetTrackerDeviceIds(TArray<FString>& DeviceIDs) const override
	{ }

	// Buttons
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN button state", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void GetButtonState(const FString& DeviceID, int32 DeviceChannel, bool& CurrentState, bool& IsChannelAvailable) const override
	{
		IsChannelAvailable = false;
	}

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is VRPN button pressed", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void IsButtonPressed(const FString& DeviceID, int32 DeviceChannel, bool& IsPressedCurrently, bool& IsChannelAvailable) const override
	{
		IsChannelAvailable = false;
	}

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Is VRPN button released", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void IsButtonReleased(const FString& DeviceID, int32 DeviceChannel, bool& IsReleasedCurrently, bool& IsChannelAvailable) const override
	{
		IsChannelAvailable = false;
	}

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Was VRPN button pressed", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void WasButtonPressed(const FString& DeviceID, int32 DeviceChannel, bool& WasPressed, bool& IsChannelAvailable) const override
	{
		IsChannelAvailable = false;
	}

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Was VRPN button released", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void WasButtonReleased(const FString& DeviceID, int32 DeviceChannel, bool& WasReleased, bool& IsChannelAvailable) const override
	{
		IsChannelAvailable = false;
	}

	// Axes
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN axis value", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void GetAxis(const FString& DeviceID, int32 DeviceChannel, float& Value, bool& IsChannelAvailable) const override
	{
		IsChannelAvailable = false;
	}

	// Trackers
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN tracker location", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void GetTrackerLocation(const FString& DeviceID, int32 DeviceChannel, FVector& Location, bool& IsChannelAvailable) const override
	{
		IsChannelAvailable = false;
	}

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get VRPN tracker rotation (as quaternion)", DeprecatedFunction, DeprecationMessage = "VRPN functionality has been moved to LiveLinkVRPN"), Category = "NDisplay|Input")
	virtual void GetTrackerQuat(const FString& DeviceID, int32 DeviceChannel, FQuat& Rotation, bool& IsChannelAvailable) const override
	{
		IsChannelAvailable = false;
	}

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Render API
	//////////////////////////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set viewport camera", DeprecatedFunction, DeprecationMessage = "Use Configuration structures"), Category = "NDisplay|Render")
	virtual void SetViewportCamera(const FString& CameraId, const FString& ViewportId) override
	{ }

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get viewport's buffer ratio", DeprecatedFunction, DeprecationMessage = "Use Configuration structures"), Category = "NDisplay|Render")
	virtual bool GetBufferRatio(const FString& ViewportId, float& BufferRatio) const override
	{
		return false;
	}

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set viewport's buffer ratio", DeprecatedFunction, DeprecationMessage = "Use Configuration structures"), Category = "NDisplay|Render")
	virtual bool SetBufferRatio(const FString& ViewportId, float BufferRatio) override
	{
		return false;
	}

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set start post processing settings for viewport", DeprecatedFunction, DeprecationMessage = "Use Configuration structures"), Category = "NDisplay|Render")
	virtual void SetStartPostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& StartPostProcessingSettings) override
	{ }

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set override post processing settings for viewport", DeprecatedFunction, DeprecationMessage = "Use Configuration structures"), Category = "NDisplay|Render")
	virtual void SetOverridePostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight = 1.0f) override
	{ }

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set final post processing settings for viewport", DeprecatedFunction, DeprecationMessage = "Use Configuration structures"), Category = "NDisplay|Render")
	virtual void SetFinalPostProcessingSettings(const FString& ViewportId, const FPostProcessSettings& FinalPostProcessingSettings) override
	{ }

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get Updated Post Processing", DeprecatedFunction, DeprecationMessage = "Use new api"), Category = "NDisplay|Render")
		virtual bool GetViewportRect(const FString& ViewportId, FIntPoint& ViewportLoc, FIntPoint& ViewportSize) const override
	{
		return false;
	}

	/** Returns list of local viewports. */
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get local viewports", DeprecatedFunction, DeprecationMessage = "Use new api"), Category = "NDisplay|Config")
	virtual void GetLocalViewports(TArray<FString>& ViewportIDs, TArray<FString>& ProjectionTypes, TArray<FIntPoint>& ViewportLocations, TArray<FIntPoint>& ViewportSizes) const override
	{ }

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Scene View Extension Is Active In Context Function", DeprecatedFunction, DeprecationMessage = "Use Configuration structures"), Category = "NDisplay|Render")
	virtual void SceneViewExtensionIsActiveInContextFunction(const TArray<FString>& ViewportIDs, FSceneViewExtensionIsActiveFunctor& OutIsActiveFunction) const override
	{ }
};
