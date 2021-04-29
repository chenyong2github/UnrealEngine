// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterShadersGenerateMips.h"

#include "RHIResources.h"
#include "GenerateMips.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

bool FDisplayClusterShadersGenerateMips::GenerateMips(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutMipsTexture, const FDisplayClusterShaderParameters_GenerateMips& InSettings)
{
	check(IsInRenderingThread());

	if (InSettings.IsEnabled())
	{
		check(InOutMipsTexture);

		//const EGenerateMipsPass GenerateMipsPass = EGenerateMipsPass::Compute;
		FGenerateMipsParams GenerateMipsParams{ InSettings.MipsSamplerFilter == TF_Nearest ? SF_Point : (InSettings.MipsSamplerFilter == TF_Trilinear ? SF_Trilinear : SF_Bilinear),
			InSettings.MipsAddressU == TA_Wrap ? AM_Wrap : (InSettings.MipsAddressU == TA_Mirror ? AM_Mirror : AM_Clamp),
			InSettings.MipsAddressV == TA_Wrap ? AM_Wrap : (InSettings.MipsAddressV == TA_Mirror ? AM_Mirror : AM_Clamp) };

		check(&RHICmdList == &FRHICommandListExecutor::GetImmediateCommandList());
		
		// Required for memory stack on this thread
		FMemMark MemMark(FMemStack::Get());

		FRDGBuilder GraphBuilder(RHICmdList);
		TRefCountPtr<IPooledRenderTarget> PoolRenderTarget = CreateRenderTarget(InOutMipsTexture, TEXT("nDisplayViewportMips"));
		FRDGTextureRef MipOutputTexture = GraphBuilder.RegisterExternalTexture(PoolRenderTarget);

		FGenerateMips::Execute(GraphBuilder, MipOutputTexture, GenerateMipsParams);

		GraphBuilder.Execute();

		return true;
	}

	return false;
}
