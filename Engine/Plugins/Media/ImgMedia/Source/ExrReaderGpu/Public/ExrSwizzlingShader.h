// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS

#include "CoreMinimal.h"
#include "UObject/ConstructorHelpers.h"
#include "RHI.h"
#include "Runtime/RenderCore/Public/ShaderParameterUtils.h"
#include "Runtime/RenderCore/Public/RenderResource.h"
#include "Runtime/RenderCore/Public/RenderGraphResources.h"

// FScreenPassTextureViewportParameters and FScreenPassTextureInput
#include "Runtime/Renderer/Private/ScreenPass.h"
#include "Runtime/Renderer/Private/SceneTextureParameters.h"


// The vertex shader used by DrawScreenPass to draw a rectangle.
class EXRREADERGPU_API FExrSwizzleVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FExrSwizzleVS);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters&)
	{
		return true;
	}

	FExrSwizzleVS() = default;
	FExrSwizzleVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

/** Pixel shader swizzle RGB planar buffer data into proper RGBA texture. */
class EXRREADERGPU_API FExrSwizzlePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FExrSwizzlePS);
	SHADER_USE_PARAMETER_STRUCT(FExrSwizzlePS, FGlobalShader);

	/** If the provided buffer is RGBA the shader would work slightly differently to RGB. */
	class FRgbaSwizzle : SHADER_PERMUTATION_INT("NUM_CHANNELS", 5);
	using FPermutationDomain = TShaderPermutationDomain<FRgbaSwizzle>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{
		return true;
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SRV(StructuredBuffer<uint>, UnswizzledBuffer)
		SHADER_PARAMETER(int, TextureWidth)
		SHADER_PARAMETER(int, TextureHeight)
	END_SHADER_PARAMETER_STRUCT()
};
#endif