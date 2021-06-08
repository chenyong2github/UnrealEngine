// Copyright Epic Games Tools LLC
//   Licenced under the Unreal Engine EULA 

#include "BinkFunctionLibrary.h"
#include "BinkMediaPlayerPCH.h"

#include "Misc/Paths.h"
#include "Rendering/RenderingCommon.h"
#include "RenderingThread.h"
#include "OneColorShader.h"

#include "Slate/SlateTextures.h"
#include "Slate/SceneViewport.h"

#include "binkplugin.h"

extern TSharedPtr<FBinkMovieStreamer, ESPMode::ThreadSafe> MovieStreamer;

// Note: Has to be in a seperate function because you can't do #if inside a render command macro
static void Bink_DrawOverlays_Internal(FRHICommandListImmediate &RHICmdList, const UGameViewportClient* gameViewport) {
	if (!GEngine || !gameViewport || !gameViewport->Viewport) {
		return;
	}

	FVector2D screenSize;
	gameViewport->GetViewportSize(screenSize);
	const FTexture2DRHIRef &backbuffer = gameViewport->Viewport->GetRenderTargetTexture();
	if(!backbuffer.GetReference()) 
	{
		return;
	}

	RHICmdList.SubmitCommandsHint();

	FRHIRenderPassInfo RPInfo(backbuffer, ERenderTargetActions::Load_Store);
	TransitionRenderPassTargets(RHICmdList, RPInfo);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("RenderBink"));
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	auto* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<TOneColorVS<true>> VertexShader(ShaderMap);
	TShaderMapRef<FOneColorPS> PixelShader(ShaderMap);
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
	RHICmdList.SetViewport(0, 0, 0, screenSize.X, screenSize.Y, 1);

	RHICmdList.EnqueueLambda([backbuffer,screenSize](FRHICommandListImmediate& RHICmdList) {
		BINKPLUGINFRAMEINFO FrameInfo = {};
		FrameInfo.screen_resource = backbuffer->GetNativeResource();
		FrameInfo.screen_resource_state = 4; // D3D12_RESOURCE_STATE_RENDER_TARGET; (only used in d3d12)
		FrameInfo.width = screenSize.X;
		FrameInfo.height = screenSize.Y;
		FrameInfo.sdr_or_hdr = backbuffer->GetFormat() == PF_A2B10G10R10 ? 1 : 0;
		FrameInfo.cmdBuf = RHICmdList.GetNativeCommandBuffer();
		BinkPluginSetPerFrameInfo(&FrameInfo);
		BinkPluginAllScheduled();
		BinkPluginDraw(0, 1);
	});
	RHICmdList.PostExternalCommandsReset();
	RHICmdList.EndRenderPass();
	RHICmdList.SubmitCommandsHint();
}

void UBinkFunctionLibrary::Bink_DrawOverlays() 
{
	TWeakObjectPtr<UGameViewportClient> gameViewport = (GEngine != nullptr) ? GEngine->GameViewport : nullptr;
	if (!gameViewport.IsValid()) {
		return;
	}

	ENQUEUE_RENDER_COMMAND(BinkOverlays)([gameViewport](FRHICommandListImmediate& RHICmdList) 
	{ 
		if (gameViewport.IsValid())
		{
			RHICmdList.PostExternalCommandsReset();
			Bink_DrawOverlays_Internal(RHICmdList, gameViewport.Get());
		}
	});
}

FTimespan UBinkFunctionLibrary::BinkLoadingMovie_GetDuration() 
{
	double ms = 0;
	if(MovieStreamer.IsValid() && MovieStreamer.Get()->bnk) 
	{
		BINKPLUGININFO bpinfo = {};
		BinkPluginInfo(MovieStreamer.Get()->bnk, &bpinfo);
		ms = ((double)bpinfo.Frames) * ((double)bpinfo.FrameRateDiv) * 1000.0 / ((double)bpinfo.FrameRate);
	}
	return FTimespan::FromMilliseconds(ms);
}

FTimespan UBinkFunctionLibrary::BinkLoadingMovie_GetTime() 
{
	double ms = 0;
	if(MovieStreamer.IsValid() && MovieStreamer.Get()->bnk) 
	{
		BINKPLUGININFO bpinfo = {};
		BinkPluginInfo(MovieStreamer.Get()->bnk, &bpinfo);
		ms = ((double)bpinfo.FrameNum) * ((double)bpinfo.FrameRateDiv) * 1000.0 / ((double)bpinfo.FrameRate);
	}
	return FTimespan::FromMilliseconds(ms);
}
