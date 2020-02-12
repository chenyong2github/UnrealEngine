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

class FTextureResource;
class UClass;

namespace OpenColorIOShader
{
	static const TCHAR* OpenColorIOShaderFunctionName = TEXT("OCIOConvert");
	static const TCHAR* OCIOLut3dName = TEXT("ociolut3d_0");
	static const uint32 MaximumTextureNumber = 10;
	static const uint32 Lut3dEdgeLength = 32;
}

class FOpenColorIOShader : public FShader
{
	DECLARE_TYPE_LAYOUT(FOpenColorIOShader, NonVirtual);
public:
	FOpenColorIOShader() = default;
	FOpenColorIOShader(const FOpenColorIOShader::CompiledShaderInitializerType& Initializer) : FShader(Initializer) {}
};

/** Base class of all shaders that need OpenColorIO pixel shader parameters. */
class OPENCOLORIO_API FOpenColorIOPixelShader : public FOpenColorIOShader
{
public:
	DECLARE_SHADER_TYPE(FOpenColorIOPixelShader, OpenColorIO);

	using FPermutationParameters = FOpenColorIOShaderPermutationParameters;

	FOpenColorIOPixelShader()
	{
	}

	static bool ShouldCompilePermutation(const FOpenColorIOShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FOpenColorIOPixelShader(const FOpenColorIOShaderType::CompiledShaderInitializerType& Initializer);

	void SetParameters(FRHICommandList& InRHICmdList, FTextureResource* InInputTexture);
	void SetLUTParameter(FRHICommandList& InRHICmdList, FTextureResource* InLUT3dResource);
	
	// Bind parameters
	void BindParams(const FShaderParameterMap &ParameterMap);

protected:
	LAYOUT_FIELD(FShaderResourceParameter, InputTexture)
	LAYOUT_FIELD(FShaderResourceParameter, InputTextureSampler)
	
	LAYOUT_FIELD(FShaderResourceParameter, OCIO3dTexture)
	LAYOUT_FIELD(FShaderResourceParameter, OCIO3dTextureSampler)

private:
	LAYOUT_FIELD(FMemoryImageString, DebugDescription)
};
