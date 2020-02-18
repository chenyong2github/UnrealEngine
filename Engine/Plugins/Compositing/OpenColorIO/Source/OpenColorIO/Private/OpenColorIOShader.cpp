// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenColorIOShader.h"

#include "Engine/VolumeTexture.h"
#include "OpenColorIODerivedDataVersion.h"
#include "OpenColorIOShaderCompilationManager.h"
#include "OpenColorIOShared.h"
#include "ProfilingDebugging/CookStats.h"
#include "ProfilingDebugging/DiagnosticTable.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "ShaderCompiler.h"
#include "ShaderParameterUtils.h"
#include "Stats/StatsMisc.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_TYPE_LAYOUT(FOpenColorIOShader);

IMPLEMENT_SHADER_TYPE(, FOpenColorIOPixelShader, TEXT("/Plugin/OpenColorIO/Private/OpenColorIOShader.usf"), TEXT("MainPS"), SF_Pixel)


//////////////////////////////////////////////////////////////////////////


FOpenColorIOPixelShader::FOpenColorIOPixelShader(const FOpenColorIOShaderType::CompiledShaderInitializerType& Initializer)
	: FOpenColorIOShader(Initializer)
	, DebugDescription(Initializer.DebugDescription)
{
	BindParams(Initializer.ParameterMap);
}

void FOpenColorIOPixelShader::SetParameters(FRHICommandList& InRHICmdList, FTextureResource* InInputTexture)
{
	SetTextureParameter(InRHICmdList, InRHICmdList.GetBoundPixelShader(), InputTexture, InputTextureSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InInputTexture->TextureRHI);
}

void FOpenColorIOPixelShader::SetLUTParameter(FRHICommandList& InRHICmdList, FTextureResource* InLUT3dResource)
{
	SetTextureParameter(InRHICmdList, InRHICmdList.GetBoundPixelShader(), OCIO3dTexture, OCIO3dTextureSampler, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InLUT3dResource->TextureRHI);
}

void FOpenColorIOPixelShader::BindParams(const FShaderParameterMap &ParameterMap)
{
	InputTexture.Bind(ParameterMap, TEXT("InputTexture"));
	InputTextureSampler.Bind(ParameterMap, TEXT("InputTextureSampler"));

	OCIO3dTexture.Bind(ParameterMap, OpenColorIOShader::OCIOLut3dName);
	OCIO3dTextureSampler.Bind(ParameterMap, TEXT("ociolut3d_0Sampler"));
}

