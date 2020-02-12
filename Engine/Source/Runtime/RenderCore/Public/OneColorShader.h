// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"

/**
 * Vertex shader for rendering a single, constant color.
 */
template<bool bUsingNDCPositions=true, bool bUsingVertexLayers=false>
class TOneColorVS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(TOneColorVS, Global, RENDERCORE_API);

	/** Default constructor. */
	TOneColorVS() {}

public:

	TOneColorVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		DepthParameter.Bind(Initializer.ParameterMap, TEXT("InputDepth"), SPF_Mandatory);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USING_NDC_POSITIONS"), (uint32)(bUsingNDCPositions ? 1 : 0));
		OutEnvironment.SetDefine(TEXT("USING_LAYERS"), (uint32)(bUsingVertexLayers ? 1 : 0));
	}

	void SetDepthParameter(FRHICommandList& RHICmdList, float Depth)
	{
		SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), DepthParameter, Depth);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
	
	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Engine/Private/OneColorShader.usf");
	}
	
	static const TCHAR* GetFunctionName()
	{
		return TEXT("MainVertexShader");
	}

private:
	LAYOUT_FIELD(FShaderParameter, DepthParameter);
};

/**
 * Pixel shader for rendering a single, constant color.
 */
class RENDERCORE_API FOneColorPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FOneColorPS);
public:
	
	FOneColorPS( )	{ }
	FOneColorPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader( Initializer )
	{
		//ColorParameter.Bind( Initializer.ParameterMap, TEXT("DrawColorMRT"), SPF_Mandatory);
	}

	void SetColors(FRHICommandList& RHICmdList, const FLinearColor* Colors, int32 NumColors);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	/** The parameter to use for setting the draw Color. */
	//LAYOUT_FIELD(FShaderParameter, ColorParameter);
};

/**
 * Pixel shader for rendering a single, constant color to MRTs.
 */
class RENDERCORE_API TOneColorPixelShaderMRT : public FOneColorPS
{
	DECLARE_GLOBAL_SHADER(TOneColorPixelShaderMRT);
public:
	class TOneColorPixelShader128bitRT : SHADER_PERMUTATION_BOOL("b128BITRENDERTARGET");
	class TOneColorPixelShaderNumOutputs : SHADER_PERMUTATION_RANGE_INT("NUM_OUTPUTS", 1, 8);
	using FPermutationDomain = TShaderPermutationDomain<TOneColorPixelShaderNumOutputs, TOneColorPixelShader128bitRT>;

	TOneColorPixelShaderMRT( )	{ }
	TOneColorPixelShaderMRT(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FOneColorPS( Initializer )
	{
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if(PermutationVector.Get<TOneColorPixelShaderMRT::TOneColorPixelShaderNumOutputs>())
		{
			return IsFeatureLevelSupported( Parameters.Platform, ERHIFeatureLevel::ES3_1) && 
				(PermutationVector.Get<TOneColorPixelShaderMRT::TOneColorPixelShader128bitRT>() ? FDataDrivenShaderPlatformInfo::GetRequiresExplicit128bitRT(Parameters.Platform) : true);
		}

		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FOneColorPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<TOneColorPixelShaderMRT::TOneColorPixelShader128bitRT>())
		{
			OutEnvironment.SetRenderTargetOutputFormat(0, PF_A32B32G32R32F);
		}
	}
};

/**
 * Compute shader for writing values
 */
class FFillTextureCS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FFillTextureCS,Global,RENDERCORE_API);
public:
	FFillTextureCS( )	{ }
	FFillTextureCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader( Initializer )
	{
		FillValue.Bind( Initializer.ParameterMap, TEXT("FillValue"), SPF_Mandatory);
		Params0.Bind( Initializer.ParameterMap, TEXT("Params0"), SPF_Mandatory);
		Params1.Bind( Initializer.ParameterMap, TEXT("Params1"), SPF_Mandatory);
		Params2.Bind( Initializer.ParameterMap, TEXT("Params2"), SPF_Optional);
		FillTexture.Bind( Initializer.ParameterMap, TEXT("FillTexture"), SPF_Mandatory);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	LAYOUT_FIELD(FShaderParameter, FillValue);
	LAYOUT_FIELD(FShaderParameter, Params0);	// Texture Width,Height (.xy); Use Exclude Rect 1 : 0 (.z)
	LAYOUT_FIELD(FShaderParameter, Params1);	// Include X0,Y0 (.xy) - X1,Y1 (.zw)
	LAYOUT_FIELD(FShaderParameter, Params2);	// ExcludeRect X0,Y0 (.xy) - X1,Y1 (.zw)
	LAYOUT_FIELD(FShaderResourceParameter, FillTexture);
};

class FLongGPUTaskPS : public FGlobalShader
{
	DECLARE_EXPORTED_SHADER_TYPE(FLongGPUTaskPS,Global,RENDERCORE_API);
public:
	FLongGPUTaskPS( )	{ }
	FLongGPUTaskPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader( Initializer )
	{
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// MLCHANGES BEGIN
		return true;
		// MLCHANGES END
	}
};

