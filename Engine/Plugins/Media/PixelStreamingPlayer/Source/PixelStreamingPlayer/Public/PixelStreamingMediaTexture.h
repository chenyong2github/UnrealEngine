// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Texture.h"
#include "RenderTargetPool.h"
#include "api/media_stream_interface.h"
#include "PixelStreamingMediaTexture.generated.h"

class FPixelStreamingMediaTextureResource;

/**
 * A Texture Object that can be used in materials etc. that takes updates from webrtc frames.
 */
UCLASS(NotBlueprintType, NotBlueprintable, HideDropdown, HideCategories = (ImportSettings, Compression, Texture, Adjustments, Compositing, LevelOfDetail, Object), META = (DisplayName = "PixelStreaming Media Texture"))
class PIXELSTREAMINGPLAYER_API UPixelStreamingMediaTexture : public UTexture, public rtc::VideoSinkInterface<webrtc::VideoFrame>
{
	GENERATED_UCLASS_BODY()

protected:
	// UTexture implementation

	virtual FTextureResource* CreateResource() override;
	virtual EMaterialValueType GetMaterialType() const override;
	virtual float GetSurfaceWidth() const override;
	virtual float GetSurfaceHeight() const override;
	virtual float GetSurfaceDepth() const override { return 0; }
	virtual uint32 GetSurfaceArraySize() const override { return 0; }
	virtual ETextureClass GetTextureClass() const { return ETextureClass::Other2DNoSource; }

	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;

	// UObject overrides.
	virtual void BeginDestroy() override;

	// from rtc::VideoSinkInterface<webrtc::VideoFrame>
	virtual void OnFrame(const webrtc::VideoFrame& frame) override;

	// updates the internal texture resource after each frame.
	void UpdateTextureReference(FRHICommandList& RHICmdList, FTexture2DRHIRef Reference);

private:
	void InitializeResources();

	FPixelStreamingMediaTextureResource* CurrentResource;

	uint8_t* Buffer = nullptr;
	size_t BufferSize = 0;

	FCriticalSection RenderSyncContext;
	FTextureRHIRef SourceTexture;
	FPooledRenderTargetDesc RenderTargetDescriptor;
	TRefCountPtr<IPooledRenderTarget> RenderTarget;
};
