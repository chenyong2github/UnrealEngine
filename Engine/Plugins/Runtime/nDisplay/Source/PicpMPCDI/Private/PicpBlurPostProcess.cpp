// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PicpBlurPostProcess.h"

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"

#include "CommonRenderResources.h"
#include "PixelShaderUtils.h"

#include "ShaderParameterUtils.h"

#include "Engine/TextureRenderTarget2D.h"

static int CVarPicpPostProcess_Value = 1;
static FAutoConsoleVariableRef CVarPicpPostProcess(
	TEXT("nDisplay.render.picp.EnablePostProcess"),
	CVarPicpPostProcess_Value,
	TEXT("Enable postprocess shaders for picp\n(0 - Disable)\n")
);

#define PostProcessShaderFileName TEXT("/Plugin/nDisplay/Private/PicpPostProcessShaders.usf")

IMPLEMENT_SHADER_TYPE(, FDirectProjectionVS, PostProcessShaderFileName, TEXT("DirectProjectionVS"), SF_Vertex);


IMPLEMENT_SHADER_TYPE(template<>, FPicpBlurPostProcessDefaultPS, PostProcessShaderFileName, TEXT("BlurPostProcessPS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FPicpBlurPostProcessDilatePS,  PostProcessShaderFileName, TEXT("BlurPostProcessPS"), SF_Pixel);

IMPLEMENT_SHADER_TYPE(, FDirectComposePS, PostProcessShaderFileName, TEXT("DirectComposePS"), SF_Pixel);

void FPicpBlurPostProcess::ApplyCompose_RenderThread(
	FRHICommandListImmediate& RHICmdList, 
	FRHITexture* OverlayTexture,
	FRHITexture2D* DstRenderTarget,
	FRHITexture2D* CopyTexture
)
{
	check(IsInRenderingThread());

	if (nullptr != OverlayTexture && OverlayTexture->IsValid())
	{
		// Apply Overlay if input defined
		FRHIRenderPassInfo RPInfo(DstRenderTarget, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("nDisplayPicpPostProcessCompose"));
		{
			const ERHIFeatureLevel::Type RenderFeatureLevel = GMaxRHIFeatureLevel;//! GWorld->Scene->GetFeatureLevel();
			const auto GlobalShaderMap = GetGlobalShaderMap(RenderFeatureLevel);

			TShaderMapRef<FDirectProjectionVS> VertexShader(GlobalShaderMap);
			TShaderMapRef<FDirectComposePS> PixelShader(GlobalShaderMap);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			PixelShader->SetParameters(RHICmdList, OverlayTexture);
			FPixelShaderUtils::DrawFullscreenQuad(RHICmdList, 1);
		}
		RHICmdList.EndRenderPass();
	}

	if (CopyTexture && CopyTexture->IsValid())
	{
		// Copy RTT to Copy texture
		RHICmdList.CopyToResolveTarget(DstRenderTarget, CopyTexture, FResolveParams());
	}
}


template<uint32 ShaderType>
static void PicpBlurPostProcess_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FRHITexture2D* InOutTexture,
	FRHITexture2D* TempTexture,
	int KernelRadius,
	float KernelScale
)
{
	check(IsInRenderingThread());

	const ERHIFeatureLevel::Type RenderFeatureLevel = GMaxRHIFeatureLevel;//! GWorld->Scene->GetFeatureLevel();
	const auto GlobalShaderMap = GetGlobalShaderMap(RenderFeatureLevel);

	TShaderMapRef<FDirectProjectionVS>                  VertexShader(GlobalShaderMap);
	TShaderMapRef<TPicpBlurPostProcessPS<ShaderType>> PixelShader(GlobalShaderMap);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	{
		const FIntPoint TargetSizeXY = InOutTexture->GetSizeXY();

		// Blur X
		FRHIRenderPassInfo RPInfo1(TempTexture, ERenderTargetActions::DontLoad_Store);
		RHICmdList.BeginRenderPass(RPInfo1, TEXT("nDisplayPicpPostProcessBlurPassX"));
		{
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			PixelShader->SetParameters(RHICmdList, InOutTexture, FVector2D(KernelScale / TargetSizeXY.X, 0.0f), KernelRadius);
			FPixelShaderUtils::DrawFullscreenQuad(RHICmdList, 1);
		}
		RHICmdList.EndRenderPass();

		// Blur Y
		FRHIRenderPassInfo RPInfo2(InOutTexture, ERenderTargetActions::DontLoad_Store);
		RHICmdList.BeginRenderPass(RPInfo2, TEXT("nDisplayPicpPostProcessBlurPassY"));
		{
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			PixelShader->SetParameters(RHICmdList, TempTexture, FVector2D(0.0f, KernelScale / TargetSizeXY.Y), KernelRadius);
			FPixelShaderUtils::DrawFullscreenQuad(RHICmdList, 1);
		}
		RHICmdList.EndRenderPass();
	}	
}

void FPicpBlurPostProcess::ApplyBlur_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutRT, FRHITexture2D* TempRT, int KernelRadius, float KernelScale, EPicpBlurPostProcessShaderType BlurType)
{
	switch (BlurType)
	{
	case EPicpBlurPostProcessShaderType::Gaussian:
		PicpBlurPostProcess_RenderThread<(int)EPicpBlurPostProcessShaderType::Gaussian>(RHICmdList, InOutRT, TempRT, KernelRadius, KernelScale);
		break;

	case EPicpBlurPostProcessShaderType::Dilate:
		PicpBlurPostProcess_RenderThread<(int)EPicpBlurPostProcessShaderType::Dilate>(RHICmdList, InOutRT, TempRT, KernelRadius, KernelScale);
		break;
	}
}


// BP gamethread api:
void FPicpBlurPostProcess::ApplyBlur(UTextureRenderTarget2D* InOutRenderTarget, UTextureRenderTarget2D* TemporaryRenderTarget, int KernelRadius, float KernelScale, EPicpBlurPostProcessShaderType BlurType)
{	
	if (!InOutRenderTarget || !TemporaryRenderTarget || (CVarPicpPostProcess_Value == 0))
	{
		return;
	}

	FTextureRenderTargetResource* InOutRT = InOutRenderTarget->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* TempRT  = TemporaryRenderTarget->GameThread_GetRenderTargetResource();

	ENQUEUE_RENDER_COMMAND(CaptureCommand)(
		[InOutRT, TempRT, KernelRadius, KernelScale, BlurType](FRHICommandListImmediate& RHICmdList)
		{
		FRHITexture2D* RenderTarget = InOutRT->GetRenderTargetTexture()->GetTexture2D();
		FRHITexture2D* TempTexture = TempRT->GetRenderTargetTexture()->GetTexture2D();
			if (nullptr==RenderTarget || nullptr==TempTexture)
			{
				//@todo handle error
				return;
			}
			if (RenderTarget->IsValid() && TempTexture->IsValid())
			{
				ApplyBlur_RenderThread(RHICmdList, RenderTarget, TempTexture, KernelRadius, KernelScale, BlurType);
			}
		}
	);
}

void FPicpBlurPostProcess::ApplyCompose(UTexture* InputTexture, UTextureRenderTarget2D* OutputRenderTarget, UTextureRenderTarget2D* Result)
{
	if (!OutputRenderTarget || (CVarPicpPostProcess_Value == 0))
	{
		return;
	}
	FTextureResource* OverlayTextureResource = InputTexture ? InputTexture->Resource : nullptr;
	FTextureRenderTargetResource* TextureRenderTarget = OutputRenderTarget->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* TextureRenderResult = Result->GameThread_GetRenderTargetResource();

	ENQUEUE_RENDER_COMMAND(CaptureCommand)(
		[OverlayTextureResource, TextureRenderTarget, TextureRenderResult](FRHICommandListImmediate& RHICmdList)
	{
		FRHITexture2D* RTTexture = TextureRenderTarget ? TextureRenderTarget->GetRenderTargetTexture()->GetTexture2D(): nullptr;
		FRHITexture2D* ResultTexture = TextureRenderResult ? TextureRenderResult->GetRenderTargetTexture()->GetTexture2D() : nullptr;
		FRHITexture*   OverlayTexture = OverlayTextureResource ? OverlayTextureResource->TextureRHI : nullptr;;
		
		if (nullptr == RTTexture)
		{
			//@todo handle error
			return;
		}

		ApplyCompose_RenderThread(RHICmdList, OverlayTexture, RTTexture, ResultTexture);

	}
	);
}


