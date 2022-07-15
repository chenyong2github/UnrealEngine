// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Render/Viewport/IDisplayClusterViewportLightCardManager.h"

#include "RenderResource.h"
#include "UnrealClient.h"

class ADisplayClusterLightCardActor;
class FDisplayClusterViewportManager;
class FPreviewScene;

/** A render targetable texture resource used to render the UV light cards to */
class FDisplayClusterLightCardMap : public FTexture, public FRenderTarget
{
public:
	FDisplayClusterLightCardMap(uint32 InSize)
		: Size(InSize)
	{ }

	virtual uint32 GetSizeX() const override { return Size; }
	virtual uint32 GetSizeY() const override { return Size; }
	virtual FIntPoint GetSizeXY() const override { return FIntPoint(Size, Size); }

	virtual void InitDynamicRHI() override;

	virtual FString GetFriendlyName() const override { return TEXT("DisplayClusterLightCardMap"); }

private:
	uint32 Size;
};

/** Manages the rendering of UV light cards for the viewport manager */
class FDisplayClusterViewportLightCardManager : public IDisplayClusterViewportLightCardManager
{
public:
	FDisplayClusterViewportLightCardManager(FDisplayClusterViewportManager& InViewportManager);
	virtual ~FDisplayClusterViewportLightCardManager();

	//~ Begin IDisplayClusterViewportLightCardManager interface
	virtual void UpdateConfiguration() override;
	virtual void ResetScene() override;
	virtual void HandleStartScene() override;
	virtual void HandleBeginNewFrame() override;
	virtual void PreRenderFrame() override;
	virtual void PostRenderFrame() override;
	virtual FRHITexture* GetUVLightCardMap() const override { return UVLightCardMap->GetTextureRHI(); }
	virtual FRHITexture* GetUVLightCardMap_RenderThread() const override { return UVLightCardMapProxy->GetTextureRHI(); }
	virtual bool HasUVLightCards_RenderThread() const override { return bHasUVLightCards; }
	virtual void RenderLightCardMap_RenderThread(FRHICommandListImmediate& RHICmdList) override;
	//~ End IDisplayClusterViewportLightCardManager interface

private:
	/** Initializes the UV light card map texture */
	void InitializeUVLightCardMap();

	/** Releases the UV light card map texture */
	void ReleaseUVLightCardMap();

private:
	/** A reference to the owning viewport manager */
	FDisplayClusterViewportManager& ViewportManager;

	/** The preview scene the UV light card proxies live in for rendering */
	TSharedPtr<FPreviewScene> PreviewScene = nullptr;

	/** The list of UV light card actors that are referenced by the root actor */
	TArray<ADisplayClusterLightCardActor*> UVLightCards;

	/** A list of primitive components that have been added to the preview scene for rendering in the current frame */
	TArray<UPrimitiveComponent*> LoadedPrimitiveComponents;

	/** The render target to which the UV light card map is rendered */
	FDisplayClusterLightCardMap* UVLightCardMap = nullptr;

	/** The render thread copy of the pointer to the UV ligth card map */
	FDisplayClusterLightCardMap* UVLightCardMapProxy = nullptr;

	/** A render thread flag that indicates the light card manager has UV light cards to render */
	bool bHasUVLightCards = false;
};