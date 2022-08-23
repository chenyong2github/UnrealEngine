// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "RHI.h"
#include "Runtime/Renderer/Private/ScreenPass.h"
#include "ScreenRendering.h"

/*
    * Copy from one texture to another.
    * Assumes SourceTexture is in ERHIAccess::CopySrc and DestTexture is in ERHIAccess::CopyDest
    * Fence can be nullptr if no fence is to be used.
    */
inline void CopyTexture(FRHICommandList& RHICmdList, FTextureRHIRef SourceTexture, FTextureRHIRef DestTexture, FRHIGPUFence* Fence)
{
    if (SourceTexture->GetDesc().Format == DestTexture->GetDesc().Format
        && SourceTexture->GetDesc().Extent.X == DestTexture->GetDesc().Extent.X
        && SourceTexture->GetDesc().Extent.Y == DestTexture->GetDesc().Extent.Y)
    {

        RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::Unknown, ERHIAccess::CopySrc));
        RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::CopyDest));

        // source and dest are the same. simple copy
        RHICmdList.CopyTexture(SourceTexture, DestTexture, {});
    }
    else
    {
        IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");

        RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::Unknown, ERHIAccess::SRVMask));
        RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::RTV));

        // source and destination are different. rendered copy
        FRHIRenderPassInfo RPInfo(DestTexture, ERenderTargetActions::Load_Store);
        RHICmdList.BeginRenderPass(RPInfo, TEXT("PixelCapture::CopyTexture"));
        {
            FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
            TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
            TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

            RHICmdList.SetViewport(0, 0, 0.0f, DestTexture->GetDesc().Extent.X, DestTexture->GetDesc().Extent.Y, 1.0f);

            FGraphicsPipelineStateInitializer GraphicsPSOInit;
            RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
            GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
            GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
            GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
            GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
            GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
            GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
            GraphicsPSOInit.PrimitiveType = PT_TriangleList;
            SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

            PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), SourceTexture);

            FIntPoint TargetBufferSize(DestTexture->GetDesc().Extent.X, DestTexture->GetDesc().Extent.Y);
            RendererModule->DrawRectangle(RHICmdList, 0, 0, // Dest X, Y
                DestTexture->GetDesc().Extent.X,			// Dest Width
                DestTexture->GetDesc().Extent.Y,			// Dest Height
                0, 0,										// Source U, V
                1, 1,										// Source USize, VSize
                TargetBufferSize,							// Target buffer size
                FIntPoint(1, 1),							// Source texture size
                VertexShader, EDRF_Default);
        }

        RHICmdList.EndRenderPass();

        RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::SRVMask, ERHIAccess::CopySrc));
        RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::RTV, ERHIAccess::CopyDest));
    }

    if (Fence != nullptr)
    {
        RHICmdList.WriteGPUFence(Fence);
    }
}