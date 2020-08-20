// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDMXPixelMappingRenderer.h"

class FTextureResource;
class FWidgetRenderer;
class IRendererModule;
class FRHICommandListImmediate;
struct FSlateMaterialBrush;

/**
 * Implementation of Pixel Mapping Renderer
 */
class FDMXPixelMappingRenderer
	: public IDMXPixelMappingRenderer
{
public:

	/**
	 * FRenderContext holds rendering info
	 */
	struct FRenderContext
	{
		const FTextureResource* InputTexture = nullptr;
		const FTextureResource* DstTexture = nullptr;
		const FTextureRenderTargetResource* DstTextureTargetResource = nullptr;

		FIntPoint InputTextureSize;
		FIntPoint OutputTextureSize;
		FVector4 PixelFactor;
		FIntVector4 InvertPixel;
		float X = 0.f; float Y = 0.f;
		float SizeX = 0.f; float SizeY = 0.f;
		float U = 0.f; float V = 0.f;
		float SizeU = 0.f; float SizeV = 0.f;
		FIntPoint TargetSize; FIntPoint TextureSize;
	};

public:
	/** Default constructor */
	FDMXPixelMappingRenderer();

	//~ Begin IDMXPixelMappingRenderer implementation
	virtual void DownsampleRender_GameThread(
		FTextureResource* TextureResource, 
		FTextureResource* DstTexture, 
		FTextureRenderTargetResource* DstTextureTargetResource, 
		FVector4 PixelFactor,
		FIntVector4 InvertPixel,
		float X, float Y,
		float SizeX, float SizeY,
		float U, float V,
		float SizeU, float SizeV,
		FIntPoint TargetSize, FIntPoint TextureSize,
		SurfaceReadCallback ReadCallback) override;
	virtual void RenderMaterial(UTextureRenderTarget2D* InRenderTarget, UMaterialInterface* InMaterialInterface) const override;
	virtual void RenderWidget(UTextureRenderTarget2D* InRenderTarget, UUserWidget* InUserWidget) const override;

	virtual void RenderTextureToRectangle_GameThread(const FTextureResource* InTextureResource, const FTexture2DRHIRef InRenderTargetTexture, FVector2D InSize, bool bSRGBSource) const override;

#if WITH_EDITOR
	virtual void RenderPreview_GameThread(FTextureResource* TextureResource, const TArray<FDMXPixelMappingRendererPreviewInfo>& PreviewInfos) const override;
#endif // WITH_EDITOR
	//~ End IDMXPixelMappingRenderer implementation

private:
	void Render_RenderThread(FRHICommandListImmediate& RHICmdList, const FRenderContext& InContext, SurfaceReadCallback InCallback);

private:
	/** Bruch for Material widget renderer */
	TSharedPtr<FSlateMaterialBrush> UIMaterialBrush;

	/** Material widget renderer */
	TSharedPtr<FWidgetRenderer> MaterialWidgetRenderer;

	/** UMG widget renderer */
	TSharedPtr<FWidgetRenderer> UMGRenderer;

	/** The public interface of the renderer module. */
	IRendererModule* RendererModule;
};