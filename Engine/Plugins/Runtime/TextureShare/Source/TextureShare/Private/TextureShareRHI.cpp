// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareRHI.h"
#include "TextureShareLog.h"

#include "RHIStaticStates.h"

#include "RenderingThread.h"
#include "RendererPrivate.h"

#include "RenderResource.h"
#include "CommonRenderResources.h"
#include "PixelShaderUtils.h"

#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterUtils.h"

#include "ScreenRendering.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcessParameters.h"

#include "RenderTargetPool.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "d3d11.h"
#include "d3d12.h"
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
	static bool IsSizeResampleRequired(FRHITexture* SrcTexture, FRHITexture* DstTexture, const FIntRect* SrcTextureRect, const FIntRect* DstTextureRect, FIntRect& OutSrcRect, FIntRect& OutDstRect)
	{
		FIntVector SrcSizeXYZ = SrcTexture->GetSizeXYZ();
		FIntVector DstSizeXYZ = DstTexture->GetSizeXYZ();

		FIntPoint SrcSize(SrcSizeXYZ.X, SrcSizeXYZ.Y);
		FIntPoint DstSize(DstSizeXYZ.X, DstSizeXYZ.Y);

		OutSrcRect = SrcTextureRect ? (*SrcTextureRect) : (FIntRect(FIntPoint(0, 0), SrcSize));
		OutDstRect = DstTextureRect ? (*DstTextureRect) : (FIntRect(FIntPoint(0, 0), DstSize));

		if (OutSrcRect.Size() != OutDstRect.Size())
		{
			return true;
		}

		return false;
	}

	static bool GetPooledTempRTT_RenderThread(FRHICommandListImmediate& RHICmdList, FIntPoint Size, EPixelFormat Format, bool bIsRTT, TRefCountPtr<IPooledRenderTarget>& OutPooledTempRTT)
	{
		FPooledRenderTargetDesc OutputDesc(FPooledRenderTargetDesc::Create2DDesc(Size, Format, FClearValueBinding::None, TexCreate_None, bIsRTT ? TexCreate_RenderTargetable : TexCreate_ShaderResource, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, OutputDesc, OutPooledTempRTT, TEXT("TextureShare_ResampleTexture")) && OutPooledTempRTT.IsValid();
		return OutPooledTempRTT.IsValid();
	}

	static void DirectCopyTextureImpl_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* SrcTexture, FRHITexture* DstTexture, const FIntRect* SrcTextureRect, const FIntRect* DstTextureRect)
	{
		FIntRect SrcRect, DstRect;
		IsSizeResampleRequired(SrcTexture, DstTexture, SrcTextureRect, DstTextureRect, SrcRect, DstRect);

		// Copy with resolved params
		FResolveParams Params;

		Params.DestArrayIndex = 0;
		Params.SourceArrayIndex = 0;

		Params.Rect.X1 = SrcRect.Min.X;
		Params.Rect.X2 = SrcRect.Max.X;

		Params.Rect.Y1 = SrcRect.Min.Y;
		Params.Rect.Y2 = SrcRect.Max.Y;

		Params.DestRect.X1 = DstRect.Min.X;
		Params.DestRect.X2 = DstRect.Max.X;

		Params.DestRect.Y1 = DstRect.Min.Y;
		Params.DestRect.Y2 = DstRect.Max.Y;

		RHICmdList.CopyToResolveTarget(SrcTexture, DstTexture, Params);
	}

	static void ResampleCopyTextureImpl_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* SrcTexture, FRHITexture* DstTexture, const FIntRect* SrcTextureRect, const FIntRect* DstTextureRect)
	{
		FIntRect SrcRect, DstRect;
		IsSizeResampleRequired(SrcTexture, DstTexture, SrcTextureRect, DstTextureRect, SrcRect, DstRect);

		// Texture format mismatch, use a shader to do the copy.
		// #todo-renderpasses there's no explicit resolve here? Do we need one?
		FRHIRenderPassInfo RPInfo(DstTexture, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("TextureShare_ResampleTexture"));
		{
			FIntVector SrcSizeXYZ = SrcTexture->GetSizeXYZ();
			FIntVector DstSizeXYZ = DstTexture->GetSizeXYZ();

			FIntPoint SrcSize(SrcSizeXYZ.X, SrcSizeXYZ.Y);
			FIntPoint DstSize(DstSizeXYZ.X, DstSizeXYZ.Y);

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
}

DECLARE_STATS_GROUP(TEXT("TextureShare"), STATGROUP_TextureShare, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("CopyShared"), STAT_TextureShare_CopyShared, STATGROUP_TextureShare);
DECLARE_CYCLE_STAT(TEXT("ResampleTempRTT"), STAT_TextureShare_ResampleTempRTT, STATGROUP_TextureShare);

/*
 * FTextureShareRHI
 */
bool FTextureShareRHI::WriteToShareTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* SrcTexture, FRHITexture* DstShareTexture, const FIntRect* SrcTextureRect, bool bIsFormatResampleRequired)
{
	FIntRect SrcRect, DstRect;
	bool bResampleRequired = IsSizeResampleRequired(SrcTexture, DstShareTexture, SrcTextureRect, nullptr, SrcRect, DstRect) || bIsFormatResampleRequired;
	if (!bResampleRequired)
	{
		// Copy direct to shared texture
		SCOPE_CYCLE_COUNTER(STAT_TextureShare_CopyShared);
		DirectCopyTextureImpl_RenderThread(RHICmdList, SrcTexture, DstShareTexture, SrcTextureRect, nullptr);

		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
		return true;
	}
	else
	{
		// Resample size and format and send
		EPixelFormat DstFormat = DstShareTexture->GetFormat();
		TRefCountPtr<IPooledRenderTarget> ResampledRTT;
		if (GetPooledTempRTT_RenderThread(RHICmdList, DstRect.Size(), DstFormat, true, ResampledRTT))
		{
			FTexture2DRHIRef RHIResampledRTT = (const FTexture2DRHIRef&)ResampledRTT->GetRenderTargetItem().TargetableTexture;
			if (RHIResampledRTT.IsValid())
			{
				// Resample source texture to PooledTempRTT (Src texture now always shader resource)
				SCOPE_CYCLE_COUNTER(STAT_TextureShare_ResampleTempRTT);
				ResampleCopyTextureImpl_RenderThread(RHICmdList, SrcTexture, RHIResampledRTT, SrcTextureRect, nullptr);

				// Copy PooledTempRTT to shared texture surface
				SCOPE_CYCLE_COUNTER(STAT_TextureShare_CopyShared);
				DirectCopyTextureImpl_RenderThread(RHICmdList, RHIResampledRTT, DstShareTexture, nullptr, nullptr);

				RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
				return true;
			}
		}
	}
	return false;
}

bool FTextureShareRHI::ReadFromShareTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* SrcShareTexture, FRHITexture* DstTexture, const FIntRect* DstTextureRect, bool bIsFormatResampleRequired)
{
	FIntRect SrcRect, DstRect;
	bool bResampleRequired = IsSizeResampleRequired(SrcShareTexture, DstTexture, nullptr, DstTextureRect, SrcRect, DstRect) || bIsFormatResampleRequired;
	if (!bResampleRequired)
	{
		// Copy direct from shared texture
		SCOPE_CYCLE_COUNTER(STAT_TextureShare_CopyShared);
		DirectCopyTextureImpl_RenderThread(RHICmdList, SrcShareTexture, DstTexture, nullptr, DstTextureRect);

		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
		return true;
	}
	else
	{
		// Receive, then resample size and format
		EPixelFormat SrcFormat = SrcShareTexture->GetFormat();
		EPixelFormat DstFormat = DstTexture->GetFormat();
		TRefCountPtr<IPooledRenderTarget> ReceivedSRV, ResampledRTT;
		if (GetPooledTempRTT_RenderThread(RHICmdList, SrcRect.Size(), SrcFormat, false, ReceivedSRV) && GetPooledTempRTT_RenderThread(RHICmdList, DstRect.Size(), DstFormat, true, ResampledRTT))
		{
			FTexture2DRHIRef RHIReceivedSRV  = (const FTexture2DRHIRef&)ReceivedSRV->GetRenderTargetItem().ShaderResourceTexture;
			FTexture2DRHIRef RHIResampledRTT = (const FTexture2DRHIRef&)ResampledRTT->GetRenderTargetItem().TargetableTexture;
			if (RHIReceivedSRV.IsValid() && RHIResampledRTT.IsValid())
			{
				// Copy direct from shared texture to RHIReceivedSRV (received shared texture has only flag TexCreate_ResolveTargetable, not shader resource)
				SCOPE_CYCLE_COUNTER(STAT_TextureShare_CopyShared);
				DirectCopyTextureImpl_RenderThread(RHICmdList, SrcShareTexture, RHIReceivedSRV, nullptr, nullptr);

				// Resample RHIReceivedSRV to RHIResampledRTT
				SCOPE_CYCLE_COUNTER(STAT_TextureShare_ResampleTempRTT);
				ResampleCopyTextureImpl_RenderThread(RHICmdList, RHIReceivedSRV, RHIResampledRTT, nullptr, nullptr);

				// Copy RHIResampledRTT to Destination
				DirectCopyTextureImpl_RenderThread(RHICmdList, RHIResampledRTT, DstTexture, nullptr, DstTextureRect);

				RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
				return true;
			}
		}
	}
	return false;
}
