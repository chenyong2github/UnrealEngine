// Copyright Epic Games, Inc. All Rights Reserved.


#include "ResolveShader.h"
#include "ShaderParameterUtils.h"

IMPLEMENT_SHADER_TYPE(, FResolveDepthPS, TEXT("/Engine/Private/ResolvePixelShader.usf"), TEXT("MainDepth"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FResolveDepth2XPS, TEXT("/Engine/Private/ResolvePixelShader.usf"), TEXT("MainDepth"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FResolveDepth4XPS, TEXT("/Engine/Private/ResolvePixelShader.usf"), TEXT("MainDepth"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FResolveDepth8XPS, TEXT("/Engine/Private/ResolvePixelShader.usf"), TEXT("MainDepth"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FResolveDepthNonMSPS, TEXT("/Engine/Private/ResolvePixelShader.usf"), TEXT("MainDepthNonMS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FResolveSingleSamplePS, TEXT("/Engine/Private/ResolvePixelShader.usf"), TEXT("MainSingleSample"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FResolveVS, TEXT("/Engine/Private/ResolveVertexShader.usf"), TEXT("Main"), SF_Vertex);

void FResolveSingleSamplePS::SetParameters(FRHICommandList& RHICmdList, uint32 SingleSampleIndexValue)
{
	SetShaderValue(RHICmdList, GetPixelShader(),SingleSampleIndex,SingleSampleIndexValue);
}

void FResolveVS::SetParameters(FRHICommandList& RHICmdList, const FResolveRect& SrcBounds, const FResolveRect& DstBounds, uint32 DstSurfaceWidth, uint32 DstSurfaceHeight)
{
	// Generate the vertices used to copy from the source surface to the destination surface.
	const float MinU = SrcBounds.X1;
	const float MinV = SrcBounds.Y1;
	const float MaxU = SrcBounds.X2;
	const float MaxV = SrcBounds.Y2;
	const float MinX = -1.f + DstBounds.X1 / ((float)DstSurfaceWidth * 0.5f);
	const float MinY = +1.f - DstBounds.Y1 / ((float)DstSurfaceHeight * 0.5f);
	const float MaxX = -1.f + DstBounds.X2 / ((float)DstSurfaceWidth * 0.5f);
	const float MaxY = +1.f - DstBounds.Y2 / ((float)DstSurfaceHeight * 0.5f);

	SetShaderValue(RHICmdList, GetVertexShader(), PositionMinMax, FVector4(MinX, MinY, MaxX, MaxY));
	SetShaderValue(RHICmdList, GetVertexShader(), UVMinMax, FVector4(MinU, MinV, MaxU, MaxV));
}
