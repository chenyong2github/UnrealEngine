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
	 * Downsample and Draw input texture to Destination texture.
	 *
	 * @params InputTexture					Rendering resource of input texture
	 * @param DstTexture					Rendering resource of RenderTarget texture
	 * @param DstTextureTargetResource		Rendering resource for render target
	 * @param PixelFactor					RGBA pixel multiplicator
	 * @param FIntVector4					RGBA pixel flag for inversion
	 * @param X 							Position in screen pixels of the top left corner of the quad. X value
	 * @param Y 							Position in screen pixels of the top left corner of the quad. Y value
	 * @param SizeX    						Size in screen pixels of the quad. X value
	 * @param SizeY 						Size in screen pixels of the quad. Y value
	 * @param U								Position in texels of the top left corner of the quad's UV's. U value
	 * @param V								Position in texels of the top left corner of the quad's UV's. V value
	 * @param SizeU    						Size in texels of the quad's UV's. U value
	 * @param SizeV		    				Size in texels of the quad's UV's. V value
	 * @param TargetSizeX					sSize in screen pixels of the target surface. X value
	 * @param TargetSizeY					Size in screen pixels of the target surface. Y value
	 * @param TextureSize                   Size in texels of the source texture
	 * @param VertexShader					The vertex shader used for rendering
	 * @param SurfaceReadCallback			ReadSurfaceData from DstTextureTargetResource callback, it holds CPU FColor array and size of the read surface
	 */
	virtual void DownsampleRender_GameThread(
		FTextureResource* InputTexture,
		FTextureResource* DstTexture, 
		FTextureRenderTargetResource* DstTextureTargetResource,
		FVector4 PixelFactor,
		FIntVector4 InvertPixel,
		float X, float Y,
		float SizeX, float SizeY,
		float U, float V,
		float SizeU, float SizeV,
		FIntPoint TargetSize,
		FIntPoint TextureSize,
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
	 * @param InRenderTarget		2D render target texture resource
	 * @param UUserWidget			UMG widget to use
	 */
	virtual void RenderWidget(UTextureRenderTarget2D* InRenderTarget, UUserWidget* InUserWidget) const  = 0;

	/**
	 * Rendering input texture to render target
	 *
	 * @param InTextureResource			Input texture resource
	 * @param InRenderTargetTexture		RenderTarget
	 * @param InSize					Rendering size
	 * @param bSRGBSource				If the source texture is sRGB
	 */
	virtual void RenderTextureToRectangle_GameThread(const FTextureResource* InTextureResource, const FTexture2DRHIRef InRenderTargetTexture, FVector2D InSize, bool bSRGBSource) const = 0;

#if WITH_EDITOR
	/**
	 * Render preview with one or multiple downsampled textures
	 *
	 * @param TextureResource		Rendering resource of RenderTarget texture
	 * @param PreviewInfos			Array of input previews
	 */
	virtual void RenderPreview_GameThread(FTextureResource* TextureResource, const TArray<FDMXPixelMappingRendererPreviewInfo>& PreviewInfos) const = 0;
#endif // WITH_EDITOR

};
