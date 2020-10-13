// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXPixelMappingRenderer.h"
#include "DMXPixelMappingRendererCommon.h"
#include "DMXPixelMappingRendererShader.h"
#include "Library/DMXEntityFixtureType.h"

#include "TextureResource.h"
#include "RHIStaticStates.h"
#include "PixelShaderUtils.h"
#include "ScreenRendering.h"
#include "ClearQuad.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Images/SImage.h"
#include "SlateMaterialBrush.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "Blueprint/UserWidget.h"
#include "Modules/ModuleManager.h"
#include "RendererInterface.h"
#include "Materials/Material.h"

namespace DMXPixelMappingRenderer
{
	static constexpr auto RenderPassName = TEXT("RenderPixelMapping");
	static constexpr auto RenderPassHint = TEXT("Render Pixel Mapping");
};

DECLARE_GPU_STAT_NAMED(DMXPixelMappingShadersStat, DMXPixelMappingRenderer::RenderPassHint);

#if WITH_EDITOR
namespace DMXPixelMappingRenderer
{
	static constexpr auto RenderPreviewPassName = TEXT("PixelMappingPreview");
	static constexpr auto RenderPreviewPassHint = TEXT("Pixel Mapping Preview");
};

DECLARE_GPU_STAT_NAMED(DMXPixelMappingPreviewStat, DMXPixelMappingRenderer::RenderPreviewPassHint);
#endif // WITH_EDITOR

FDMXPixelMappingRenderer::FDMXPixelMappingRenderer()
{
	static const FName RendererModuleName("Renderer");
	RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);

	// Initialize Material Renderer
	if (!MaterialWidgetRenderer.IsValid())
	{
		const bool bUseGammaCorrection = false;
		MaterialWidgetRenderer = MakeShared<FWidgetRenderer>(bUseGammaCorrection);
		check(MaterialWidgetRenderer.IsValid());
	}

	// Initialize Material Brush
	if (!UIMaterialBrush.IsValid())
	{
		UIMaterialBrush = MakeShared<FSlateMaterialBrush>(FVector2D(1.f));
		check(UIMaterialBrush.IsValid());
	}

	// Initialize UMG renderer
	if (!UMGRenderer.IsValid())
	{
		const bool bUseGammaCorrection = true;
		UMGRenderer = MakeShared<FWidgetRenderer>(bUseGammaCorrection);
		check(UMGRenderer.IsValid());
	}
}


void FDMXPixelMappingRenderer::DownsampleRender_GameThread(
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
	SurfaceReadCallback ReadCallback)
{
	check(IsInGameThread());

	FRenderContext RenderContext
	{
		InputTexture,
		DstTexture,
		DstTextureTargetResource,

		FIntPoint(InputTexture->GetSizeX(), InputTexture->GetSizeY()),
		FIntPoint(DstTexture->GetSizeX(), DstTexture->GetSizeY()),
		Brightness * PixelFactor,
		InvertPixel,
		Position,
		Size,
		UV,
		UVSize,
		UVCellSize,
		TargetSize,
		TextureSize,
		CellBlendingQuality,
		bStaticCalculateUV,
	};

	ENQUEUE_RENDER_COMMAND(DMXPixelMappingRenderer)(
		[this, RenderContext, ReadCallback](FRHICommandListImmediate& RHICmdList)
		{
			Render_RenderThread(RHICmdList, RenderContext, [this, ReadCallback](TArray<FColor>& SurfaceBuffer, FIntRect& InRect)
			{
				ReadCallback(SurfaceBuffer, InRect);
			});
		}
	);
}

#if WITH_EDITOR
void FDMXPixelMappingRenderer::RenderPreview_GameThread(FTextureResource* TextureResource, const TArray<FDMXPixelMappingRendererPreviewInfo>& PreviewInfos) const
{
	check(IsInGameThread());

	struct FRenderContext
	{
		const FTextureResource* TextureResource = nullptr;
		TArray<FDMXPixelMappingRendererPreviewInfo> RenderConfig;
	};

	FRenderContext RenderContext
	{
		TextureResource,
		PreviewInfos
	};

	ENQUEUE_RENDER_COMMAND(DMXPixelMapping_CopyToPreveiewTexture)([this, RenderContext]
	(FRHICommandListImmediate& RHICmdList)
	{

		SCOPED_GPU_STAT(RHICmdList, DMXPixelMappingPreviewStat);
		SCOPED_DRAW_EVENTF(RHICmdList, DMXPixelMappingPreviewStat, DMXPixelMappingRenderer::RenderPreviewPassName);

		// Clear preview texture
		{
			FRHIRenderPassInfo RPInfo(RenderContext.TextureResource->TextureRHI, ERenderTargetActions::DontLoad_Store);
			TransitionRenderPassTargets(RHICmdList, RPInfo);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearCanvas"));
			RHICmdList.SetViewport(0.f, 0.f, 0.f, RenderContext.TextureResource->GetSizeX(), RenderContext.TextureResource->GetSizeY(), 1.f);
			DrawClearQuad(RHICmdList, FColor::Black);
			RHICmdList.EndRenderPass();
		}

		// Render Preview
		{
			FRHIRenderPassInfo RPInfo(RenderContext.TextureResource->TextureRHI, ERenderTargetActions::Load_Store);
			RHICmdList.BeginRenderPass(RPInfo, DMXPixelMappingRenderer::RenderPreviewPassName);
			{
				RHICmdList.SetViewport(0.f, 0.f, 0.f, RenderContext.TextureResource->GetSizeX(), RenderContext.TextureResource->GetSizeY(), 1.f);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
				TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
				TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				// Draw preview downsampled rectangles
				for (const FDMXPixelMappingRendererPreviewInfo& RenderConfig : RenderContext.RenderConfig)
				{
					if (RenderConfig.TextureResource != nullptr)
					{
						PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), RenderConfig.TextureResource->TextureRHI);

						RendererModule->DrawRectangle(
							RHICmdList,
							RenderConfig.TexturePosition.X, RenderConfig.TexturePosition.Y,											// Dest X, Y
							RenderConfig.TextureSize.X, RenderConfig.TextureSize.Y,													// Dest Width, Height
							0, 0,																									// Source U, V
							1, 1,																									// Source USize, VSize
							FIntPoint(RenderContext.TextureResource->GetSizeX(), RenderContext.TextureResource->GetSizeY()),		// Target buffer size
							FIntPoint(1, 1),																						// Source texture size
							VertexShader,
							EDRF_Default);
					}
				}
			}
			RHICmdList.EndRenderPass();
		}
	});
}
#endif // WITH_EDITOR

void FDMXPixelMappingRenderer::RenderMaterial(UTextureRenderTarget2D* InRenderTarget, UMaterialInterface* InMaterialInterface) const
{
	if (InMaterialInterface == nullptr)
	{
		return;
	}

	if (InRenderTarget == nullptr)
	{
		return;
	}

	FVector2D TextureSize = FVector2D(InRenderTarget->SizeX, InRenderTarget->SizeY);
	UMaterial* Material = InMaterialInterface->GetMaterial();

	if (Material != nullptr && Material->IsUIMaterial())
	{
		UIMaterialBrush->ImageSize = TextureSize;
		UIMaterialBrush->SetMaterial(InMaterialInterface);

		TSharedRef<SWidget> Widget =
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SImage)
				.Image(UIMaterialBrush.Get())
			];

		static const float DeltaTime = 0.f;
		MaterialWidgetRenderer->DrawWidget(InRenderTarget, Widget, TextureSize, DeltaTime);

		// Reset material after drawing
		UIMaterialBrush->SetMaterial(nullptr);
	}
}

void FDMXPixelMappingRenderer::RenderWidget(UTextureRenderTarget2D* InRenderTarget, UUserWidget* InUserWidget) const
{
	if (InUserWidget == nullptr)
	{
		return;
	}

	if (InRenderTarget == nullptr)
	{
		return;
	}

	FVector2D TextureSize = FVector2D(InRenderTarget->SizeX, InRenderTarget->SizeY);
	static const float DeltaTime = 0.f;

	UMGRenderer->DrawWidget(InRenderTarget, InUserWidget->TakeWidget(), TextureSize, DeltaTime);
}

void FDMXPixelMappingRenderer::Render_RenderThread(FRHICommandListImmediate& RHICmdList, const FRenderContext& InContext, SurfaceReadCallback InCallback)
{
	check(IsInRenderingThread());
	check(InContext.DstTexture && InContext.DstTexture->TextureRHI);

	SCOPED_GPU_STAT(RHICmdList, DMXPixelMappingShadersStat);
	SCOPED_DRAW_EVENTF(RHICmdList, DMXPixelMappingShadersStat, DMXPixelMappingRenderer::RenderPassName);

	FRHIRenderPassInfo RPInfo(InContext.DstTexture->TextureRHI, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, DMXPixelMappingRenderer::RenderPassName);
	{
		RHICmdList.SetViewport(0.f, 0.f, 0.f, InContext.OutputTextureSize.X, InContext.OutputTextureSize.Y, 1.f);

		FFDMXPixelMappingRendererPassData PassData;
		PassData.PSParameters.InputTexture = InContext.InputTexture->TextureRHI;
		// Pixel Mapping usint SF_Trilinear for texture sampling,  
		PassData.PSParameters.InputSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		FDMXPixelMappingRendererPS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FDMXPixelBlendingQualityDimension>((EDMXPixelShaderBlendingQuality)InContext.CellBlendingQuality);
		PermutationVector.Set<FDMXVertexUVDimension>(InContext.bStaticCalculateUV);
		
		// Get shaders
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FDMXPixelMappingRendererVS> VertexShader(ShaderMap, PermutationVector);
		TShaderMapRef<FDMXPixelMappingRendererPS> PixelShader(ShaderMap, PermutationVector);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		{
			// Set the graphics pipeline state
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

			// Setup graphics pipeline
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Never>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();


			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		}

		// Set vertex shader buffer
		PassData.VSParameters.DrawRectanglePosScaleBias = FVector4(InContext.Size.X, InContext.Size.Y, InContext.Position.X, InContext.Position.Y);
		PassData.VSParameters.DrawRectangleUVScaleBias = FVector4(InContext.UVSize.X, InContext.UVSize.Y, InContext.UV.X, InContext.UV.Y);
		PassData.VSParameters.DrawRectangleInvTargetSizeAndTextureSize = FVector4(
			1.0f / InContext.TargetSize.X, 1.0f / InContext.TargetSize.Y,
			1.0f / InContext.TextureSize.X, 1.0f / InContext.TextureSize.Y);
		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassData.VSParameters);

		// Set pixel shader buffer
		PassData.PSParameters.InputTextureSize = InContext.InputTextureSize;
		PassData.PSParameters.OutputTextureSize = InContext.OutputTextureSize;
		PassData.PSParameters.PixelFactor = InContext.PixelFactor;
		PassData.PSParameters.InvertPixel = InContext.InvertPixel;
		PassData.PSParameters.UVCellSize = InContext.UVCellSize;
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassData.PSParameters);

		// Draw a two triangle on the entire viewport.
		FPixelShaderUtils::DrawFullscreenQuad(RHICmdList, 1);
	}
	RHICmdList.EndRenderPass();

	// Copies the contents of the given surface to its resolve target texture.
	RHICmdList.CopyToResolveTarget(InContext.DstTextureTargetResource->GetRenderTargetTexture(), InContext.DstTexture->TextureRHI, FResolveParams());

	// Read the contents of a texture to an output CPU buffer
	TArray<FColor> Data;
	int32 Width = InContext.DstTexture->GetSizeX();
	int32 Height = InContext.DstTexture->GetSizeY();
	FIntRect Rect(0, 0, Width, Height);
	RHICmdList.ReadSurfaceData(InContext.DstTextureTargetResource->GetRenderTargetTexture(), Rect, Data, FReadSurfaceDataFlags());

	// Fire the callback after drawing and copying texture to CPU buffer
	InCallback(Data, Rect);
}

void FDMXPixelMappingRenderer::RenderTextureToRectangle_GameThread(const FTextureResource* InTextureResource, const FTexture2DRHIRef InRenderTargetTexture, FVector2D InSize, bool bSRGBSource) const
{
	check(IsInGameThread());

	struct FRenderContext
	{
		const FTextureResource* TextureResource = nullptr;
		const FTexture2DRHIRef Texture2DRHI = nullptr;
		FVector2D ViewportSize;
		bool bSRGBSource;
	};

	FRenderContext RenderContext
	{
		InTextureResource,
		InRenderTargetTexture,
		InSize,
		bSRGBSource
	};

	ENQUEUE_RENDER_COMMAND(DMXPixelMapping_CopyToPreveiewTexture)([this, RenderContext]
	(FRHICommandListImmediate& RHICmdList)
	{
		FRHIRenderPassInfo RPInfo(RenderContext.Texture2DRHI, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("DMXPixelMapping_CopyToPreveiewTexture"));
		{
			RHICmdList.SetViewport(0.f, 0.f, 0.f, RenderContext.ViewportSize.X, RenderContext.ViewportSize.Y, 1.f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FScreenVS> VertexShader(ShaderMap);

			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			if (RenderContext.bSRGBSource)
			{
				TShaderMapRef<FScreenPSsRGBSource> PixelShader(ShaderMap);
				PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), RenderContext.TextureResource->TextureRHI);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			}
			else
			{
				TShaderMapRef<FScreenPS> PixelShader(ShaderMap);
				PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), RenderContext.TextureResource->TextureRHI);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			}
			
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			RendererModule->DrawRectangle(
				RHICmdList,
				0, 0,// X, Y Position in screen pixels of the top left corner of the quad
				RenderContext.ViewportSize.X, RenderContext.ViewportSize.Y,	// SizeX, SizeY	Size in screen pixels of the quad
				0, 0,		// U, V	Position in texels of the top left corner of the quad's UV's
				1, 1,		// SizeU, SizeV	Size in texels of the quad's UV's
				FIntPoint(RenderContext.ViewportSize.X, RenderContext.ViewportSize.Y), // TargetSizeX, TargetSizeY Size in screen pixels of the target surface
				FIntPoint(1, 1), // TextureSize Size in texels of the source texture
				VertexShader, // VertexShader The vertex shader used for rendering
				EDRF_Default); // Flags see EDrawRectangleFlags
		}
		RHICmdList.EndRenderPass();
	});
}