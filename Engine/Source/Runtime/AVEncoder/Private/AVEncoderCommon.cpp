// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AVEncoderCommon.h"
#include "Modules/ModuleManager.hh"

DEFINE_LOG_CATEGORY(LogAVEncoder);
CSV_DECLARE_CATEGORY_EXTERN(AVEncoder);
CSV_DEFINE_CATEGORY(AVEncoder, true);

namespace AVEncoder
{

bool ReadH264Setting(const FString& Name, const FString& Value, FH264Settings& OutSettings)
{
	if (Name == TEXT("qp"))
	{
		OutSettings.QP =  FMath::Clamp(FCString::Atoi(*Value), 0, 51);
	}
	else if (Name == TEXT("ratecontrolmode"))
	{
		if (Value == TEXT("constqp"))
		{
			OutSettings.RcMode = FH264Settings::ConstQP;
		}
		else if (Value == TEXT("vbr"))
		{
			OutSettings.RcMode = FH264Settings::VBR;
		}
		else if (Value == TEXT("cbr"))
		{
			OutSettings.RcMode = FH264Settings::CBR;
		}
		else
		{
			UE_LOG(LogAVEncoder, Error, TEXT("Option '%s' has an invalid value ('%s')"), *Name, *Value);
			return false;
		}
	}
	else
	{
		return false;
	}

	return true;
}

void ReadH264Settings(const TArray<TPair<FString, FString>>& Options, FH264Settings& OutSettings)
{
	for (const TPair<FString, FString>& Opt : Options)
	{
		ReadH264Setting(Opt.Key, Opt.Value, OutSettings);
	}
}

void CopyTextureImpl(const FTexture2DRHIRef& Src, FTexture2DRHIRef& Dst, FRHIGPUFence* GPUFence)
{
	IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	if (GPUFence)
	{
		GPUFence->Clear();
	}

	if (Src->GetFormat() == Dst->GetFormat() &&
		Src->GetSizeXY() == Dst->GetSizeXY())
	{
		RHICmdList.CopyToResolveTarget(Src, Dst, FResolveParams{});
	}
	else // Texture format mismatch, use a shader to do the copy.
	{
		// #todo-renderpasses there's no explicit resolve here? Do we need one?
		FRHIRenderPassInfo RPInfo(Dst, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("CopyBackbuffer"));
		{
			RHICmdList.SetViewport(0, 0, 0.0f, Dst->GetSizeX(), Dst->GetSizeY(), 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
			TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			if (Dst->GetSizeX() != Src->GetSizeX() || Dst->GetSizeY() != Src->GetSizeY())
			{
				PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), Src);
			}
			else
			{
				PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), Src);
			}

			RendererModule->DrawRectangle(
				RHICmdList,
				0, 0,						// Dest X, Y
				Dst->GetSizeX(),			// Dest Width
				Dst->GetSizeY(),			// Dest Height
				0, 0,						// Source U, V
				1, 1,						// Source USize, VSize
				Dst->GetSizeXY(),			// Target buffer size
				FIntPoint(1, 1),			// Source texture size
				*VertexShader,
				EDRF_Default);
		}
		RHICmdList.EndRenderPass();

		if (GPUFence)
		{
			RHICmdList.WriteGPUFence(GPUFence);
		}
	}
}


}
