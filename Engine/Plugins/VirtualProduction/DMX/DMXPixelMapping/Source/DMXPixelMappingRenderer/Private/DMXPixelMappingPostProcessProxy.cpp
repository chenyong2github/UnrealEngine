// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXPixelMappingPostProcessProxy.h"

#include "CanvasTypes.h"
#include "CommonRenderResources.h"
#include "GlobalShader.h"
#include "RHIStaticStates.h"
#include "ScreenRendering.h"
#include "TextureResource.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialRenderProxy.h"
#include "Modules/ModuleManager.h"


namespace UE::DMXPixelMapping::Renderer::Private
{
	FDMXPixelMappingPostProcessCanvas::FDMXPixelMappingPostProcessCanvas()
	{
		Canvas = NewObject<UCanvas>();
	}

	void FDMXPixelMappingPostProcessCanvas::DrawMaterialToRenderTarget(UTextureRenderTarget2D* TextureRenderTarget, UMaterialInterface* Material)
	{
		if (!Material || !TextureRenderTarget || !TextureRenderTarget->GetResource())
		{
			return;
		}

		// This is a user-facing function, so we'd rather make sure that shaders are ready by the time we render, in order to ensure we don't draw with a fallback material
		Material->EnsureIsComplete();
		FTextureRenderTargetResource* RenderTargetResource = TextureRenderTarget->GameThread_GetRenderTargetResource();
		if (!RenderTargetResource)
		{
			return;
		}

		FCanvas RenderCanvas(
			RenderTargetResource,
			nullptr,
			GEngine->GetWorld(),
			GMaxRHIFeatureLevel);

		Canvas->Init(TextureRenderTarget->SizeX, TextureRenderTarget->SizeY, nullptr, &RenderCanvas);

		{
			ENQUEUE_RENDER_COMMAND(FlushDeferredResourceUpdateCommand)(
				[RenderTargetResource](FRHICommandListImmediate& RHICmdList)
				{
					RenderTargetResource->FlushDeferredResourceUpdate(RHICmdList);
				});

			Canvas->K2_DrawMaterial(Material, FVector2D(0, 0), FVector2D(TextureRenderTarget->SizeX, TextureRenderTarget->SizeY), FVector2D(0, 0));

			RenderCanvas.Flush_GameThread();
			Canvas->Canvas = nullptr;

			// UpdateResourceImmediate must be called here to ensure mips are generated.
			TextureRenderTarget->UpdateResourceImmediate(false);

			ENQUEUE_RENDER_COMMAND(ResetSceneTextureExtentHistory)(
				[RenderTargetResource](FRHICommandListImmediate& RHICmdList)
				{
					RenderTargetResource->ResetSceneTextureExtentsHistory();
				});
		}
	}

	void FDMXPixelMappingPostProcessCanvas::AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(Canvas);
	}


	void FDMXPixelMappingPostProccessProxy::Render(UTexture* InInputTexture, const FDMXPixelMappingInputTextureRenderingParameters& InParams)
	{
		InputTexture = InInputTexture;
		PostProcessMID = InParams.PostProcessMID;
		UpdateRenderTargets(InParams.OutputSize, InParams.NumDownsamplePasses);

		if (!CanRender())
		{
			return;
		}

		if (DownsampleRenderTargets.IsEmpty() && PostProcessMID)
		{
			// Render without downsampling
			PostProcessMID->SetTextureParameterValue(InParams.PostProcessMaterialInputTextureParameterName, InputTexture);
			PostProcessMID->SetScalarParameterValue(InParams.BlurDistanceParameterName, InParams.BlurDistance);
			PostProcessCanvas.DrawMaterialToRenderTarget(OutputRenderTarget, PostProcessMID);
		}
		else
		{
			// Render with downsampling. 
			// Note, if the last downsample render target is of minimal size, further renderer targets are not created.
			// Hence use the number of downsample render targets instead of InParams.NumDownsamplePasses.
			const int32 NumDownsamplePasses = DownsampleRenderTargets.Num();
			for (int32 DownsamplePass = 0; DownsamplePass < NumDownsamplePasses; DownsamplePass++)
			{
				UTexture* SourceTexture = DownsamplePass == 0 ? InputTexture : DownsampleRenderTargets[DownsamplePass - 1];
				UTextureRenderTarget2D* DownsampleRenderTarget = DownsampleRenderTargets[DownsamplePass].Get();

				const bool bApplyPostProcessMaterial = PostProcessMID && (InParams.bApplyPostProcessMaterialEachDownsamplePass || DownsampleRenderTarget == DownsampleRenderTargets.Last());
				if (bApplyPostProcessMaterial)
				{
					PostProcessMID->SetTextureParameterValue(InParams.PostProcessMaterialInputTextureParameterName, SourceTexture);
					PostProcessMID->SetScalarParameterValue(InParams.BlurDistanceParameterName, InParams.BlurDistance);
					PostProcessCanvas.DrawMaterialToRenderTarget(DownsampleRenderTarget, PostProcessMID);
				}
				else
				{
					RenderTextureToTarget(SourceTexture, DownsampleRenderTarget);
				}
			}

			// Upscale
			UTexture* LastSourceTexture = DownsampleRenderTargets.Last();
			RenderTextureToTarget(LastSourceTexture, OutputRenderTarget);
		}
	}

	UTexture* FDMXPixelMappingPostProccessProxy::GetRenderedTextureGameThread() const
	{
		return CanRender() ? OutputRenderTarget : InputTexture;
	}

	void FDMXPixelMappingPostProccessProxy::AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(InputTexture);
		Collector.AddReferencedObject(PostProcessMID);
		Collector.AddReferencedObjects(DownsampleRenderTargets);
		Collector.AddReferencedObject(OutputRenderTarget);
	}

	bool FDMXPixelMappingPostProccessProxy::CanRender() const
	{
		return InputTexture && OutputRenderTarget && FApp::CanEverRender();
	}

	void FDMXPixelMappingPostProccessProxy::UpdateRenderTargets(const FVector2D& OutputSize, int32 NumDownsamplePasses)
	{
		const bool bNeedsAnyRenderTargets = InputTexture && (NumDownsamplePasses > 0 || PostProcessMID);
		if (!bNeedsAnyRenderTargets)
		{
			OutputRenderTarget = nullptr;
			return;
		}

		bool bInvalidRendertargets = !OutputRenderTarget || DownsampleRenderTargets.IsEmpty() || DownsampleRenderTargets.Num() != NumDownsamplePasses;
		if (!bInvalidRendertargets)
		{
			if (OutputRenderTarget->GetSurfaceWidth() != OutputSize.X ||
				OutputRenderTarget->GetSurfaceHeight() != OutputSize.Y)
			{
				// Input texture was resized 
				bInvalidRendertargets = true;
			}
		}

		if (bInvalidRendertargets)
		{
			const FVector2D InputSize = FVector2D(InputTexture->GetSurfaceHeight(), InputTexture->GetSurfaceWidth());

			// Create downsample render targets
			DownsampleRenderTargets.Reset();
			FVector2D DownsampleSize = InputSize;
			for (int32 DownsamplePass = 0; DownsamplePass < NumDownsamplePasses; DownsamplePass++)
			{
				DownsampleSize /= 2.0;
				if (DownsampleSize.X <= 1.0 || DownsampleSize.Y <= 1.0)
				{
					break;
				}
				UTextureRenderTarget2D* DownsampleRenderTarget = NewObject<UTextureRenderTarget2D>();
				DownsampleRenderTarget->ClearColor = FLinearColor::Black;
				DownsampleRenderTarget->InitAutoFormat(DownsampleSize.X, DownsampleSize.Y);
				DownsampleRenderTarget->UpdateResourceImmediate();
				DownsampleRenderTargets.Add(DownsampleRenderTarget);
			}

			// Create output render target
			OutputRenderTarget = NewObject<UTextureRenderTarget2D>();
			OutputRenderTarget->ClearColor = FLinearColor::Black;
			OutputRenderTarget->InitAutoFormat(OutputSize.X, OutputSize.Y);
			OutputRenderTarget->UpdateResourceImmediate();
		}
	}

	void FDMXPixelMappingPostProccessProxy::RenderTextureToTarget(UTexture* Texture, UTextureRenderTarget2D* RenderTarget) const
	{
		IRendererModule* RendererModule = FModuleManager::GetModulePtr<IRendererModule>("Renderer");
		ENQUEUE_RENDER_COMMAND(PixelMappingPostProccessRenderToTargetPass)(
			[RendererModule, Texture, RenderTarget, this](FRHICommandListImmediate& RHICmdList)
			{
				if (!Texture || !Texture->GetResource())
				{
					return;
				}

				const FTextureRHIRef SourceTextureRHI = Texture->GetResource()->GetTexture2DRHI();
				const FTextureRHIRef TargetTextureRHI = RenderTarget->GetResource()->GetTexture2DRHI();

				FRHIRenderPassInfo RPInfo(TargetTextureRHI, MakeRenderTargetActions(ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::EStore));

				RHICmdList.Transition(FRHITransitionInfo(TargetTextureRHI, ERHIAccess::Unknown, ERHIAccess::RTV));
				RHICmdList.BeginRenderPass(RPInfo, TEXT("PixelMappingPostProcessProxy_RenderToTarget"));
				{
					RHICmdList.SetViewport(0, 0, 0.0f, (float)TargetTextureRHI->GetSizeX(), (float)TargetTextureRHI->GetSizeY(), 1.0f);

					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
					GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

					FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
					TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
					TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					GraphicsPSOInit.PrimitiveType = PT_TriangleList;

					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

					SetShaderParametersLegacyPS(RHICmdList, PixelShader, TStaticSamplerState<SF_Bilinear>::GetRHI(), SourceTextureRHI);

					RendererModule->DrawRectangle(RHICmdList,
						0, 0,									// Dest X, Y
						(float)TargetTextureRHI->GetSizeX(),	// Dest Width
						(float)TargetTextureRHI->GetSizeY(),	// Dest Height
						0, 0,									// Source U, V
						1, 1,									// Source USize, VSize
						TargetTextureRHI->GetSizeXY(),			// Target buffer size
						FIntPoint(1, 1),						// Source texture size
						VertexShader,
						EDRF_Default);
				}
				RHICmdList.EndRenderPass();
			});
	}
}
