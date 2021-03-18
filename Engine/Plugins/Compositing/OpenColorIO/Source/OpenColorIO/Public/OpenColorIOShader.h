// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	OpenColorIOShader.h: Shader base classes
=============================================================================*/

#pragma once

#include "GlobalShader.h"
#include "OpenColorIOShaderType.h"
#include "OpenColorIOShared.h"
#include "SceneView.h"
#include "Shader.h"
#include "ShaderParameters.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
class FTextureResource;
class UClass;

/** RDG Parameters to be used with FOpenColorIOPixelShader_RDG. */
BEGIN_SHADER_PARAMETER_STRUCT(FOpenColorIOPixelShaderParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, InputTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture3D, Ociolut3d)
	SHADER_PARAMETER_SAMPLER(SamplerState, Ociolut3dSampler)
	SHADER_PARAMETER(float, Gamma)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

namespace OpenColorIOShader
{
	static const TCHAR* OpenColorIOShaderFunctionName = TEXT("OCIOConvert");
	static const TCHAR* OCIOLut3dName = TEXT("Ociolut3d");
	static const uint32 MaximumTextureNumber = 10;
	static const uint32 Lut3dEdgeLength = 65;
}

class FOpenColorIOShader : public FShader
{
	DECLARE_TYPE_LAYOUT(FOpenColorIOShader, NonVirtual);
public:
	FOpenColorIOShader() = default;
	FOpenColorIOShader(const FOpenColorIOShader::CompiledShaderInitializerType& Initializer) : FShader(Initializer) {}
};

/** Vertex shader compatible with both RHI and RDG. */
class OPENCOLORIO_API FOpenColorIOVertexShader : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FOpenColorIOVertexShader, Global);

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	/** Default constructor. */
	FOpenColorIOVertexShader() {}

	/** Initialization constructor. */
	FOpenColorIOVertexShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
};


/** Base class of all shaders that need OpenColorIO pixel shader parameters. This Pixel shader is to be used only within RHI. */
class OPENCOLORIO_API FOpenColorIOPixelShader : public FOpenColorIOShader
{
	DECLARE_SHADER_TYPE(FOpenColorIOPixelShader, OpenColorIO);

public:
	using FPermutationParameters = FOpenColorIOShaderPermutationParameters;

	FOpenColorIOPixelShader() = default;
	FOpenColorIOPixelShader(const FOpenColorIOShaderType::CompiledShaderInitializerType& Initializer);

	static bool ShouldCompilePermutation(const FOpenColorIOShaderPermutationParameters& Parameters)
	{
		return true;
	}


	void SetParameters(FRHICommandList& InRHICmdList, FTextureResource* InInputTexture, float InGamma);
	void SetParameters(FRHICommandList& InRHICmdList, FTextureRHIRef InInputTexture, float InGamma);
	void SetLUTParameter(FRHICommandList& InRHICmdList, FTextureResource* InLUT3dResource);

	// Bind parameters
	void BindParams(const FShaderParameterMap& ParameterMap);

protected:
	LAYOUT_FIELD(FShaderResourceParameter, InputTexture)
	LAYOUT_FIELD(FShaderResourceParameter, InputTextureSampler)

	LAYOUT_FIELD(FShaderResourceParameter, OCIO3dTexture)
	LAYOUT_FIELD(FShaderResourceParameter, OCIO3dTextureSampler)
	LAYOUT_FIELD(FShaderParameter, Gamma)

private:
	LAYOUT_FIELD(FMemoryImageString, DebugDescription)
};


/** 
*   Pixel shader to be used within RDG environment. 
*	It is identical (on hlsl side) to the above FOpenColorIOPixelShader except for the way the resources are bound on CPU side. 
*/
class OPENCOLORIO_API FOpenColorIOPixelShader_RDG : public FGlobalShader
{
public:
	using FParameters = FOpenColorIOPixelShaderParameters;
	using FPermutationParameters = FOpenColorIOShaderPermutationParameters;
	DECLARE_SHADER_TYPE(FOpenColorIOPixelShader_RDG, OpenColorIO);
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(FOpenColorIOPixelShader_RDG, FGlobalShader);

	static bool ShouldCompilePermutation(const FOpenColorIOShaderPermutationParameters& Parameters)
	{
		return true;
	}
};




