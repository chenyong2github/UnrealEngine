// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenColorIORendering.h"

#include "Engine/RendererSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GlobalShader.h"
#include "Logging/LogMacros.h"
#include "Logging/MessageLog.h"
#include "OpenColorIOColorSpace.h"
#include "OpenColorIOConfiguration.h"
#include "OpenColorIOModule.h"
#include "OpenColorIOShader.h"
#include "OpenColorIOShaderType.h"
#include "OpenColorIOShared.h"
#include "OpenColorIOColorTransform.h"
#include "ScreenPass.h"
#include "TextureResource.h"

namespace {
	FViewInfo CreateDummyViewInfo(const FIntRect& InViewRect)
	{
		FSceneViewFamily ViewFamily(FSceneViewFamily::ConstructionValues(nullptr, nullptr, FEngineShowFlags(ESFIM_Game))
			.SetTime(FGameTime())
			.SetGammaCorrection(1.0f));
		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.ViewFamily = &ViewFamily;
		ViewInitOptions.SetViewRectangle(InViewRect);
		ViewInitOptions.ViewOrigin = FVector::ZeroVector;
		ViewInitOptions.ViewRotationMatrix = FMatrix::Identity;
		ViewInitOptions.ProjectionMatrix = FMatrix::Identity;

		return FViewInfo(ViewInitOptions);
	}
}

void AddDrawOCIOScreenPass(
	FRHICommandListImmediate& InRHICmdList
	, const ERHIFeatureLevel::Type InFeatureLevel
	, FOpenColorIOTransformResource* InShaderResource
	, const TSortedMap<int32, FTextureResource*>& InTextureResources
	, FTextureRHIRef InputSpaceColorTexture
	, FTextureRHIRef OutputSpaceColorTexture
	, FIntPoint OutputResolution)
{
	check(IsInRenderingThread());

	FRDGBuilder GraphBuilder(InRHICmdList);

	FRDGTextureRef InputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(InputSpaceColorTexture->GetTexture2D(), TEXT("OCIOInputTexture")));
	FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(OutputSpaceColorTexture->GetTexture2D(), TEXT("OCIORenderTargetTexture")));
	FScreenPassTextureViewport Viewport = FScreenPassTextureViewport(OutputTexture);
	FScreenPassRenderTarget ScreenPassRenderTarget = FScreenPassRenderTarget(OutputTexture, FIntRect(FIntPoint::ZeroValue, OutputResolution), ERenderTargetLoadAction::EClear);
	FViewInfo DummyView = CreateDummyViewInfo(ScreenPassRenderTarget.ViewRect);

	if (InShaderResource != nullptr)
	{
		TShaderRef<FOpenColorIOPixelShader> OCIOPixelShader = InShaderResource->GetShader<FOpenColorIOPixelShader>();

		FOpenColorIOPixelShaderParameters* Parameters = GraphBuilder.AllocParameters<FOpenColorIOPixelShaderParameters>();
		Parameters->InputTexture = InputTexture;
		Parameters->InputTextureSampler = TStaticSamplerState<>::GetRHI();
		OpenColorIOBindTextureResources(Parameters, InTextureResources);
		// Set Gamma to 1., since we do not have any display parameters or requirement for Gamma.
		Parameters->Gamma = 1.0;
		Parameters->RenderTargets[0] = ScreenPassRenderTarget.GetRenderTargetBinding();

		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("DrawOCIOScreenPass"), DummyView, Viewport, Viewport, OCIOPixelShader, Parameters);
	}
	else
	{
		TShaderMapRef<FOpenColorIOErrorPassPS> OCIOPixelShader(GetGlobalShaderMap(InFeatureLevel));

		FOpenColorIOErrorShaderParameters* Parameters = GraphBuilder.AllocParameters<FOpenColorIOErrorShaderParameters>();
		Parameters->InputTexture = InputTexture;
		Parameters->InputTextureSampler = TStaticSamplerState<>::GetRHI();
		Parameters->MiniFontTexture = OpenColorIOGetMiniFontTexture();
		Parameters->RenderTargets[0] = ScreenPassRenderTarget.GetRenderTargetBinding();

		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("DrawOCIOErrorScreenPass"), DummyView, Viewport, Viewport, OCIOPixelShader, Parameters);
	}

	GraphBuilder.Execute();
}

// static
bool FOpenColorIORendering::ApplyColorTransform(UWorld* InWorld, const FOpenColorIOColorConversionSettings& InSettings, UTexture* InTexture, UTextureRenderTarget2D* OutRenderTarget)
{
	check(IsInGameThread());

	if (!ensureMsgf(InTexture, TEXT("Can't apply color transform - Invalid Input Texture")))
	{
		return false;
	}

	if (!ensureMsgf(OutRenderTarget, TEXT("Can't apply color transform - Invalid Output Texture")))
	{
		return false;
	}

	FTextureResource* InputResource = InTexture->GetResource();
	FTextureResource* OutputResource = OutRenderTarget->GetResource();
	if (!ensureMsgf(InputResource, TEXT("Can't apply color transform - Invalid Input Texture resource")))
	{
		return false;
	}

	if (!ensureMsgf(OutputResource, TEXT("Can't apply color transform - Invalid Output Texture resource")))
	{
		return false;
	}

	const ERHIFeatureLevel::Type FeatureLevel = InWorld->Scene->GetFeatureLevel();
	FOpenColorIOTransformResource* ShaderResource = nullptr;
	TSortedMap<int32, FTextureResource*> TransformTextureResources;

	if (InSettings.ConfigurationSource != nullptr)
	{
		const bool bFoundTransform = InSettings.ConfigurationSource->GetRenderResources(FeatureLevel, InSettings, ShaderResource, TransformTextureResources);

		if (bFoundTransform)
		{
			check(ShaderResource);
			if (ShaderResource->GetShaderGameThread<FOpenColorIOPixelShader>().IsNull())
			{
				ensureMsgf(false, TEXT("Can't apply display look - Shader was invalid for Resource %s"), *ShaderResource->GetFriendlyName());

				//Invalidate shader resource
				ShaderResource = nullptr;
			}
		}
	}
	
	ENQUEUE_RENDER_COMMAND(ProcessColorSpaceTransform)(
		[FeatureLevel, InputResource, OutputResource, ShaderResource, TextureResources = MoveTemp(TransformTextureResources)](FRHICommandListImmediate& RHICmdList)
		{
			AddDrawOCIOScreenPass(
				RHICmdList,
				FeatureLevel,
				ShaderResource,
				TextureResources,
				InputResource->TextureRHI,
				OutputResource->TextureRHI,
				FIntPoint(OutputResource->GetSizeX(), OutputResource->GetSizeY()));
		}
	);

	return ShaderResource != nullptr;
}
