// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDMXPixelMappingRenderer.h"

#include "UObject/GCObject.h"
#include "UObject/ObjectPtr.h"

class UCanvas;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture;
class UTextureRenderTarget2D;


namespace UE::DMXPixelMapping::Renderer::Private
{
	/** Canvas implementation for pixelmapping */
	class FDMXPixelMappingPostProcessCanvas
		: public FGCObject
	{
	public:
		/** Constructor */
		FDMXPixelMappingPostProcessCanvas();

		/** Draws a material to a render target */
		void DrawMaterialToRenderTarget(UTextureRenderTarget2D* TextureRenderTarget, UMaterialInterface* Material);

	protected:
		//~ Begin FGCObject interface
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override
		{
			return TEXT("FDMXPixelMappingPostProccessProxy");
		}
		//~ End FGCObject interface

	private:
		/** Canvas to be used for post processing */
		TObjectPtr<UCanvas> Canvas;
	};


	/** Proxy to post process the texture used in Pixel Mapping */
	class FDMXPixelMappingPostProccessProxy
		: public FGCObject
	{
	public:
		/** Renders the input texture  */
		void Render(UTexture* InInputTexture, const FDMXPixelMappingInputTextureRenderingParameters& InParams);

		/** Returns the rendered texture, or nullptr if no texture was rendered */
		UTexture* GetRenderedTextureGameThread() const;

	protected:
		//~ Begin FGCObject interface
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override
		{
			return TEXT("FDMXPixelMappingPostProccessProxy");
		}
		//~ End FGCObject interface

	private:
		/** Returns true if rendering is needed */
		bool CanRender() const;

		/** Updates render targets */
		void UpdateRenderTargets(const FVector2D& OutputSize, int32 NumDownsamplePasses);

		/** Renders the Input Texture to the Output render target*/
		void RenderTextureToTarget(UTexture* Texture, UTextureRenderTarget2D* RenderTarget) const;

		/** The number of times rendering is required, either to downsample or applying the post process material */
		int32 NumRenderPasses = 0;

		/** Weak ref to the input texture */
		TObjectPtr<UTexture> InputTexture;

		/** Post process material instance dynamic to be applied to the input texture */
		TObjectPtr<UMaterialInstanceDynamic> PostProcessMID;

		/** Downsample render targets */
		TArray<TObjectPtr<UTextureRenderTarget2D>> DownsampleRenderTargets;

		/** The render target that is output. Only valid if rendering is needed */
		TObjectPtr<UTextureRenderTarget2D> OutputRenderTarget;

		/** Post process canvas used to draw materials */
		FDMXPixelMappingPostProcessCanvas PostProcessCanvas;
	};

}
