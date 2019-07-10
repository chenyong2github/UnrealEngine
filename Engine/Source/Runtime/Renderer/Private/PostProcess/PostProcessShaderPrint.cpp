// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PostProcess/PostProcessShaderPrint.h"

#include "ShaderPrint.h"

bool FRCPassPostProcessShaderPrint::IsEnabled(FViewInfo const& View)
{
	return ShaderPrint::IsEnabled() && ShaderPrint::IsSupported(View);
}

void FRCPassPostProcessShaderPrint::Process(FRenderingCompositePassContext& Context)
{
	const FRenderingCompositeOutputRef* Input = GetInput(ePId_Input0);
	if (Input == nullptr)
		return;

	ShaderPrint::DrawView(Context.RHICmdList, Context.View, Input->GetOutput()->PooledRenderTarget);
	PassOutputs[0].RenderTargetDesc = Input->GetOutput()->RenderTargetDesc;
	PassOutputs[0].PooledRenderTarget = Input->GetOutput()->PooledRenderTarget;
}

FPooledRenderTargetDesc FRCPassPostProcessShaderPrint::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc Ret = GetInput(ePId_Input0)->GetOutput()->RenderTargetDesc;
	Ret.DebugName = TEXT("PostProcessShaderPrint");
	return Ret;
}
