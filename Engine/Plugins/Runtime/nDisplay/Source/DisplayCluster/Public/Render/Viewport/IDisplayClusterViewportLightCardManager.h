// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class FRHITexture;
class FRHICommandListImmediate;

/** Interface for a manager which manages the rendering of UV light cards for the viewport manager */
class DISPLAYCLUSTER_API IDisplayClusterViewportLightCardManager
{
public:
	/** Updates the UV light card scene based on the current root actor configuration */
	virtual void UpdateConfiguration() = 0;

	/** Resets the UV light card scene */
	virtual void ResetScene() = 0;

	/** Raised when a new scene is being started, initializes the UV light card scene */
	virtual void HandleStartScene() = 0;

	/** Raised when a new frame begins, initializes any frame resources needed */
	virtual void HandleBeginNewFrame() = 0;

	/** Raised just before a render frame pass is performed */
	virtual void PreRenderFrame() = 0;

	/** Raised just after a render frame pass is performed */
	virtual void PostRenderFrame() = 0;

	/** Gets the UV light card map texture */
	virtual FRHITexture* GetUVLightCardMap() const = 0;

	/** Gets the render thread copy of the UV light card map texture */
	virtual FRHITexture* GetUVLightCardMap_RenderThread() const = 0;

	/** Gets whether there are any UV light cards to render to the viewports */
	virtual bool HasUVLightCards_RenderThread() const = 0;

	/** Renders the UV light card scene to the UV light card map texture */
	virtual void RenderLightCardMap_RenderThread(FRHICommandListImmediate& RHICmdList) = 0;
};