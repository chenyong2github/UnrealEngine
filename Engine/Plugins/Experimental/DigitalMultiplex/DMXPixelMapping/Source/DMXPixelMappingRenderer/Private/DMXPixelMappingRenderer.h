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
		FVector2D Position = FVector2D(0.f, 0.f);
		FVector2D Size = FVector2D(0.f, 0.f);
		FVector2D UV = FVector2D(0.f, 0.f);
		FVector2D UVSize = FVector2D(0.f, 0.f);
		FVector2D UVCellSize = FVector2D(0.f, 0.f);
		FIntPoint TargetSize;
		FIntPoint TextureSize;
		EDMXPixelBlendingQuality CellBlendingQuality;
		bool bStaticCalculateUV;
	};

public:
	/** Default constructor */
	FDMXPixelMappingRenderer();

	//~ Begin IDMXPixelMappingRenderer implementation
	virtual void DownsampleRender_GameThread(
		FTextureResource* InputTexture,
		FTextureResource* DstTexture,
		FTextureRenderTargetResource* DstTextureTargetResource,
		const FVector4& PixelFactor,
		const FIntVector4& InvertPixel,
		const FVector2D& Position,
		const FVector2D& Size,
		const FVector2D& UV,
		const FVector2D& UVSize,
		const FVector2D& UVCellSize,
		const FIntPoint& TargetSize,
		const FIntPoint& TextureSize,
		EDMXPixelBlendingQuality CellBlendingQuality,
		bool bStaticCalculateUV,
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