// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenColorIORendering.h"

#include "Engine/RendererSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GlobalShader.h"
#include "CommonRenderResources.h"
#include "Logging/LogMacros.h"
#include "Logging/MessageLog.h"
#include "OpenColorIOConfiguration.h"
#include "OpenColorIOModule.h"
#include "OpenColorIOShader.h"
#include "OpenColorIOShaderType.h"
#include "OpenColorIOShared.h"
#include "PipelineStateCache.h"
#include "RHIStaticStates.h"
#include "SceneInterface.h"
#include "SceneUtils.h"
#include "ShaderParameterUtils.h"
#include "TextureResource.h"
#include "ScreenPass.h"

namespace {
	/** This function is similar to DrawScreenPass in OpenColorIODisplayExtension.cpp except it is catered for Viewless texture rendering. */
	template<typename TSetupFunction>
	void DrawScreenPass(
		FRHICommandListImmediate& RHICmdList,
		const FIntPoint& OutputResolution,
		const FScreenPassPipelineState& PipelineState,
		TSetupFunction SetupFunction)
	{
		RHICmdList.SetViewport(0.f, 0.f, 0.f, OutputResolution.X, OutputResolution.Y, 1.0f);

		SetScreenPassPipelineState(RHICmdList, PipelineState);

		// Setting up buffers.
		SetupFunction(RHICmdList);

		FIntPoint LocalOutputPos(FIntPoint::ZeroValue);
		FIntPoint LocalOutputSize(OutputResolution);
		EDrawRectangleFlags DrawRectangleFlags = EDRF_UseTriangleOptimization;

		DrawPostProcessPass(
			RHICmdList,
			LocalOutputPos.X, LocalOutputPos.Y, LocalOutputSize.X, LocalOutputSize.Y,
			0., 0., OutputResolution.X, OutputResolution.Y,
			OutputResolution,
			OutputResolution,
			PipelineState.VertexShader,
			EStereoscopicPass::eSSP_FULL,
			false,
			DrawRectangleFlags);
	}
}


void ProcessOCIOColorSpaceTransform_RenderThread(
	FRHICommandListImmediate& InRHICmdList
	, ERHIFeatureLevel::Type InFeatureLevel
	, FOpenColorIOTransformResource* InOCIOColorTransformResource
	, FTextureResource* InLUT3dResource
	, FTextureRHIRef InputSpaceColorTexture
	, FTextureRHIRef OutputSpaceColorTexture
	, FIntPoint OutputResolution)
{
	check(IsInRenderingThread());

	SCOPED_DRAW_EVENT(InRHICmdList, ProcessOCIOColorSpaceTransform);

	InRHICmdList.Transition(FRHITransitionInfo(OutputSpaceColorTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

	FRHIRenderPassInfo RPInfo(OutputSpaceColorTexture, ERenderTargetActions::DontLoad_Store);
	InRHICmdList.BeginRenderPass(RPInfo, TEXT("ProcessOCIOColorSpaceXfrm"));

	// Get shader from shader map.
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(InFeatureLevel);
	TShaderMapRef<FOpenColorIOVertexShader> VertexShader(GlobalShaderMap);
	TShaderRef<FOpenColorIOPixelShader> OCIOPixelShader = InOCIOColorTransformResource->GetShader<FOpenColorIOPixelShader>();

	FScreenPassPipelineState PipelineState(VertexShader, OCIOPixelShader, TStaticBlendState<>::GetRHI(), TStaticDepthStencilState<false, CF_Always>::GetRHI());
	DrawScreenPass(InRHICmdList, OutputResolution, PipelineState, [&](FRHICommandListImmediate& RHICmdList)
	{
		// Set Gamma to 1., since we do not have any display parameters or requirement for Gamma.
		const float Gamma = 1.0;

		// Update pixel shader parameters.
		OCIOPixelShader->SetParameters(InRHICmdList, InputSpaceColorTexture, Gamma);

		if (InLUT3dResource != nullptr)
		{
			OCIOPixelShader->SetLUTParameter(InRHICmdList, InLUT3dResource);
		}
	});

	// Resolve render target.
	InRHICmdList.EndRenderPass();

	// Restore readable state
	InRHICmdList.Transition(FRHITransitionInfo(OutputSpaceColorTexture, ERHIAccess::RTV, ERHIAccess::SRVGraphics));
}

// static
bool FOpenColorIORendering::ApplyColorTransform(UWorld* InWorld, const FOpenColorIOColorConversionSettings& InSettings, UTexture* InTexture, UTextureRenderTarget2D* OutRenderTarget)
{
	check(IsInGameThread());

	if (InSettings.ConfigurationSource == nullptr)
	{
		UE_LOG(LogOpenColorIO, Warning, TEXT("Can't apply color transform - Invalid config asset"));
		return false;
	}

	if (InTexture == nullptr)
	{
		UE_LOG(LogOpenColorIO, Warning, TEXT("Can't apply color transform - Invalid Input Texture"));
		return false;
	}

	if (OutRenderTarget == nullptr)
	{
		UE_LOG(LogOpenColorIO, Warning, TEXT("Can't apply color transform - Invalid Output Texture"));
		return false;
	}


	FTextureResource* InputResource = InTexture->Resource;
	FTextureResource* OutputResource = OutRenderTarget->Resource;
	if (InputResource == nullptr)
	{
		UE_LOG(LogOpenColorIO, Warning, TEXT("Can't apply color transform - Invalid Input Texture resource"));
		return false;
	}

	if (OutputResource == nullptr)
	{
		UE_LOG(LogOpenColorIO, Warning, TEXT("Can't apply color transform - Invalid Output Texture resource"));
		return false;
	}

	const ERHIFeatureLevel::Type FeatureLevel = InWorld->Scene->GetFeatureLevel();
	FOpenColorIOTransformResource* ShaderResource = nullptr;
	FTextureResource* LUT3dResource = nullptr;
	bool bFoundTransform = InSettings.ConfigurationSource->GetShaderAndLUTResources(FeatureLevel, InSettings.SourceColorSpace.ColorSpaceName, InSettings.DestinationColorSpace.ColorSpaceName, ShaderResource, LUT3dResource);
	if (!bFoundTransform)
	{
		UE_LOG(LogOpenColorIO, Warning, TEXT("Can't apply color transform - Couldn't find shader to transform from %s to %s"), *InSettings.SourceColorSpace.ColorSpaceName, *InSettings.DestinationColorSpace.ColorSpaceName);
		return false;
	}

	check(ShaderResource);

	if (ShaderResource->GetShaderGameThread<FOpenColorIOPixelShader>().IsNull())
	{
		UE_LOG(LogOpenColorIO, Warning, TEXT("OCIOPass - Shader was invalid for Resource %s"), *ShaderResource->GetFriendlyName());
		return false;
	}


	ENQUEUE_RENDER_COMMAND(ProcessColorSpaceTransform)(
		[FeatureLevel, InputResource, OutputResource, ShaderResource, LUT3dResource](FRHICommandListImmediate& RHICmdList)
		{
			ProcessOCIOColorSpaceTransform_RenderThread(
			RHICmdList,
			FeatureLevel,
			ShaderResource,
			LUT3dResource,
			InputResource->TextureRHI,
			OutputResource->TextureRHI,
			FIntPoint(OutputResource->GetSizeX(), OutputResource->GetSizeY()));
		}
	);
	return true;
}
