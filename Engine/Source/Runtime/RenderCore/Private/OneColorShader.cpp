// Copyright Epic Games, Inc. All Rights Reserved.


#include "OneColorShader.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FClearShaderUB, )
	SHADER_PARAMETER_ARRAY(FVector4, DrawColorMRT, [8] )
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FClearShaderUB, "ClearShaderUB");

void FOneColorPS::SetColors(FRHICommandList& RHICmdList, const FLinearColor* Colors, int32 NumColors)
{
	check(NumColors <= MaxSimultaneousRenderTargets);

	auto& ClearUBParam = GetUniformBufferParameter<FClearShaderUB>();
	if (ClearUBParam.IsBound())
	{
		FClearShaderUB ClearData;
		FMemory::Memzero(ClearData.DrawColorMRT);
		for (int32 i = 0; i < NumColors; ++i)
		{
			ClearData.DrawColorMRT[i].X = Colors[i].R;
			ClearData.DrawColorMRT[i].Y = Colors[i].G;
			ClearData.DrawColorMRT[i].Z = Colors[i].B;
			ClearData.DrawColorMRT[i].W = Colors[i].A;
		}

		FLocalUniformBuffer LocalUB = TUniformBufferRef<FClearShaderUB>::CreateLocalUniformBuffer(RHICmdList, ClearData, UniformBuffer_SingleFrame);
		RHICmdList.SetLocalShaderUniformBuffer(RHICmdList.GetBoundPixelShader(), ClearUBParam.GetBaseIndex(), LocalUB);
	}
}

// #define avoids a lot of code duplication
#define IMPLEMENT_ONECOLORVS(A,B) typedef TOneColorVS<A,B> TOneColorVS##A##B; \
	IMPLEMENT_SHADER_TYPE2_WITH_TEMPLATE_PREFIX(template<> RENDERCORE_API, TOneColorVS##A##B, SF_Vertex);

IMPLEMENT_ONECOLORVS(false,false)
IMPLEMENT_ONECOLORVS(false,true)
IMPLEMENT_ONECOLORVS(true,true)
IMPLEMENT_ONECOLORVS(true,false)
#undef IMPLEMENT_ONECOLORVS

IMPLEMENT_GLOBAL_SHADER(FOneColorPS,"/Engine/Private/OneColorShader.usf","MainPixelShader",SF_Pixel);
// Compiling a version for every number of MRT's
// On AMD PC hardware, outputting to a color index in the shader without a matching render target set has a significant performance hit
IMPLEMENT_GLOBAL_SHADER(TOneColorPixelShaderMRT,"/Engine/Private/OneColorShader.usf","MainPixelShaderMRT",SF_Pixel);

IMPLEMENT_SHADER_TYPE(,FFillTextureCS,TEXT("/Engine/Private/OneColorShader.usf"),TEXT("MainFillTextureCS"),SF_Compute);
