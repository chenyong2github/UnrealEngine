// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Render/Viewport/IDisplayClusterViewport.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrame.h"

class UWorld;
class FViewport;
class FSceneViewFamilyContext;
class ADisplayClusterRootActor;
class UDisplayClusterConfigurationViewport;
class IDisplayClusterViewportManagerProxy;

struct FDisplayClusterConfigurationViewportPreview;

class DISPLAYCLUSTER_API IDisplayClusterViewportManager
{
public:
	virtual ~IDisplayClusterViewportManager() = default;

public:
	virtual const IDisplayClusterViewportManagerProxy* GetProxy() const = 0;
	virtual       IDisplayClusterViewportManagerProxy* GetProxy() = 0;

	virtual UWorld* GetCurrentWorld() const = 0;

	virtual ADisplayClusterRootActor* GetRootActor() const = 0;

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
	virtual bool BeginNewFrame(FViewport* InViewport, UWorld* InWorld, FDisplayClusterRenderFrame& OutRenderFrame) = 0;

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

	// Send to render thread
	virtual void RenderFrame(FViewport* InViewport) = 0;

#if WITH_EDITOR
	virtual bool UpdatePreviewConfiguration(const FDisplayClusterConfigurationViewportPreview& PreviewConfiguration, ADisplayClusterRootActor* InRootActorPtr) = 0;
	virtual bool RenderInEditor(class FDisplayClusterRenderFrame& InRenderFrame, FViewport* InViewport) = 0;
#endif

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
};

