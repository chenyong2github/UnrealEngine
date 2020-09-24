// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"

class FTextureResource;
class FTextureRenderTargetResource;
class IDMXPixelMappingRenderer;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UUserWidget;
enum class EDMXPixelBlendingQuality : uint8;

/**
 * FDMXPixelMappingRendererPreviewInfo holds properties for the group rendering of multiple downsampled textures
 */
struct FDMXPixelMappingRendererPreviewInfo
{
	const FTextureResource* TextureResource = nullptr;
	FVector2D TextureSize;
	FVector2D TexturePosition;
};

/**
 * The public interface of the Pixel Mapping renderer instance interface.
 */
class IDMXPixelMappingRenderer 
	: public TSharedFromThis<IDMXPixelMappingRenderer>
{
public:
	using SurfaceReadCallback = TFunction<void(TArray<FColor>&, FIntRect&)>;

public:
	/** Virtual destructor */
	virtual ~IDMXPixelMappingRenderer() {}

	/**
	 * Downsample and Draw input texture to Destination texture. TODO: May want to refactor to use FRenderContext directly.
	 *
	 * @param InputTexture					Rendering resource of input texture
	 * @param DstTexture					Rendering resource of RenderTarget texture
	 * @param DstTextureTargetResource		Rendering resource for render target
	 * @param PixelFactor					RGBA pixel multiplicator
	 * @param InvertPixel					RGBA pixel flag for inversion
	 * @param Position 						Position in screen pixels of the top left corner of the quad
	 * @param Size    						Size in screen pixels of the quad
	 * @param UV							Position in texels of the top left corner of the quad's UV's
	 * @param UVSize    					Size in texels of the quad's total UV space
	 * @param UVCellSize					Size in texels of UV. May match UVSize
	 * @param TargetSize					Size in texels of the target texture
	 * @param TextureSize					Size in texels of the source texture
	 * @param ReadCallback					ReadSurfaceData from DstTextureTargetResource callback, it holds CPU FColor array and size of the read surface
	 * @param CellBlendingQuality			The quality of color samples in the pixel shader (number of samples)
	 * @param bStaticCalculateUV			Calculates the UV point to sample purely on the UV position/size. Works best for renderers which represent a single pixel
	 */
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
		SurfaceReadCallback ReadCallback) = 0;

	/**
	 * Render material into the RenderTarget2D
	 *
	 * @param InRenderTarget				2D render target texture resource
	 * @param InMaterialInterface			Material to use
	 */
	virtual void RenderMaterial(UTextureRenderTarget2D* InRenderTarget, UMaterialInterface* InMaterialInterface) const = 0;

	/**
	 * Render material into the RenderTarget2D
	 *
	 * @param InRenderTarget				2D render target texture resource
	 * @param InUserWidget					UMG widget to use
	 */
	virtual void RenderWidget(UTextureRenderTarget2D* InRenderTarget, UUserWidget* InUserWidget) const  = 0;

	/**
	 * Rendering input texture to render target
	 *
	 * @param InTextureResource				Input texture resource
	 * @param InRenderTargetTexture			RenderTarget
	 * @param InSize						Rendering size
	 * @param bSRGBSource					If the source texture is sRGB
	 */
	virtual void RenderTextureToRectangle_GameThread(const FTextureResource* InTextureResource, const FTexture2DRHIRef InRenderTargetTexture, FVector2D InSize, bool bSRGBSource) const = 0;

#if WITH_EDITOR
	/**
	 * Render preview with one or multiple downsampled textures
	 *
	 * @param TextureResource				Rendering resource of RenderTarget texture
	 * @param PreviewInfos					Array of input previews
	 */
	virtual void RenderPreview_GameThread(FTextureResource* TextureResource, const TArray<FDMXPixelMappingRendererPreviewInfo>& PreviewInfos) const = 0;
#endif // WITH_EDITOR

	/**
	* Sets the brigthness of the renderer
	*/
	void SetBrightness(float InBrightness) { Brightness = InBrightness; }

protected:
	/** Brightness multiplier for the renderer */
	float Brightness = 1.0f;
};
