// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Rendering/IDMXPixelMappingRenderInputTextureProxy.h"

class IDMXPixelMappingRenderer;
class IDMXPixelMappingRenderInputTextureProxy;

class UTexture;
class UTextureRenderTarget2D;


namespace UE::DMXPixelMapping::Rendering::Private
{
	/** Base class for the render input texture proxy implementation. */
	template <typename TextureType>
	class FRenderInputTextureProxy
		: public IDMXPixelMappingRenderInputTextureProxy
	{
	public:
		FRenderInputTextureProxy(const TSharedRef<IDMXPixelMappingRenderer>& InPixelMappingRenderer, TextureType* InTexture, const FDMXPixelMappingRenderInputTextureParameters& InParameters);
		FRenderInputTextureProxy(const TSharedRef<IDMXPixelMappingRenderer>& InPixelMappingRenderer, TextureType* InTexture, const FDMXPixelMappingRenderInputTextureParameters& InParameters, const FVector2D& InSize);

	protected:
		//~ Begin DMXPixelMappingRenderInputTextureProxy interface
		virtual void UpdateParameters(const FDMXPixelMappingRenderInputTextureParameters& InParameters) override;
		virtual void Render() override;
		virtual UTexture* GetRenderedTexture() const override;
		//~ End DMXPixelMappingRenderInputTextureProxy interface

	private:
		/** The renderer used with this proxy */
		TSharedRef<IDMXPixelMappingRenderer> PixelMappingRenderer;

		/** The Input Texture */
		TWeakObjectPtr<TextureType> WeakInputTexture;

		/** Intermediate render target for materials and user widgets */
		TObjectPtr<UTextureRenderTarget2D> IntermediateRenderTarget;

		/** Parameters for renderering */
		FDMXPixelMappingRenderInputTextureParameters Parameters;
	};
}
