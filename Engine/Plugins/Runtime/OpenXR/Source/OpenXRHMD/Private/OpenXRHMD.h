// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OpenXRHMD_Layer.h"
#include "OpenXRAssetManager.h"
#include "CoreMinimal.h"
#include "HeadMountedDisplayBase.h"
#include "XRTrackingSystemBase.h"
#include "XRRenderTargetManager.h"
#include "XRRenderBridge.h"
#include "XRSwapChain.h"
#include "SceneViewExtension.h"
#include "StereoLayerManager.h"
#include "DefaultSpectatorScreenController.h"
#include "IHeadMountedDisplayVulkanExtensions.h"

#include <openxr/openxr.h>

class APlayerController;
class FSceneView;
class FSceneViewFamily;
class UCanvas;
class FOpenXRRenderBridge;

/**
 * Simple Head Mounted Display
 */
class FOpenXRHMD
	: public FHeadMountedDisplayBase
	, public FXRRenderTargetManager
	, public FSceneViewExtensionBase
	, public FOpenXRAssetManager
	, public TStereoLayerManager<FOpenXRLayer>
{
public:
	class FDeviceSpace
	{
	public:
		FDeviceSpace(XrAction InAction, XrPath InPath);
		~FDeviceSpace();

		bool CreateSpace(XrSession InSession);
		void DestroySpace();

		XrAction Action;
		XrSpace Space;
		XrPath Path;
	};

	// The game and render threads each have a separate copy of these structures so that they don't stomp on each other or cause tearing
	// when the game thread progresses to the next frame while the render thread is still working on the previous frame.
	struct FPipelinedFrameState
	{
		XrFrameState FrameState{XR_TYPE_FRAME_STATE};
		XrViewState ViewState{XR_TYPE_VIEW_STATE};
		TArray<XrView> Views;
		TArray<XrSpaceLocation> DeviceLocations;
		XrSpace TrackingSpace;
		float WorldToMetersScale = 100.0f;

		TArray<XrViewConfigurationView> ViewConfigs;
		TArray<class IOpenXRExtensionPlugin*> PluginViews;
	};

	struct FPipelinedLayerState
	{
		TArray<XrCompositionLayerQuad> QuadLayers;
		TArray<XrCompositionLayerProjectionView> ProjectionLayers;
		TArray<XrCompositionLayerDepthInfoKHR> DepthLayers;

		TArray<XrSwapchainSubImage> ColorImages;
		TArray<XrSwapchainSubImage> DepthImages;

		FXRSwapChainPtr ColorSwapchain;
		FXRSwapChainPtr DepthSwapchain;
		TArray<FXRSwapChainPtr> QuadSwapchains;
	};

	class FVulkanExtensions : public IHeadMountedDisplayVulkanExtensions
	{
	public:
		FVulkanExtensions(XrInstance InInstance, XrSystemId InSystem) : Instance(InInstance), System(InSystem) {}
		virtual ~FVulkanExtensions() {}

		/** IHeadMountedDisplayVulkanExtensions */
		virtual bool GetVulkanInstanceExtensionsRequired(TArray<const ANSICHAR*>& Out) override;
		virtual bool GetVulkanDeviceExtensionsRequired(VkPhysicalDevice_T *pPhysicalDevice, TArray<const ANSICHAR*>& Out) override;

	private:
		XrInstance Instance;
		XrSystemId System;

		TArray<char> Extensions;
		TArray<char> DeviceExtensions;
	};

	/** IXRTrackingSystem interface */
	virtual FName GetSystemName() const override
	{
		// This identifier is relied upon for plugin identification,
		// see GetHMDName() to query the true XR system name.
		static FName DefaultName(TEXT("OpenXR"));
		return DefaultName;
	}

	int32 GetXRSystemFlags() const override
	{
		int32 flags = EXRSystemFlags::IsHeadMounted;

		if (SelectedEnvironmentBlendMode != XR_ENVIRONMENT_BLEND_MODE_OPAQUE)
		{
			flags |= EXRSystemFlags::IsAR;
		}

		if (bSupportsHandTracking)
		{
			flags |= EXRSystemFlags::SupportsHandTracking;
		}

		return flags;
	}

	virtual bool EnumerateTrackedDevices(TArray<int32>& OutDevices, EXRTrackedDeviceType Type = EXRTrackedDeviceType::Any) override;

	virtual void SetInterpupillaryDistance(float NewInterpupillaryDistance) override;
	virtual float GetInterpupillaryDistance() const override;
	virtual bool GetRelativeEyePose(int32 InDeviceId, EStereoscopicPass InEye, FQuat& OutOrientation, FVector& OutPosition) override;

	virtual void ResetOrientationAndPosition(float yaw = 0.f) override;
	virtual void ResetOrientation(float Yaw = 0.f) override;
	virtual void ResetPosition() override;

	virtual bool GetIsTracked(int32 DeviceId);
	virtual bool GetCurrentPose(int32 DeviceId, FQuat& CurrentOrientation, FVector& CurrentPosition) override;
	virtual bool GetPoseForTime(int32 DeviceId, FTimespan Timespan, FQuat& CurrentOrientation, FVector& CurrentPosition, bool& bProvidedLinearVelocity, FVector& LinearVelocity, bool& bProvidedAngularVelocity, FVector& AngularVelocityRadPerSec);
	virtual void SetBaseRotation(const FRotator& BaseRot) override;
	virtual FRotator GetBaseRotation() const override;

	virtual void SetBaseOrientation(const FQuat& BaseOrient) override;
	virtual FQuat GetBaseOrientation() const override;

	virtual void SetTrackingOrigin(EHMDTrackingOrigin::Type NewOrigin) override
	{
		TrackingSpaceType = (NewOrigin == EHMDTrackingOrigin::Eye || StageSpace == XR_NULL_HANDLE) ? XR_REFERENCE_SPACE_TYPE_LOCAL : XR_REFERENCE_SPACE_TYPE_STAGE;
	}

	virtual EHMDTrackingOrigin::Type GetTrackingOrigin() const override
	{
		return (TrackingSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL) ? EHMDTrackingOrigin::Eye : EHMDTrackingOrigin::Stage;
	}

	virtual class IHeadMountedDisplay* GetHMDDevice() override
	{
		return this;
	}
	virtual class TSharedPtr< class IStereoRendering, ESPMode::ThreadSafe > GetStereoRenderingDevice() override
	{
		return SharedThis(this);
	}
#if !PLATFORM_HOLOLENS
	// Native stereo layers severely impact performance on Hololens
	virtual class IStereoLayers* GetStereoLayers() override
	{
		return this;
	}
#endif

	virtual void GetMotionControllerData(UObject* WorldContext, const EControllerHand Hand, FXRMotionControllerData& MotionControllerData) override;

	virtual float GetWorldToMetersScale() const override;

	virtual FVector2D GetPlayAreaBounds(EHMDTrackingOrigin::Type Origin) const override;

protected:

	bool StartSession();
	bool StopSession();
	bool OnStereoStartup();
	bool OnStereoTeardown();
	bool ReadNextEvent(XrEventDataBuffer* buffer);
	void DestroySession();

	void BuildOcclusionMeshes();
	bool BuildOcclusionMesh(XrVisibilityMaskTypeKHR Type, int View, FHMDViewMesh& Mesh);

	const FPipelinedFrameState& GetPipelinedFrameStateForThread() const;
	FPipelinedFrameState& GetPipelinedFrameStateForThread();

	void UpdateDeviceLocations(bool bUpdateOpenXRExtensionPlugins);
	void EnumerateViews(FPipelinedFrameState& PipelineState);
	void LocateViews(FPipelinedFrameState& PipelinedState, bool ResizeViewsArray = false);

	void CopyTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, FIntRect SrcRect, FRHITexture2D* DstTexture, FIntRect DstRect, bool bClearBlack, bool bNoAlpha, ERenderTargetActions RTAction) const;
	void CopyTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, FIntRect SrcRect, const FXRSwapChainPtr& DstSwapChain, FIntRect DstRect, bool bClearBlack, bool bNoAlpha) const;

public:
	/** IXRTrackingSystem interface */
	virtual bool DoesSupportLateProjectionUpdate() const override { return true; }
	virtual FString GetVersionString() const override;
	virtual bool HasValidTrackingPosition() override { return IsTracking(HMDDeviceId); }

	/** IHeadMountedDisplay interface */
	virtual bool IsHMDConnected() override { return true; }
	virtual bool DoesSupportPositionalTracking() const override { return true; }
	virtual bool IsHMDEnabled() const override;
	virtual void EnableHMD(bool allow = true) override;
	virtual FName GetHMDName() const override;
	virtual bool GetHMDMonitorInfo(MonitorInfo&) override;
	virtual void GetFieldOfView(float& OutHFOVInDegrees, float& OutVFOVInDegrees) const override;
	virtual bool IsChromaAbCorrectionEnabled() const override;
	virtual FIntPoint GetIdealRenderTargetSize() const override;
	virtual bool GetHMDDistortionEnabled(EShadingPath ShadingPath) const override { return false; }
	virtual FIntRect GetFullFlatEyeRect_RenderThread(FTexture2DRHIRef EyeTexture) const override;
	virtual void CopyTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, FIntRect SrcRect, FRHITexture2D* DstTexture, FIntRect DstRect, bool bClearBlack, bool bNoAlpha) const override;
	virtual bool HasHiddenAreaMesh() const override final;
	virtual bool HasVisibleAreaMesh() const override final;
	virtual void DrawHiddenAreaMesh_RenderThread(class FRHICommandList& RHICmdList, EStereoscopicPass StereoPass) const override final;
	virtual void DrawVisibleAreaMesh_RenderThread(class FRHICommandList& RHICmdList, EStereoscopicPass StereoPass) const override final;
	virtual void OnBeginRendering_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& ViewFamily) override;
	virtual void OnBeginRendering_GameThread() override;
	virtual void OnLateUpdateApplied_RenderThread(FRHICommandListImmediate& RHICmdList, const FTransform& NewRelativeTransform) override;
	virtual bool OnStartGameFrame(FWorldContext& WorldContext) override;
	virtual EHMDWornState::Type GetHMDWornState() override { return bIsReady ? EHMDWornState::Worn : EHMDWornState::NotWorn; }

	/** IStereoRendering interface */
	virtual bool IsStereoEnabled() const override;
	virtual bool EnableStereo(bool stereo = true) override;
	virtual void AdjustViewRect(EStereoscopicPass StereoPass, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const override;
	virtual void SetFinalViewRect(FRHICommandListImmediate& RHICmdList, const enum EStereoscopicPass StereoPass, const FIntRect& FinalViewRect) override;
	virtual int32 GetDesiredNumberOfViews(bool bStereoRequested) const override;
	virtual EStereoscopicPass GetViewPassForIndex(bool bStereoRequested, uint32 ViewIndex) const override;
	virtual uint32 GetViewIndexForPass(EStereoscopicPass StereoPassType) const override;
	virtual uint32 DeviceGetLODViewIndex() const override;
	virtual bool DeviceIsAPrimaryPass(EStereoscopicPass Pass) override;
	
	virtual FMatrix GetStereoProjectionMatrix(const enum EStereoscopicPass StereoPassType) const override;
	virtual void GetEyeRenderParams_RenderThread(const struct FRenderingCompositePassContext& Context, FVector2D& EyeToSrcUVScaleValue, FVector2D& EyeToSrcUVOffsetValue) const override;
	virtual IStereoRenderTargetManager* GetRenderTargetManager() override;
	virtual void RenderTexture_RenderThread(class FRHICommandListImmediate& RHICmdList, class FRHITexture2D* BackBuffer, class FRHITexture2D* SrcTexture, FVector2D WindowSize) const override;

	/** ISceneViewExtension interface */
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override;
	virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override;
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const;

	/** IStereoRenderTargetManager */
	virtual bool ShouldUseSeparateRenderTarget() const override { return IsStereoEnabled(); }
	virtual bool AllocateRenderTargetTexture(uint32 Index, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ETextureCreateFlags TargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture, FTexture2DRHIRef& OutShaderResourceTexture, uint32 NumSamples = 1) override;
	virtual bool NeedReAllocateDepthTexture(const TRefCountPtr<IPooledRenderTarget>& DepthTarget) override final { return bNeedReAllocatedDepth; }
	virtual bool AllocateDepthTexture(uint32 Index, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, ETextureCreateFlags InTexFlags, ETextureCreateFlags TargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture, FTexture2DRHIRef& OutShaderResourceTexture, uint32 NumSamples = 1) override final;

	virtual FXRRenderBridge* GetActiveRenderBridge_GameThread(bool bUseSeparateRenderTarget) override;

	/** IStereoRenderTargetManager */
	virtual void OnBeginPlay(FWorldContext& InWorldContext) override;
	virtual void OnEndPlay(FWorldContext& InWorldContext) override;

	/** IStereoLayers */
	virtual bool ShouldCopyDebugLayersToSpectatorScreen() const override { return true; }

public:
	/** Constructor */
	FOpenXRHMD(const FAutoRegister&, XrInstance InInstance, XrSystemId InSystem, TRefCountPtr<FOpenXRRenderBridge>& InRenderBridge, TArray<const char*> InEnabledExtensions, TArray<class IOpenXRExtensionPlugin*> InExtensionPlugins, IARSystemSupport* ARSystemSupport);

	/** Destructor */
	virtual ~FOpenXRHMD();

	void OnBeginRendering_RHIThread(const FPipelinedFrameState& InFrameState, FXRSwapChainPtr ColorSwapchain, FXRSwapChainPtr DepthSwapchain);
	void OnFinishRendering_RHIThread();

	/** @return	True if the HMD was initialized OK */
	OPENXRHMD_API bool IsInitialized() const;
	OPENXRHMD_API bool IsRunning() const;
	OPENXRHMD_API bool IsFocused() const;

	OPENXRHMD_API int32 AddActionDevice(XrAction Action, XrPath Path);
	OPENXRHMD_API void ResetActionDevices();
	OPENXRHMD_API XrPath GetTrackedDevicePath(const int32 DeviceId);

	OPENXRHMD_API bool IsExtensionEnabled(const FString& Name) const { return EnabledExtensions.Contains(Name); }
	OPENXRHMD_API XrInstance GetInstance() { return Instance; }
	OPENXRHMD_API XrSystemId GetSystem() { return System; }
	OPENXRHMD_API XrSession GetSession() { return Session; }
	OPENXRHMD_API XrSpace GetTrackingSpace()
	{
		return (TrackingSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE) ? StageSpace : LocalSpace;
	}
	OPENXRHMD_API XrTime GetDisplayTime() const;
	OPENXRHMD_API TArray<IOpenXRExtensionPlugin*>& GetExtensionPlugins() { return ExtensionPlugins; }

	OPENXRHMD_API bool IsStandaloneStereoOnlyDevice() const { return bIsStandaloneStereoOnlyDevice; }

private:
	bool					bStereoEnabled;
	TAtomic<bool>			bIsRunning;
	TAtomic<bool>			bIsReady;
	TAtomic<bool>			bIsRendering;
	TAtomic<bool>			bIsSynchronized;
	bool					bDepthExtensionSupported;
	bool					bHiddenAreaMaskSupported;
	bool					bViewConfigurationFovSupported;
	bool					bNeedReAllocatedDepth;
	bool					bNeedReBuildOcclusionMesh;
	bool					bIsMobileMultiViewEnabled;
	bool					bSupportsHandTracking;
	bool					bProjectionLayerAlphaEnabled;
	bool					bIsStandaloneStereoOnlyDevice;
	float					WorldToMetersScale = 100.0f;

	XrSessionState			CurrentSessionState;
	FRWLock					SessionHandleMutex;

	TArray<const char*>		EnabledExtensions;
	TArray<class IOpenXRExtensionPlugin*> ExtensionPlugins;
	XrInstance				Instance;
	XrSystemId				System;
	XrSession				Session;
	XrSpace					LocalSpace;
	XrSpace					StageSpace;
	XrReferenceSpaceType	TrackingSpaceType;
	XrViewConfigurationType SelectedViewConfigurationType;
	XrEnvironmentBlendMode  SelectedEnvironmentBlendMode;
	XrInstanceProperties    InstanceProperties;
	XrSystemProperties      SystemProperties;

	FPipelinedFrameState	PipelinedFrameStateGame;
	FPipelinedFrameState	PipelinedFrameStateRendering;
	FPipelinedFrameState	PipelinedFrameStateRHI;

	FPipelinedLayerState	PipelinedLayerStateRendering;
	FPipelinedLayerState	PipelinedLayerStateRHI;

	FRWLock					DeviceMutex;
	TArray<FDeviceSpace>	DeviceSpaces;

	TRefCountPtr<FOpenXRRenderBridge> RenderBridge;
	IRendererModule*		RendererModule;

	uint8					LastRequestedSwapchainFormat;
	uint8					LastRequestedDepthSwapchainFormat;

	TArray<FHMDViewMesh>	HiddenAreaMeshes;
	TArray<FHMDViewMesh>	VisibleAreaMeshes;
};
