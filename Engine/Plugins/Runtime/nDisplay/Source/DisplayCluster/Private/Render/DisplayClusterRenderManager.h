// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Render/IPDisplayClusterRenderManager.h"

class FDisplayClusterDeviceNativePresentHandler;
class IDisplayClusterPostProcess;
class IDisplayClusterProjectionPolicy;
class IDisplayClusterProjectionPolicyFactory;
class IDisplayClusterRenderDeviceFactory;
class IDisplayClusterRenderSyncPolicy;
class IDisplayClusterRenderSyncPolicyFactory;
class UDisplayClusterCameraComponent;


/**
 * Render manager. Responsible for everything related to the visuals.
 */
class FDisplayClusterRenderManager :
	public IPDisplayClusterRenderManager
{
public:
	FDisplayClusterRenderManager();
	virtual ~FDisplayClusterRenderManager();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Init(EDisplayClusterOperationMode OperationMode) override;
	virtual void Release() override;
	virtual bool StartSession(const FString& configPath, const FString& nodeId) override;
	virtual void EndSession() override;
	virtual bool StartScene(UWorld* InWorld) override;
	virtual void EndScene() override;
	virtual void PreTick(float DeltaSeconds) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterRenderManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	// Device
	virtual bool RegisterRenderDeviceFactory(const FString& InDeviceType, TSharedPtr<IDisplayClusterRenderDeviceFactory>& InFactory) override;
	virtual bool UnregisterRenderDeviceFactory(const FString& InDeviceType) override;
	// Synchronization
	virtual bool RegisterSynchronizationPolicyFactory(const FString& InSyncPolicyType, TSharedPtr<IDisplayClusterRenderSyncPolicyFactory>& InFactory) override;
	virtual bool UnregisterSynchronizationPolicyFactory(const FString& InSyncPolicyType) override;
	virtual TSharedPtr<IDisplayClusterRenderSyncPolicy> GetCurrentSynchronizationPolicy() override;
	// Projection
	virtual bool RegisterProjectionPolicyFactory(const FString& InProjectionType, TSharedPtr<IDisplayClusterProjectionPolicyFactory>& InFactory) override;
	virtual bool UnregisterProjectionPolicyFactory(const FString& InProjectionType) override;
	virtual TSharedPtr<IDisplayClusterProjectionPolicyFactory> GetProjectionPolicyFactory(const FString& InProjectionType) override;
	// Post-process
	virtual bool RegisterPostprocessOperation(const FString& InName, TSharedPtr<IDisplayClusterPostProcess>& InOperation, int InPriority = 0) override;
	virtual bool RegisterPostprocessOperation(const FString& InName, IPDisplayClusterRenderManager::FDisplayClusterPPInfo& InPPInfo) override;
	virtual bool UnregisterPostprocessOperation(const FString& InName) override;
	virtual TMap<FString, IPDisplayClusterRenderManager::FDisplayClusterPPInfo> GetRegisteredPostprocessOperations() const override;
	// Custom Rendering Post-process
	virtual void SetStartPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& StartPostProcessingSettings) override;
	virtual void SetOverridePostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight = 1.0f) override;
	virtual void SetFinalPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& FinalPostProcessingSettings) override;

	// Camera
	virtual void SetViewportCamera(const FString& InCameraId = FString(), const FString& InViewportId = FString()) override;

	// Viewports
	virtual bool GetViewportRect(const FString& InViewportID, FIntRect& Rect) override;

	// Camera API
	virtual float GetInterpupillaryDistance(const FString& CameraId) const override;
	virtual void  SetInterpupillaryDistance(const FString& CameraId, float EyeDistance) override;
	virtual bool  GetEyesSwap(const FString& CameraId) const override;
	virtual void  SetEyesSwap(const FString& CameraId, bool EyeSwapped) override;
	virtual bool  ToggleEyesSwap(const FString& CameraId) override;
	virtual float GetNearCullingDistance(const FString& CameraId) const override;
	virtual void  SetNearCullingDistance(const FString& CameraId, float NearDistance) override;
	virtual float GetFarCullingDistance(const FString& CameraId) const override;
	virtual void  SetFarCullingDistance(const FString& CameraId, float FarDistance) override;
	virtual void  GetCullingDistance(const FString& CameraId, float& NearDistance, float& FarDistance) const override;
	virtual void  SetCullingDistance(const FString& CameraId, float NearDistance, float FarDistance) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IPDisplayClusterRenderManager
	//////////////////////////////////////////////////////////////////////////////////////////////

private:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FDisplayClusterRenderManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	void ResizeWindow(int32 WinX, int32 WinY, int32 ResX, int32 ResY);
	void OnViewportCreatedHandler();
	void OnBeginDrawHandler();

private:
	EDisplayClusterOperationMode CurrentOperationMode;
	FString ConfigPath;
	FString ClusterNodeId;

	// Interface pointer to avoid type casting
	TSharedPtr<IDisplayClusterRenderDevice, ESPMode::ThreadSafe> RenderDevice;
	FDisplayClusterDeviceNativePresentHandler* NativePresentHandler;
	bool bWindowAdjusted = false;

private:
	// Rendering device factories
	TMap<FString, TSharedPtr<IDisplayClusterRenderDeviceFactory>> RenderDeviceFactories;
	// Helper function to instantiate a rendering device
	TSharedPtr<IDisplayClusterRenderDevice, ESPMode::ThreadSafe> CreateRenderDevice() const;

private:
	// Synchronization internals
	TMap<FString, TSharedPtr<IDisplayClusterRenderSyncPolicyFactory>> SyncPolicyFactories;
	TSharedPtr<IDisplayClusterRenderSyncPolicy> CreateRenderSyncPolicy() const;
	TSharedPtr<IDisplayClusterRenderSyncPolicy> SyncPolicy;

private:
	// Projection internals
	TMap<FString, TSharedPtr<IDisplayClusterProjectionPolicyFactory>> ProjectionPolicyFactories;

private:
	// Post-process internals
	TMap<FString, FDisplayClusterPPInfo> PostProcessOperations;

private:
	// Internal data access synchronization
	mutable FCriticalSection CritSecInternals;
};
