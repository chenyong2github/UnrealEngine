// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderInputTextureProxy.h"

#include "IDMXPixelMappingRenderer.h"
#include "IDMXPixelMappingRendererModule.h"

#include "Blueprint/UserWidget.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"


namespace UE::DMXPixelMapping::Rendering::Private
{
	static FDMXPixelMappingInputTextureRenderingParameters MakeRenderingParameters(const FDMXPixelMappingRenderInputTextureParameters& Parameters)
	{
		FDMXPixelMappingInputTextureRenderingParameters RenderingParams;
		RenderingParams.NumDownsamplePasses = Parameters.NumDownSamplePasses;
		RenderingParams.PostProcessMID = Parameters.PostProcessMID;
		RenderingParams.BlurDistance = Parameters.BlurDistance;
		RenderingParams.bApplyPostProcessMaterialEachDownsamplePass = Parameters.bApplyPostProcessMaterialEachDownsamplePass;
		RenderingParams.PostProcessMaterialInputTextureParameterName = Parameters.InputTextureParameterName;
		RenderingParams.BlurDistanceParameterName = Parameters.BlurDistanceParamtereName;
		RenderingParams.OutputSize = Parameters.OutputSize;

		return RenderingParams;
	}

	template<>
	FRenderInputTextureProxy<UTexture>::FRenderInputTextureProxy(const TSharedRef<IDMXPixelMappingRenderer>& InPixelMappingRenderer, UTexture* InTexture, const FDMXPixelMappingRenderInputTextureParameters& InParameters)
		: PixelMappingRenderer(InPixelMappingRenderer)
		, WeakInputTexture(InTexture)
		, Parameters(InParameters)
	{}

	template<>
	FRenderInputTextureProxy<UMaterialInterface>::FRenderInputTextureProxy(const TSharedRef<IDMXPixelMappingRenderer>& InPixelMappingRenderer, UMaterialInterface* InMaterial, const FDMXPixelMappingRenderInputTextureParameters& InParameters, const FVector2D& InSize)
		: PixelMappingRenderer(InPixelMappingRenderer)
		, WeakInputTexture(InMaterial)
		, Parameters(InParameters)
	{
		IntermediateRenderTarget = NewObject<UTextureRenderTarget2D>();
		IntermediateRenderTarget->ClearColor = FLinearColor::Black;
		IntermediateRenderTarget->InitAutoFormat(InSize.X, InSize.Y);
		IntermediateRenderTarget->UpdateResourceImmediate();
	}

	template<>
	FRenderInputTextureProxy<UUserWidget>::FRenderInputTextureProxy(const TSharedRef<IDMXPixelMappingRenderer>& InPixelMappingRenderer, UUserWidget* InUserWidget, const FDMXPixelMappingRenderInputTextureParameters& InParameters, const FVector2D& InSize)
		: PixelMappingRenderer(InPixelMappingRenderer)
		, WeakInputTexture(InUserWidget)
		, Parameters(InParameters)
	{
		IntermediateRenderTarget = NewObject<UTextureRenderTarget2D>();
		IntermediateRenderTarget->ClearColor = FLinearColor::Black;
		IntermediateRenderTarget->InitAutoFormat(InSize.X, InSize.Y);
		IntermediateRenderTarget->UpdateResourceImmediate();
	}

	template <typename TextureType>
	void FRenderInputTextureProxy<TextureType>::UpdateParameters(const FDMXPixelMappingRenderInputTextureParameters& InParameters)
	{
		Parameters = InParameters;
	}

	template<>
	void FRenderInputTextureProxy<UTexture>::Render()
	{
		const FDMXPixelMappingInputTextureRenderingParameters RenderingParams = MakeRenderingParameters(Parameters);

		PixelMappingRenderer->PostProcessTexture(WeakInputTexture.Get(), RenderingParams);
	}

	template<>
	void FRenderInputTextureProxy<UMaterialInterface>::Render()
	{
		check(IntermediateRenderTarget);
		PixelMappingRenderer->RenderMaterial(IntermediateRenderTarget.Get(), CastChecked<UMaterialInterface>(WeakInputTexture.Get()));

		const FDMXPixelMappingInputTextureRenderingParameters RenderingParams = MakeRenderingParameters(Parameters);

		PixelMappingRenderer->PostProcessTexture(IntermediateRenderTarget.Get(), RenderingParams);
	}

	template<>
	void FRenderInputTextureProxy<UUserWidget>::Render()
	{
		check(IntermediateRenderTarget);
		PixelMappingRenderer->RenderWidget(IntermediateRenderTarget.Get(), CastChecked<UUserWidget>(WeakInputTexture.Get()));

		const FDMXPixelMappingInputTextureRenderingParameters RenderingParams = MakeRenderingParameters(Parameters);

		PixelMappingRenderer->PostProcessTexture(IntermediateRenderTarget.Get(), RenderingParams);
	}

	template <typename TextureType>
	UTexture* FRenderInputTextureProxy<TextureType>::GetRenderedTexture() const
	{
		return PixelMappingRenderer->GetPostProcessedTexture();
	}
}
