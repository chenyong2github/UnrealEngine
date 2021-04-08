// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Render/Viewport/IDisplayClusterViewport.h"
#include "Render/Viewport/IDisplayClusterViewportProxy.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrame.h"

class UWorld;
class FViewport;
class FSceneViewFamilyContext;
class ADisplayClusterRootActor;
class UDisplayClusterConfigurationViewport;

class DISPLAYCLUSTER_API IDisplayClusterViewportManager
{
public:
	virtual ~IDisplayClusterViewportManager() = 0
	{ }

public:
	virtual UWorld* GetWorld() const = 0;
	virtual ADisplayClusterRootActor* GetRootActor() const = 0;

	/**
	* Handle start scene event
	* [Game thread func]
	*/
	virtual void StartScene(UWorld* World) = 0;

	/**
	* Handle end scene event
	* [Game thread func]
	*/
	virtual void EndScene() = 0;

	/**
	* Return current scene status
	* [Game thread func]
	*/
	virtual bool IsSceneOpened() const = 0;

	/**
	* Update\Create\Delete local node viewports
	* Update ICVFX configuration from root actor components
	* [Game thread func]
	*
	* @param InRenderMode     - Render mode
	* @param InClusterNodeId  - cluster node for rendering
	* @param InRootActorPtr   - reference to RootActor with actual configuration inside
	*
	* @return - true, if success
	*/
	virtual bool UpdateConfiguration(EDisplayClusterRenderFrameMode InRenderMode, const FString& InClusterNodeId, class ADisplayClusterRootActor* InRootActorPtr) = 0;

	/**
	* Initialize new frame for all viewports on game thread, and update context, render resources with viewport new settings
	* And finally build render frame structure and send to render thread proxy viewport objects
	* [Game thread func]
	*
	* @param InViewport          - target viewport
	* @param OutRenderFrame      - output render frame container
	*
	* @return - true, if success
	*/
	virtual bool BeginNewFrame(FViewport* InViewport, FDisplayClusterRenderFrame& OutRenderFrame) = 0;

	/**
	* Finalize frame logic for viewports on game thread
	* [Game thread func]
	*
	*/
	virtual void FinalizeNewFrame() = 0;

	/**
	* Initialize view family, using rules
	* [Game thread func]
	*
	* @param InFrameTarget          - frame target
	* @param OutRenderOutViewFamily - output family
	*
	*/
	virtual void ConfigureViewFamily(const FDisplayClusterRenderFrame::FFrameRenderTarget& InFrameTarget, const FDisplayClusterRenderFrame::FFrameViewFamily& InFrameViewFamily, FSceneViewFamilyContext& InOutViewFamily) = 0;


#if WITH_EDITOR
	virtual bool UpdatePreviewConfiguration(class UDisplayClusterConfigurationViewportPreview* PreviewConfiguration, UWorld* PreviewWorld, ADisplayClusterRootActor* InRootActorPtr) = 0;
	virtual bool RenderPreview(FDisplayClusterRenderFrame& InPreviewRenderFrame) = 0;
#endif

	/** Transfer view results accross GPUs. This is done only once all views have been rendered for RenderFrame
	* [Rendering thread func]
	*/
	virtual void DoCrossGPUTransfers_RenderThread(FViewport* InViewport, FRHICommandListImmediate& RHICmdList) const = 0;

	/** Resolve render targets to unique viewport inputshader contexts resources
	* Apply postprocess, generate mips, etc from settings in FDisplayClusterViewporDeferredUpdateSettings
	* [Rendering thread func]
	*/
	virtual void UpdateDeferredResources_RenderThread(FRHICommandListImmediate& RHICmdList) const = 0;

	/** Apply Warp&Blend
	* [Rendering thread func]
	*/
	virtual void UpdateFrameResources_RenderThread(FRHICommandListImmediate& RHICmdList, bool bWarpBlendEnabled) const = 0;

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	/**
	* Find viewport object by name
	* [Game thread func]
	*
	* @param ViewportId - Viewport name
	*
	* @return - viewport object ref
	*/
	virtual IDisplayClusterViewport* FindViewport(const FString& InViewportId) const = 0;

	/**
	* Find viewport object and context number by stereoscopic pass index
	* [Game thread func]
	*
	* @param StereoPassType - stereoscopic pass index
	* @param OutContextNum - context number
	*
	* @return - viewport object ref
	*/
	virtual IDisplayClusterViewport* FindViewport(const enum EStereoscopicPass StereoPassType, uint32* OutContextNum = nullptr) const = 0;
	
	/**
	* Return all exist viewports objects
	* [Game thread func]
	*
	* @return - arrays with viewport objects refs
	*/
	virtual const TArrayView<IDisplayClusterViewport*> GetViewports() const = 0;

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	/**
	* Find viewport render thread proxy object by name
	* [Rendering thread func]
	*
	* @param ViewportId - Viewport name
	*
	* @return - viewport proxy object ref
	*/
	virtual IDisplayClusterViewportProxy* FindViewport_RenderThread(const FString& InViewportId) const = 0;

	/**
	* Find viewport render thread proxy object and context number by stereoscopic pass index
	* [Rendering thread func]
	*
	* @param StereoPassType - stereoscopic pass index
	* @param OutContextNum - context number
	*
	* @return - viewport render thread proxy object ref
	*/
	virtual IDisplayClusterViewportProxy* FindViewport_RenderThread(const enum EStereoscopicPass StereoPassType, uint32* OutContextNum = nullptr) const = 0;

	/**
	* Return all exist viewports render thread proxy objects
	* [Rendering thread func]
	*
	* @return - arrays with viewport render thread proxy objects refs
	*/
	virtual const TArrayView<IDisplayClusterViewportProxy*> GetViewports_RenderThread() const = 0;

	/**
	* Return render frame targets for current frame
	* [Rendering thread func]
	*
	* @param OutRenderFrameTargets - frame RTTs (left, right)
	* @param OutTargetOffsets - frames offset on backbuffer
	* @param OutAdditionalFrameResources - (optional) array with additional render targetable resources (requested externally FDisplayClusterRenderFrameSettings::bShouldUseAdditionalTargetableFrameResource)
	*
	* @return - true if success
	*/
	virtual bool GetFrameTargets_RenderThread(TArray<FRHITexture2D*>& OutFrameResources, TArray<FIntPoint>& OutTargetOffsets, TArray<FRHITexture2D*>* OutAdditionalFrameResources=nullptr) const = 0;

	/**
	* Resolve to backbuffer
	* [Rendering thread func]
	*
	* @param InContextNum - renderframe source context num
	* @param DestArrayIndex - dest array index on backbuffer
	* @param WindowSize - dest backbuffer window size
	*
	* @return - true if success
	*/
	virtual bool ResolveFrameTargetToBackBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, const uint32 InContextNum, const int DestArrayIndex, FRHITexture2D* DstBackBuffer, FVector2D WindowSize) const = 0;
};

