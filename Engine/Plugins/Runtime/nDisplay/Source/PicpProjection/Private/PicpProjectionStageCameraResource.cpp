// Copyright Epic Games, Inc. All Rights Reserved.
#include "PicpProjectionStageCameraResource.h"

#include "PicpProjectionLog.h"

#include "GenerateMips.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

#include "RenderResource.h"
#include "CommonRenderResources.h"
#include "PixelShaderUtils.h"

#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterUtils.h"

#include "ScreenRendering.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcessParameters.h"

static TAutoConsoleVariable<int32> CVarStageCameraMaxNumMips(
	TEXT("nDisplay.render.picp.StageCamera.MaxNumMips"),
	32,
	TEXT("Force max num mips level for stage cameras textures\n")
	TEXT(" 0: disabled mips generation\n")
	TEXT(" N: Max num mips value\n"),
	ECVF_RenderThreadSafe
);

void FPicpProjectionStageCameraResource::InitializeStageCameraRTT(FRHICommandListImmediate& RHICmdList, const FCameraData& InStageCameraInfo)
{
	uint32 MaxNumMips = CVarStageCameraMaxNumMips.GetValueOnAnyThread();

	uint32 InNumMips = FMath::Min(InStageCameraInfo.NumMips, MaxNumMips);
	FIntPoint InDim = InStageCameraInfo.ViewportRect.Size();

	bool bSRGB = InStageCameraInfo.bSRGB;

	// create output render target if necessary
	ETextureCreateFlags OutputCreateFlags = TexCreate_Dynamic | (bSRGB ? TexCreate_SRGB : TexCreate_None);
	if (InNumMips > 1)
	{
		// Make sure can have mips & the mip generator has what it needs to work
		OutputCreateFlags |= (TexCreate_GenerateMipCapable | TexCreate_UAV);

		// Make sure we only set a number of mips that actually makes sense, given the sample size
		uint32 MaxMips = FGenericPlatformMath::FloorToInt(FGenericPlatformMath::Log2(FGenericPlatformMath::Min(InDim.X, InDim.Y)));
		InNumMips = FMath::Min(InNumMips, MaxMips);
	}
	else
	{
		InNumMips = 1;
	}

	bool bTextureChanged = !CameraRenderTarget.IsValid() || (CameraRenderTarget->GetSizeXY() != InStageCameraInfo.ViewportRect.Size()) || (CameraRenderTarget->GetFormat() != InStageCameraInfo.Format) || ((CameraRenderTarget->GetFlags() & OutputCreateFlags) != OutputCreateFlags) || CurrentNumMips != InNumMips;
	CameraData = InStageCameraInfo;

	if (bTextureChanged)
	{
		TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;

		MipGenerationCache.SafeRelease();

		FRHIResourceCreateInfo CreateInfo = {
			FClearValueBinding::None
		};

		RHICreateTargetableShaderResource2D(
			InDim.X,
			InDim.Y,
			CameraData.Format,
			InNumMips,
			OutputCreateFlags,
			TexCreate_RenderTargetable,
			false,
			CreateInfo,
			CameraRenderTarget,
			DummyTexture2DRHI
		);

		CameraRenderTarget->SetName(TEXT("PicpStageCameraTextureResourceOutput"));

		CurrentNumMips = InNumMips;

		bResourceUpdated = false;
	}
}

void FPicpProjectionStageCameraResource::DiscardStageCameraRTT()
{
	bResourceUpdated = false;
}

bool FPicpProjectionStageCameraResource::UpdateStageCameraRTT(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture)
{
	if (!CameraRenderTarget.IsValid())
	{
		return false;
	}

	if (!bResourceUpdated)
	{
		// Call once
		bResourceUpdated = true;

		if (CameraData.CustomCameraTexture)
		{
			// Copy debug texture to SrcTexture viewport
			ResampleCopyTextureImpl_RenderThread(RHICmdList, CameraData.CustomCameraTexture, SrcTexture, nullptr, &CameraData.ViewportRect);
		}

		// Copy first Mip from SrcTexture to CameraRTT (other mips is black)
		{
			FRHICopyTextureInfo CopyInfo;
			FIntPoint Size = CameraData.ViewportRect.Size();
			CopyInfo.Size = FIntVector(Size.X, Size.Y, 1);
			CopyInfo.SourcePosition.X = CameraData.ViewportRect.Min.X;
			CopyInfo.SourcePosition.Y = CameraData.ViewportRect.Min.Y;

			RHICmdList.CopyTexture(SrcTexture, CameraRenderTarget, CopyInfo);
		}

		if (CurrentNumMips > 1)
		{
			check(CameraRenderTarget);

			const EGenerateMipsPass GenerateMipsPass = EGenerateMipsPass::Compute;
			FRHISamplerState* TrilinearClampSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			FRDGBuilder GraphBuilder(RHICmdList);
			{
				FRDGTextureRef MipOutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(CameraRenderTarget, TEXT("MipGenerationInput")));
				FGenerateMips::Execute(GraphBuilder, MipOutputTexture, TrilinearClampSampler, GenerateMipsPass);
			}
			GraphBuilder.Execute();
		}
	}

	return true;
}

void FPicpProjectionStageCameraResource::ResampleCopyTextureImpl_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* SrcTexture, FRHITexture* DstTexture, const FIntRect* SrcTextureRect, const FIntRect* DstTextureRect)
{
	FIntVector SrcSizeXYZ = SrcTexture->GetSizeXYZ();
	FIntVector DstSizeXYZ = DstTexture->GetSizeXYZ();

	FIntPoint SrcSize(SrcSizeXYZ.X, SrcSizeXYZ.Y);
	FIntPoint DstSize(DstSizeXYZ.X, DstSizeXYZ.Y);

	FIntRect SrcRect = SrcTextureRect ? (*SrcTextureRect) : (FIntRect(FIntPoint(0, 0), SrcSize));
	FIntRect DstRect = DstTextureRect ? (*DstTextureRect) : (FIntRect(FIntPoint(0, 0), DstSize));

	// Texture format mismatch, use a shader to do the copy.
	// #todo-renderpasses there's no explicit resolve here? Do we need one?
	FRHIRenderPassInfo RPInfo(DstTexture, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("PicpStageCamera_ResampleTexture"));
	{
		RHICmdList.SetViewport(0.f, 0.f, 0.0f, DstSize.X, DstSize.Y, 1.0f);

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

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		if (SrcRect.Size() != DstRect.Size())
		{
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), SrcTexture);
		}
		else
		{
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), SrcTexture);
		}

		// Set up vertex uniform parameters for scaling and biasing the rectangle.
		// Note: Use DrawRectangle in the vertex shader to calculate the correct vertex position and uv.
		FDrawRectangleParameters Parameters;
		{
			Parameters.PosScaleBias = FVector4(DstRect.Size().X, DstRect.Size().Y, DstRect.Min.X, DstRect.Min.Y);
			Parameters.UVScaleBias = FVector4(SrcRect.Size().X, SrcRect.Size().Y, SrcRect.Min.X, SrcRect.Min.Y);
			Parameters.InvTargetSizeAndTextureSize = FVector4(1.0f / DstSize.X, 1.0f / DstSize.Y, 1.0f / SrcSize.X, 1.0f / SrcSize.Y);

			SetUniformBufferParameterImmediate(RHICmdList, VertexShader.GetVertexShader(), VertexShader->GetUniformBufferParameter<FDrawRectangleParameters>(), Parameters);
		}

		FPixelShaderUtils::DrawFullscreenQuad(RHICmdList, 1);
	}
	RHICmdList.EndRenderPass();
}

