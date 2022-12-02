// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "HAL/Platform.h"
#include "RHIDefinitions.h"
#include "Serialization/MemoryLayout.h"
#include "Shader.h"
#include "ShaderCore.h"
#include "ShaderParameters.h"
#include "DataDrivenShaderPlatformInfo.h"

class FPointerTableBase;
class FRHICommandList;
struct FResolveRect;

struct FDummyResolveParameter {};

class FResolveDepthPS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveDepthPS, Global, RENDERCORE_API);
public:
	
	typedef FDummyResolveParameter FParameter;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
		
	FResolveDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		UnresolvedSurface.Bind(Initializer.ParameterMap,TEXT("UnresolvedSurface"), SPF_Mandatory);
	}
	FResolveDepthPS() {}
	
	void SetParameters(FRHICommandList& RHICmdList, FParameter)
	{
	}

	LAYOUT_FIELD(FShaderResourceParameter, UnresolvedSurface);
};

class FResolveDepth2XPS : public FResolveDepthPS
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveDepth2XPS, Global, RENDERCORE_API);
public:

	typedef FDummyResolveParameter FParameter;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FResolveDepthPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DEPTH_RESOLVE_NUM_SAMPLES"), 2);
	}

	FResolveDepth2XPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FResolveDepthPS(Initializer)
	{
	}
	FResolveDepth2XPS() {}
};

class FResolveDepth4XPS : public FResolveDepthPS
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveDepth4XPS, Global, RENDERCORE_API);
public:

	typedef FDummyResolveParameter FParameter;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FResolveDepthPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DEPTH_RESOLVE_NUM_SAMPLES"), 4);
	}

	FResolveDepth4XPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FResolveDepthPS(Initializer)
	{
	}
	FResolveDepth4XPS() {}
};


class FResolveDepth8XPS : public FResolveDepthPS
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveDepth8XPS, Global, RENDERCORE_API);
public:

	typedef FDummyResolveParameter FParameter;

	static bool ShouldCache(EShaderPlatform Platform) { return GetMaxSupportedFeatureLevel(Platform) >= ERHIFeatureLevel::SM5; }

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FResolveDepthPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DEPTH_RESOLVE_NUM_SAMPLES"), 8);
	}

	FResolveDepth8XPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FResolveDepthPS(Initializer)
	{
	}
	FResolveDepth8XPS() {}
};

class FResolveDepthArrayPS : public FResolveDepthPS
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveDepthArrayPS, Global, RENDERCORE_API);
public:

	typedef FDummyResolveParameter FParameter;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FResolveDepthPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DEPTH_RESOLVE_TEXTUREARRAY"), 1);
	}

	FResolveDepthArrayPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FResolveDepthPS(Initializer)
	{
	}
	FResolveDepthArrayPS() {}
};

class FResolveDepthArray2XPS : public FResolveDepthArrayPS
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveDepthArray2XPS, Global, RENDERCORE_API);
public:

	typedef FDummyResolveParameter FParameter;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FResolveDepthArrayPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DEPTH_RESOLVE_NUM_SAMPLES"), 2);
	}

	FResolveDepthArray2XPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FResolveDepthArrayPS(Initializer)
	{
	}
	FResolveDepthArray2XPS() {}
};

class FResolveDepthArray4XPS : public FResolveDepthArrayPS
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveDepthArray4XPS, Global, RENDERCORE_API);
public:

	typedef FDummyResolveParameter FParameter;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FResolveDepthArrayPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DEPTH_RESOLVE_NUM_SAMPLES"), 4);
	}

	FResolveDepthArray4XPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FResolveDepthArrayPS(Initializer)
	{
	}
	FResolveDepthArray4XPS() {}
};


class FResolveDepthArray8XPS : public FResolveDepthArrayPS
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveDepthArray8XPS, Global, RENDERCORE_API);
public:

	typedef FDummyResolveParameter FParameter;

	static bool ShouldCache(EShaderPlatform Platform) { return GetMaxSupportedFeatureLevel(Platform) >= ERHIFeatureLevel::SM5; }

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FResolveDepthArrayPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DEPTH_RESOLVE_NUM_SAMPLES"), 8);
	}

	FResolveDepthArray8XPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FResolveDepthArrayPS(Initializer)
	{
	}
	FResolveDepthArray8XPS() {}
};

class FResolveSingleSamplePS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveSingleSamplePS, Global, RENDERCORE_API);
public:
	
	typedef uint32 FParameter;
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return FDataDrivenShaderPlatformInfo::GetIsLanguageD3D(Parameters.Platform);
	}
	
	FResolveSingleSamplePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		UnresolvedSurface.Bind(Initializer.ParameterMap,TEXT("UnresolvedSurface"), SPF_Mandatory);
		SingleSampleIndex.Bind(Initializer.ParameterMap,TEXT("SingleSampleIndex"), SPF_Mandatory);
	}
	FResolveSingleSamplePS() {}
	
	RENDERCORE_API void SetParameters(FRHICommandList& RHICmdList, uint32 SingleSampleIndexValue);
	
	LAYOUT_FIELD(FShaderResourceParameter, UnresolvedSurface);
	LAYOUT_FIELD(FShaderParameter, SingleSampleIndex);
};

/**
 * A vertex shader for rendering a textured screen element.
 */
class FResolveVS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveVS, Global, RENDERCORE_API);
public:
	
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
	
	FResolveVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PositionMinMax.Bind(Initializer.ParameterMap, TEXT("PositionMinMax"), SPF_Mandatory);
		UVMinMax.Bind(Initializer.ParameterMap, TEXT("UVMinMax"), SPF_Mandatory);
	}
	FResolveVS() {}

	RENDERCORE_API void SetParameters(FRHICommandList& RHICmdList, const FResolveRect& SrcBounds, const FResolveRect& DstBounds, uint32 DstSurfaceWidth, uint32 DstSurfaceHeight);

	LAYOUT_FIELD(FShaderParameter, PositionMinMax);
	LAYOUT_FIELD(FShaderParameter, UVMinMax);
};

class FResolveArrayVS : public FResolveVS
{
	DECLARE_EXPORTED_SHADER_TYPE(FResolveArrayVS, Global, RENDERCORE_API);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FResolveVS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DEPTH_RESOLVE_TEXTUREARRAY"), 1);
	}

	FResolveArrayVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FResolveVS(Initializer)
	{
	}
	FResolveArrayVS() {}

	RENDERCORE_API void SetParameters(FRHICommandList& RHICmdList, const FResolveRect& SrcBounds, const FResolveRect& DstBounds, uint32 DstSurfaceWidth, uint32 DstSurfaceHeight)
	{
		return FResolveVS::SetParameters(RHICmdList, SrcBounds, DstBounds, DstSurfaceWidth, DstSurfaceHeight);
	}
};
