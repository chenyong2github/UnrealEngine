// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RHI.h"
#include "RHIResources.h"
#include "Engine/Scene.h"
#include "StereoRenderTargetManager.h"

#include "Render/Device/DisplayClusterDeviceBase_PostProcess.h"
#include "Render/Device/DisplayClusterRenderViewport.h"
#include "Render/Device/IDisplayClusterRenderDevice.h"

#include "Containers/Queue.h"

class IDisplayClusterPostProcess;


/**
 * Abstract render device
 */
class FDisplayClusterDeviceBase
	: public IStereoRenderTargetManager
	, public IDisplayClusterRenderDevice
	, public FRHICustomPresent
	, protected FDisplayClusterDeviceBase_PostProcess
{
public:
	FDisplayClusterDeviceBase() = delete;
	FDisplayClusterDeviceBase(uint32 ViewsPerViewport);
	virtual ~FDisplayClusterDeviceBase();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterStereoDevice
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Initialize() override;
	virtual void InitializeWorldContent(UWorld* InWorld) override;
	virtual void SetViewportCamera(const FString& InCameraId = FString(), const FString& InViewportId = FString()) override;
	virtual void SetStartPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& StartPostProcessingSettings) override;
	virtual void SetOverridePostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight = 1.0f) override;
	virtual void SetFinalPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& FinalPostProcessingSettings) override;
	virtual bool GetViewportRect(const FString& InViewportID, FIntRect& Rect) override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IStereoRendering
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool IsStereoEnabled() const override;
	virtual bool IsStereoEnabledOnNextFrame() const override;
	virtual bool EnableStereo(bool stereo = true) override;
	virtual void InitCanvasFromView(class FSceneView* InView, class UCanvas* Canvas) override;
	virtual void AdjustViewRect(enum EStereoscopicPass StereoPassType, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const;
	virtual void CalculateStereoViewOffset(const enum EStereoscopicPass StereoPassType, FRotator& ViewRotation, const float WorldToMeters, FVector& ViewLocation) override;
	virtual FMatrix GetStereoProjectionMatrix(const enum EStereoscopicPass StereoPassType) const override;
	virtual void RenderTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* BackBuffer, FRHITexture2D* SrcTexture, FVector2D WindowSize) const override final;
	virtual int32 GetDesiredNumberOfViews(bool bStereoRequested) const override;
	virtual EStereoscopicPass GetViewPassForIndex(bool bStereoRequested, uint32 ViewIndex) const override;
	virtual uint32 GetViewIndexForPass(EStereoscopicPass StereoPassType) const override;

	virtual IStereoRenderTargetManager* GetRenderTargetManager() override
	{ return this; }

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IStereoRenderTargetManager
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool ShouldUseSeparateRenderTarget() const override
	{ return true; }

	virtual void UpdateViewport(bool bUseSeparateRenderTarget, const class FViewport& Viewport, class SViewport* ViewportWidget = nullptr) override;
	virtual void CalculateRenderTargetSize(const class FViewport& Viewport, uint32& InOutSizeX, uint32& InOutSizeY) override;
	virtual bool NeedReAllocateViewportRenderTarget(const class FViewport& Viewport) override;

	virtual bool NeedReAllocateDepthTexture(const TRefCountPtr<struct IPooledRenderTarget>& DepthTarget) override
	{ return false; }

	virtual uint32 GetNumberOfBufferedFrames() const override
	{ return 1; }

	virtual bool AllocateRenderTargetTexture(uint32 Index, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 Flags, uint32 TargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture, FTexture2DRHIRef& OutShaderResourceTexture, uint32 NumSamples = 1) override
	{ return false; }

	virtual bool AllocateDepthTexture(uint32 Index, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 Flags, uint32 TargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture, FTexture2DRHIRef& OutShaderResourceTexture, uint32 NumSamples = 1)
	{ return false; }

	virtual bool DeviceIsAPrimaryView(EStereoscopicPass Pass) override
	{ return true; }
	
	virtual bool DeviceIsASecondaryView(EStereoscopicPass Pass) override
	{ return false; }

	virtual bool DeviceIsAnAdditionalView(EStereoscopicPass Pass) override
	{ return false; }

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FRHICustomPresent
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void OnBackBufferResize() override;

	virtual bool NeedsNativePresent() override
	{ return true; }

	virtual bool Present(int32& InOutSyncInterval) override;

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// FDisplayClusterDeviceBase
	//////////////////////////////////////////////////////////////////////////////////////////////
	enum EDisplayClusterEyeType
	{
		StereoLeft  = 0,
		Mono        = 1,
		StereoRight = 2,
		COUNT
	};

protected:
	// Encodes view index to EStereoscopicPass (the value may be out of EStereoscopicPass bounds)
	EStereoscopicPass EncodeStereoscopicPass(int ViewIndex) const;
	// Decodes normal EStereoscopicPass from encoded EStereoscopicPass
	EStereoscopicPass DecodeStereoscopicPass(const enum EStereoscopicPass StereoPassType) const;
	// Decodes viewport index from encoded EStereoscopicPass
	int DecodeViewportIndex(const enum EStereoscopicPass StereoPassType) const;
	// Decodes eye type from encoded EStereoscopicPass
	EDisplayClusterEyeType DecodeEyeType(const enum EStereoscopicPass StereoPassType) const;
	// Decodes view index for a viewport (i.e. left=0, right=1)
	uint32 DecodeViewIndex(const enum EStereoscopicPass StereoPassType) const;

	// Returns swap interval
	uint32 GetSwapInt() const;

	// Adds a new viewport with specified parameters and projection policy object
	void AddViewport(const FString& InViewportId, const FIntPoint& InViewportLocation, const FIntPoint& InViewportSize, TSharedPtr<IDisplayClusterProjectionPolicy> InProjPolicy, const FString& InCameraId, bool IsRTT = false);
	// Performs copying of render target data to the back buffer
	virtual void CopyTextureToBackBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* BackBuffer, FRHITexture2D* SrcTexture, FVector2D WindowSize) const;
	
	// Checks if custom post processing settings is assigned for specific viewport and assign them to be used
	virtual void StartFinalPostprocessSettings(struct FPostProcessSettings* StartPostProcessingSettings, const enum EStereoscopicPass StereoPassType) override;
	virtual bool OverrideFinalPostprocessSettings(struct FPostProcessSettings* OverridePostProcessingSettings, const enum EStereoscopicPass StereoPassType, float& BlendWeight) override;
	virtual void EndFinalPostprocessSettings(struct FPostProcessSettings* FinalPostProcessingSettings, const enum EStereoscopicPass StereoPassType) override;

protected:
	struct FOverridePostProcessingSettings
	{
		float BlendWeight = 1.0f;
		FPostProcessSettings PostProcessingSettings;
	};

	// Viewports
	mutable TArray<FDisplayClusterRenderViewport> RenderViewports;
	// Views per viewport (render passes)
	uint32 ViewsAmountPerViewport = 0;
	// UE4 main viewport
	FViewport* MainViewport = nullptr;
	// custom post processing settings
	TMap<int, FPostProcessSettings> ViewportStartPostProcessingSettings;	 
	TMap<int, FOverridePostProcessingSettings> ViewportOverridePostProcessingSettings;
	TMap<int, FPostProcessSettings> ViewportFinalPostProcessingSettings;

	// Data access synchronization
	mutable FCriticalSection InternalsSyncScope;

	// Temporary: don't allow to add more than 1 RTT viewport
	bool bViewportRttAdded = false;
};
