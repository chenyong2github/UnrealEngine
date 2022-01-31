// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "CommonRenderResources.h"
#include "ScreenRendering.h"
#include "RHIStaticStates.h"
#include "Modules/ModuleManager.h"
#include "PixelStreamingPrivate.h"

namespace UE
{
	namespace PixelStreaming
	{
		inline void CopyTexture(FTexture2DRHIRef SourceTexture, FTexture2DRHIRef DestinationTexture, FGPUFenceRHIRef& CopyFence)
		{
			FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

			IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");

			// #todo-renderpasses there's no explicit resolve here? Do we need one?
			FRHIRenderPassInfo RPInfo(DestinationTexture, ERenderTargetActions::Load_Store);

			RHICmdList.BeginRenderPass(RPInfo, TEXT("CopyBackbuffer"));

			{
				RHICmdList.SetViewport(0, 0, 0.0f, DestinationTexture->GetSizeX(), DestinationTexture->GetSizeY(), 1.0f);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

				// New engine version...
				FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
				TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
				TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

				GraphicsPSOInit.PrimitiveType = PT_TriangleList;

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

				if (DestinationTexture->GetSizeX() != SourceTexture->GetSizeX() || DestinationTexture->GetSizeY() != SourceTexture->GetSizeY())
				{
					PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), SourceTexture);
				}
				else
				{
					PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), SourceTexture);
				}

				RendererModule->DrawRectangle(RHICmdList, 0, 0, // Dest X, Y
					DestinationTexture->GetSizeX(),				// Dest Width
					DestinationTexture->GetSizeY(),				// Dest Height
					0, 0,										// Source U, V
					1, 1,										// Source USize, VSize
					DestinationTexture->GetSizeXY(),			// Target buffer size
					FIntPoint(1, 1),							// Source texture size
					VertexShader, EDRF_Default);
			}

			RHICmdList.EndRenderPass();

			RHICmdList.WriteGPUFence(CopyFence);

			RHICmdList.ImmediateFlush(EImmediateFlushType::WaitForOutstandingTasksOnly);
		}

		inline FTexture2DRHIRef CreateTexture(uint32 Width, uint32 Height)
		{
			// Create empty texture
			FRHIResourceCreateInfo CreateInfo(TEXT("BlankTexture"));

			FTexture2DRHIRef Texture;
			FString RHIName = GDynamicRHI->GetName();

			if (RHIName == TEXT("Vulkan"))
			{
				Texture = GDynamicRHI->RHICreateTexture2D(Width, Height, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_RenderTargetable | TexCreate_External, ERHIAccess::Present, CreateInfo);
			}
			else
			{
				Texture = GDynamicRHI->RHICreateTexture2D(Width, Height, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_Shared | TexCreate_RenderTargetable, ERHIAccess::CopyDest, CreateInfo);
			}
			return Texture;
		}

		inline void ReadTextureToCPU(FRHICommandListImmediate& RHICmdList, FTexture2DRHIRef& TextureRef, TArray<FColor>& OutPixels)
		{
			FIntRect Rect(0, 0, TextureRef->GetSizeX(), TextureRef->GetSizeY());
			RHICmdList.ReadSurfaceData(TextureRef, Rect, OutPixels, FReadSurfaceDataFlags());
		}

	} // namespace PixelStreaming
} // namespace UE
