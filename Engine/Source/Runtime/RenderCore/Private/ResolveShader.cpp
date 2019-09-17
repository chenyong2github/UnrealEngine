// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "ResolveShader.h"
#include "ShaderParameterUtils.h"

IMPLEMENT_SHADER_TYPE(, FResolveDepthPS, TEXT("/Engine/Private/ResolvePixelShader.usf"), TEXT("MainDepth"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FResolveDepth2XPS, TEXT("/Engine/Private/ResolvePixelShader.usf"), TEXT("MainDepth"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FResolveDepth4XPS, TEXT("/Engine/Private/ResolvePixelShader.usf"), TEXT("MainDepth"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FResolveDepth8XPS, TEXT("/Engine/Private/ResolvePixelShader.usf"), TEXT("MainDepth"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FResolveDepthNonMSPS, TEXT("/Engine/Private/ResolvePixelShader.usf"), TEXT("MainDepthNonMS"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FResolveSingleSamplePS, TEXT("/Engine/Private/ResolvePixelShader.usf"), TEXT("MainSingleSample"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(, FResolveVS, TEXT("/Engine/Private/ResolveVertexShader.usf"), TEXT("Main"), SF_Vertex);

RENDERCORE_API TGlobalResource<FResolveVertexBuffer> GResolveVertexBuffer;

void FResolveSingleSamplePS::SetParameters(FRHICommandList& RHICmdList, uint32 SingleSampleIndexValue)
{
	SetShaderValue(RHICmdList, GetPixelShader(),SingleSampleIndex,SingleSampleIndexValue);
}

void FResolveVS::SetParameters(FRHICommandList& RHICmdList, FVector4 InPositionMinMax, FVector4 InUVMinMax)
{
	SetShaderValue(RHICmdList, GetVertexShader(), PositionMinMax, InPositionMinMax);
	SetShaderValue(RHICmdList, GetVertexShader(), UVMinMax, InUVMinMax);
}

void FResolveVertexBuffer::InitDynamicRHI()
{
	FVertexDeclarationElementList Elements;
	Elements.Add(FVertexElement(0, 0, VET_Float2, 0, sizeof(FVector2D)));
	VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);

	void* VoidPtr;
	FRHIResourceCreateInfo CreateInfo;
	VertexBufferRHI = RHICreateAndLockVertexBuffer(sizeof(FVector2D) * 4, BUF_Static, CreateInfo, VoidPtr);
	// Generate the vertices used
	FVector2D* Vertices = (FVector2D*)VoidPtr;
	Vertices[0] = { 0.0f, 0.0f };
	Vertices[1] = { 1.0f, 0.0f };
	Vertices[2] = { 0.0f, 1.0f };
	Vertices[3] = { 1.0f, 1.0f };
	RHIUnlockVertexBuffer(VertexBufferRHI);
}

void FResolveVertexBuffer::ReleaseDynamicRHI()
{
	VertexBufferRHI.SafeRelease();
	VertexDeclarationRHI.SafeRelease();
}
