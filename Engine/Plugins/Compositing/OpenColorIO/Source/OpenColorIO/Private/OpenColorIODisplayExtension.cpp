// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenColorIODisplayExtension.h"

#include "CoreGlobals.h"
#include "OpenColorIOConfiguration.h"
#include "OpenColorIOColorTransform.h"
#include "OpenColorIODisplayManager.h"
#include "OpenColorIOModule.h"
#include "SceneView.h"

#include "GlobalShader.h"
#include "OpenColorIOShader.h"
#include "PipelineStateCache.h"
#include "RenderTargetPool.h"
#include "Shader.h"
#include "OpenColorIORenderingPrivate.h"

#include "RHI.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "CommonRenderResources.h"
#include "PostProcess/PostProcessing.h"
#include "Containers/DynamicRHIResourceArray.h"
// for FPostProcessMaterialInputs
#include "PostProcess/PostProcessMaterial.h"

float FOpenColorIODisplayExtension::DefaultDisplayGamma = 2.2f;

FOpenColorIODisplayExtension::FOpenColorIODisplayExtension(const FAutoRegister& AutoRegister, FViewportClient* AssociatedViewportClient)
	: FSceneViewExtensionBase(AutoRegister)
	, LinkedViewportClient(AssociatedViewportClient)
{
}

bool FOpenColorIODisplayExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	if (Context.Viewport && LinkedViewportClient == Context.Viewport->GetClient() && DisplayConfiguration.bIsEnabled)
	{
		return DisplayConfiguration.ColorConfiguration.IsValid();
	}

	return false;
}

void FOpenColorIODisplayExtension::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (DisplayConfiguration.ColorConfiguration.ConfigurationSource)
	{
		Collector.AddReferencedObject(DisplayConfiguration.ColorConfiguration.ConfigurationSource);
	}
}

void FOpenColorIODisplayExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	//Cache render resource so they are available on the render thread (Can't access UObjects on RT)
	//If something fails, cache invalid resources to invalidate them
	FOpenColorIOTransformResource* ShaderResource = nullptr;
	FTextureResource* LUT3dResource = nullptr;

	if (DisplayConfiguration.ColorConfiguration.ConfigurationSource == nullptr)
	{
		UE_LOG(LogOpenColorIO, Warning, TEXT("Can't apply display look - Invalid config asset"));
	}
	else
	{
		const bool bFoundTransform = DisplayConfiguration.ColorConfiguration.ConfigurationSource->GetShaderAndLUTResources(
			InViewFamily.GetFeatureLevel()
			, DisplayConfiguration.ColorConfiguration.SourceColorSpace.ColorSpaceName
			, DisplayConfiguration.ColorConfiguration.DestinationColorSpace.ColorSpaceName
			, ShaderResource
			, LUT3dResource);

		if (!bFoundTransform)
		{
			UE_LOG(LogOpenColorIO, Warning, TEXT("Can't apply display look - Couldn't find shader to transform from %s to %s"), *DisplayConfiguration.ColorConfiguration.SourceColorSpace.ColorSpaceName, *DisplayConfiguration.ColorConfiguration.DestinationColorSpace.ColorSpaceName);
		}
		else
		{
			// Transform was found, so shader must be there but doesn't mean the actual shader is available
			check(ShaderResource);
			if (ShaderResource->GetShaderGameThread<FOpenColorIOPixelShader_RDG>().IsNull())
			{
				UE_LOG(LogOpenColorIO, Warning, TEXT("Can't apply display look - Shader was invalid for Resource %s"), *ShaderResource->GetFriendlyName());

				//Invalidate shader resource
				ShaderResource = nullptr;
			}
			else
			{
				//Force ToneCurve to be off while we'are alive to make sure the input color space is the working space : srgb linear
				InViewFamily.EngineShowFlags.SetToneCurve(false);
				// This flags sets tonampper to output to ETonemapperOutputDevice::LinearNoToneCurve
				InViewFamily.SceneCaptureSource = SCS_FinalColorHDR;

				InView.FinalPostProcessSettings.bOverride_ToneCurveAmount = 1;
				InView.FinalPostProcessSettings.ToneCurveAmount = 0.0;
			}
		}
	}

	ENQUEUE_RENDER_COMMAND(ProcessColorSpaceTransform)(
		[this, ShaderResource, LUT3dResource](FRHICommandListImmediate& RHICmdList)
		{
			//Caches render thread resource to be used when applying configuration in PostRenderViewFamily_RenderThread
			CachedResourcesRenderThread.ShaderResource = ShaderResource;
			CachedResourcesRenderThread.LUT3dResource = LUT3dResource;
		}
	);
}

namespace {
	template<typename TSetupFunction>
	void DrawScreenPass(
		FRHICommandListImmediate& RHICmdList,
		const FSceneView& View,
		const FScreenPassTextureViewport& OutputViewport,
		const FScreenPassTextureViewport& InputViewport,
		const FScreenPassPipelineState& PipelineState,
		TSetupFunction SetupFunction)
	{
		PipelineState.Validate();

		const FIntRect InputRect = InputViewport.Rect;
		const FIntPoint InputSize = InputViewport.Extent;
		const FIntRect OutputRect = OutputViewport.Rect;
		const FIntPoint OutputSize = OutputRect.Size();

		RHICmdList.SetViewport(OutputRect.Min.X, OutputRect.Min.Y, 0.0f, OutputRect.Max.X, OutputRect.Max.Y, 1.0f);

		SetScreenPassPipelineState(RHICmdList, PipelineState);

		// Setting up buffers.
		SetupFunction(RHICmdList);

		FIntPoint LocalOutputPos(FIntPoint::ZeroValue);
		FIntPoint LocalOutputSize(OutputSize);
		EDrawRectangleFlags DrawRectangleFlags = EDRF_UseTriangleOptimization;

		DrawPostProcessPass(
			RHICmdList,
			LocalOutputPos.X, LocalOutputPos.Y, LocalOutputSize.X, LocalOutputSize.Y,
			InputRect.Min.X, InputRect.Min.Y, InputRect.Width(), InputRect.Height(),
			OutputSize,
			InputSize,
			PipelineState.VertexShader,
			View.StereoPass,
			false,
			DrawRectangleFlags);
	}

}

void FOpenColorIODisplayExtension::SubscribeToPostProcessingPass(EPostProcessingPass PassId, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	if (PassId == EPostProcessingPass::Tonemap)
	{
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FOpenColorIODisplayExtension::PostProcessPassAfterTonemap_RenderThread));
	}
}

FScreenPassTexture FOpenColorIODisplayExtension::PostProcessPassAfterTonemap_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& InOutInputs)
{

	const FSceneViewFamily& ViewFamily = *View.Family;

	const ERHIFeatureLevel::Type FeatureLevel = View.GetFeatureLevel();

	const FScreenPassTexture& SceneColor = InOutInputs.Textures[(uint32)EPostProcessMaterialInput::SceneColor];

	//If the shader resource could not be found, skip this frame. The LUT isn't required
	if (CachedResourcesRenderThread.ShaderResource == nullptr)
	{
		return SceneColor;
	}

	if (!(SceneColor.Texture->Desc.Flags & ETextureCreateFlags::TexCreate_ShaderResource))
	{
		return SceneColor;
	}

	if (!SceneColor.IsValid())
	{
		return SceneColor;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "OCIODisplayLook");

	{
		// Get shader from shader map.
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(ViewFamily.GetFeatureLevel());

		
		FScreenPassRenderTarget BackBufferRenderTarget;

		// If the override output is provided it means that this is the last pass in post processing.
		if (InOutInputs.OverrideOutput.IsValid())
		{
			BackBufferRenderTarget = InOutInputs.OverrideOutput;
		}
		else
		{
			// Reusing the same output description for our back buffer as SceneColor when it's not overriden
			FRDGTextureDesc OutputDesc = SceneColor.Texture->Desc;
			OutputDesc.Flags |= TexCreate_RenderTargetable;
			FLinearColor ClearColor(0., 0., 0., 0.);
			OutputDesc.ClearValue = FClearValueBinding(ClearColor);

			FRDGTexture* BackBufferRenderTargetTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("BackBufferRenderTargetTexture"));
			BackBufferRenderTarget = FScreenPassRenderTarget(BackBufferRenderTargetTexture, SceneColor.ViewRect, ERenderTargetLoadAction::EClear);
		}

		//Get input and output viewports. Backbuffer could be targeting a different region than input viewport
		const FScreenPassTextureViewport SceneColorViewport(SceneColor);
		const FScreenPassTextureViewport BackBufferViewport(BackBufferRenderTarget);

		FScreenPassRenderTarget SceneColorRenderTarget(SceneColor, ERenderTargetLoadAction::ELoad);

		FRHIBlendState* DefaultBlendState = FScreenPassPipelineState::FDefaultBlendState::GetRHI();
		FRHIDepthStencilState* DepthStencilState = FScreenPassPipelineState::FDefaultDepthStencilState::GetRHI();

		{
			
			TShaderMapRef<FOpenColorIOVertexShader> VertexShader(GlobalShaderMap);
			TShaderRef<FOpenColorIOPixelShader_RDG> OCIOPixelShader = CachedResourcesRenderThread.ShaderResource->GetShaderGameThread<FOpenColorIOPixelShader_RDG>();

			const float DisplayGamma = View.Family->RenderTarget->GetDisplayGamma();

			FOpenColorIOPixelShaderParameters* Parameters = GraphBuilder.AllocParameters<FOpenColorIOPixelShaderParameters>();
			Parameters->InputTexture = SceneColorRenderTarget.Texture;
			Parameters->InputTextureSampler = TStaticSamplerState<>::GetRHI();
			if (CachedResourcesRenderThread.LUT3dResource)
			{
				Parameters->Ociolut3d = CachedResourcesRenderThread.LUT3dResource->TextureRHI;
			}
			Parameters->Ociolut3dSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			// There is a special case where post processing and tonemapper are disabled. In this case tonemapper applies a static display Inverse of Gamma which defaults to 2.2.
			// In the case when Both PostProcessing and ToneMapper are disabled we apply gamma manually. In every other case we apply inverse gamma before applying OCIO.
			Parameters->Gamma = (ViewFamily.EngineShowFlags.Tonemapper == 0) || (ViewFamily.EngineShowFlags.PostProcessing == 0) ? DefaultDisplayGamma : DefaultDisplayGamma/DisplayGamma;
			Parameters->RenderTargets[0] = BackBufferRenderTarget.GetRenderTargetBinding();

			// Main Pass
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ProcessOCIOColorSpaceXfrm"),
				Parameters,
				ERDGPassFlags::Raster,
				[&View,
				VertexShader,
				OCIOPixelShader,
				DefaultBlendState,
				DepthStencilState,
				SceneColorViewport,
				BackBufferViewport,
				Parameters](FRHICommandListImmediate& RHICmdList)
			{
				DrawScreenPass(
					RHICmdList,
					View,
					BackBufferViewport,
					SceneColorViewport,
					FScreenPassPipelineState(VertexShader, OCIOPixelShader, DefaultBlendState, DepthStencilState),
					[&](FRHICommandListImmediate& RHICmdList)
				{
					SetShaderParameters(RHICmdList, OCIOPixelShader, OCIOPixelShader.GetPixelShader(), *Parameters);
				});

			});

			return MoveTemp(BackBufferRenderTarget);
		}
	}
}