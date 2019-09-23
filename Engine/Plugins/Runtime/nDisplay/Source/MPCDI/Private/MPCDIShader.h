// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "RHIStaticStates.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"

#include "IMPCDI.h"

#include "MPCDIData.h"
#include "MPCDIRegion.h"


enum class EMPCDIShader : uint8
{
	Passthrough,
	ShowWarpTexture,
	WarpAndBlend,
	Warp,
	Invalid,
};

class FMPCDIData;


class FMPCDIShader
{
public:
	static bool ApplyWarpBlend(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& TextureWarpData, IMPCDI::FShaderInputData& ShaderInputData, FMPCDIData* MPCDIData);
};


struct FMpcdiWarpDrawRectangleParameters
{
	FVector4 DrawRectanglePosScaleBias;
	FVector4 DrawRectangleInvTargetSizeAndTextureSize;
	FVector4 DrawRectangleUVScaleBias;
};

class FMPCDIDirectProjectionVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FMPCDIDirectProjectionVS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	/** Default constructor. */
	FMPCDIDirectProjectionVS() 
	{
	}


public:
	/** Initialization constructor. */
	FMPCDIDirectProjectionVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
};


class FMpcdiWarpVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FMpcdiWarpVS, Global);

public:
	FMpcdiWarpVS()
	{ 
	}

	FMpcdiWarpVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		DrawRectanglePosScaleBiasParameter.Bind(Initializer.ParameterMap, TEXT("DrawRectanglePosScaleBias"));
		DrawRectangleUVScaleBiasParameter.Bind(Initializer.ParameterMap, TEXT("DrawRectangleUVScaleBias"));
		DrawRectangleInvTargetSizeAndTextureSizeParameter.Bind(Initializer.ParameterMap, TEXT("DrawRectangleInvTargetSizeAndTextureSize"));
	}

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

public:
	template<typename TShaderRHIParamRef>
	void SetParameters(FRHICommandList& RHICmdList, const TShaderRHIParamRef ShaderRHI, const FMpcdiWarpDrawRectangleParameters& ShaderInputData)
	{
		SetShaderValue(RHICmdList, ShaderRHI, DrawRectanglePosScaleBiasParameter, ShaderInputData.DrawRectanglePosScaleBias);
		SetShaderValue(RHICmdList, ShaderRHI, DrawRectangleUVScaleBiasParameter, ShaderInputData.DrawRectangleUVScaleBias);
		SetShaderValue(RHICmdList, ShaderRHI, DrawRectangleInvTargetSizeAndTextureSizeParameter, ShaderInputData.DrawRectangleInvTargetSizeAndTextureSize);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar
			<< DrawRectanglePosScaleBiasParameter
			<< DrawRectangleUVScaleBiasParameter
			<< DrawRectangleInvTargetSizeAndTextureSizeParameter;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter DrawRectanglePosScaleBiasParameter;
	FShaderParameter DrawRectangleUVScaleBiasParameter;
	FShaderParameter DrawRectangleInvTargetSizeAndTextureSizeParameter;
};



template<EMPCDIShader ShaderType>
class TMpcdiWarpBasePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TMpcdiWarpBasePS, Global);

public:
	TMpcdiWarpBasePS()
	{ 
	}

	TMpcdiWarpBasePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
#if 0
		//Cubemap supoport
		if (ShaderType != EMPCDIShader::WarpAndBlendCubeMap)
#endif
		{
			PostprocessInputParameter0.Bind(Initializer.ParameterMap, TEXT("PostprocessInput0"));
			PostprocessInputParameterSampler0.Bind(Initializer.ParameterMap, TEXT("PostprocessInput0Sampler"));
		}

		TextureProjectionMatrixParameter.Bind(Initializer.ParameterMap, TEXT("TextureProjectionMatrix"));

		if (ShaderType != EMPCDIShader::Passthrough)
		{
			AlphaEmbeddedGammaParameter.Bind(Initializer.ParameterMap, TEXT("AlphaEmbeddedGamma"));

			WarpMapParameter.Bind(Initializer.ParameterMap, TEXT("WarpMap"));
			WarpMapParameterSampler.Bind(Initializer.ParameterMap, TEXT("WarpMapSampler"));

			AlphaMapParameter.Bind(Initializer.ParameterMap, TEXT("AlphaMap"));
			AlphaMapParameterSampler.Bind(Initializer.ParameterMap, TEXT("AlphaMapSampler"));
			BetaMapParameter.Bind(Initializer.ParameterMap, TEXT("BetaMap"));
			BetaMapParameterSampler.Bind(Initializer.ParameterMap, TEXT("BetaMapSampler"));


#if 0
			if (ShaderType == EMPCDIShader::WarpAndBlendCubeMap)
			{
				SceneCubeMapParameter.Bind(Initializer.ParameterMap, TEXT("SceneCubemap"));
				SceneCubeMapParameterSampler.Bind(Initializer.ParameterMap, TEXT("SceneCubemapSampler"));
			}
#endif

				EyePositionParameter.Bind(Initializer.ParameterMap, TEXT("EyePosition"));
				EyeLookAtParameter.Bind(Initializer.ParameterMap, TEXT("EyeLookAt"));
			VignetteEVParameter.Bind(Initializer.ParameterMap, TEXT("VignetteEV"));		
		}
	}

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		switch (ShaderType)
		{
		case EMPCDIShader::WarpAndBlend:
			OutEnvironment.SetDefine(TEXT("RENDER_CASE_APPLYBLENDING"), 1);
			break;
		default:
			break;
		}
	}

public:
	template<typename TShaderRHIParamRef>
	void SetParameters(FRHICommandListImmediate& RHICmdList, const TShaderRHIParamRef ShaderRHI, const FMatrix& UVMatrix)
	{
		SetShaderValue(RHICmdList, ShaderRHI, TextureProjectionMatrixParameter, UVMatrix);
	}

	template<typename TShaderRHIParamRef>
	void SetParameters(FRHICommandListImmediate& RHICmdList, const TShaderRHIParamRef ShaderRHI, const IMPCDI::FShaderInputData& ShaderInputData)
	{
		SetShaderValue(RHICmdList, ShaderRHI, TextureProjectionMatrixParameter, ShaderInputData.UVMatrix);

#if 0
		if (ShaderType == EMPCDIShader::WarpAndBlendCubeMap)
		{
			RHICmdList.SetShaderSampler(ShaderRHI, SceneCubeMapParameterSampler.GetBaseIndex(), TStaticSamplerState<SF_Bilinear>::GetRHI());
			SetTextureParameter(RHICmdList, ShaderRHI, SceneCubeMapParameter, ShaderInputData.SceneCubeMap);
		}
#endif

			SetShaderValue(RHICmdList, ShaderRHI, EyePositionParameter, ShaderInputData.EyePosition);
			SetShaderValue(RHICmdList, ShaderRHI, EyeLookAtParameter, ShaderInputData.EyeLookAt);
			SetShaderValue(RHICmdList, ShaderRHI, VignetteEVParameter, ShaderInputData.VignetteEV);
		}

	template<typename TShaderRHIParamRef>
	void SetParameters(FRHICommandListImmediate& RHICmdList, const TShaderRHIParamRef ShaderRHI, FRHITexture2D* SourceTexture)
	{
#if 0
		//Cubemap support
		if (ShaderType != EMPCDIShader::WarpAndBlendCubeMap)
#endif
		{
			SetTextureParameter(RHICmdList, ShaderRHI, PostprocessInputParameter0, SourceTexture);
			RHICmdList.SetShaderSampler(ShaderRHI, PostprocessInputParameterSampler0.GetBaseIndex(), TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		}
	}

	template<typename TShaderRHIParamRef>
	void SetParameters(FRHICommandListImmediate& RHICmdList, const TShaderRHIParamRef ShaderRHI, MPCDI::FMPCDIRegion& MPCDIRegionData)
	{
		if (ShaderType != EMPCDIShader::Passthrough)
		{
			SetShaderValue(RHICmdList, ShaderRHI, AlphaEmbeddedGammaParameter, MPCDIRegionData.AlphaMap.GetEmbeddedGamma());

			SetTextureParameter(RHICmdList, ShaderRHI, WarpMapParameter, MPCDIRegionData.WarpMap.TextureRHI);
			RHICmdList.SetShaderSampler(ShaderRHI, WarpMapParameterSampler.GetBaseIndex(), TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());

			SetTextureParameter(RHICmdList, ShaderRHI, AlphaMapParameter, MPCDIRegionData.AlphaMap.TextureRHI);
			RHICmdList.SetShaderSampler(ShaderRHI, AlphaMapParameterSampler.GetBaseIndex(), TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());

			SetTextureParameter(RHICmdList, ShaderRHI, BetaMapParameter, MPCDIRegionData.BetaMap.TextureRHI);
			RHICmdList.SetShaderSampler(ShaderRHI, BetaMapParameterSampler.GetBaseIndex(), TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		}
	}

	template<typename TShaderRHIParamRef>
	void SetParameters(FRHICommandListImmediate& RHICmdList, const TShaderRHIParamRef ShaderRHI, FRHITexture2D* SourceTexture, const IMPCDI::FShaderInputData& ShaderInputData, MPCDI::FMPCDIRegion& MPCDIRegionData)
	{
		SetParameters(RHICmdList, ShaderRHI, SourceTexture);
		SetParameters(RHICmdList, ShaderRHI, ShaderInputData);
		SetParameters(RHICmdList, ShaderRHI, MPCDIRegionData);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		Ar
			<< PostprocessInputParameter0
			<< PostprocessInputParameterSampler0

			<< WarpMapParameter
			<< WarpMapParameterSampler

			<< AlphaMapParameter
			<< AlphaMapParameterSampler
			<< BetaMapParameter
			<< BetaMapParameterSampler

			<< TextureProjectionMatrixParameter
			<< AlphaEmbeddedGammaParameter

#if 0
			// Cubemap support
			<< SceneCubeMapParameter
			<< SceneCubeMapParameterSampler
#endif

			<< EyePositionParameter
			<< EyeLookAtParameter
			<< VignetteEVParameter
			;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter PostprocessInputParameter0;
	FShaderResourceParameter PostprocessInputParameterSampler0;

	FShaderResourceParameter WarpMapParameter;
	FShaderResourceParameter WarpMapParameterSampler;

	FShaderResourceParameter AlphaMapParameter;
	FShaderResourceParameter AlphaMapParameterSampler;
	FShaderResourceParameter BetaMapParameter;
	FShaderResourceParameter BetaMapParameterSampler;

	FShaderParameter         TextureProjectionMatrixParameter;
	FShaderParameter         AlphaEmbeddedGammaParameter;
#if 0
	//@todo  Cubemap support not implemented
	FShaderResourceParameter SceneCubeMapParameter;
	FShaderResourceParameter SceneCubeMapParameterSampler;
#endif

	FShaderParameter         EyePositionParameter;
	FShaderParameter         EyeLookAtParameter;
	FShaderParameter         VignetteEVParameter;
};

typedef TMpcdiWarpBasePS<EMPCDIShader::Passthrough>          FMPCDIPassthroughPS;
typedef TMpcdiWarpBasePS<EMPCDIShader::ShowWarpTexture>      FMPCDIShowWarpTexture;
typedef TMpcdiWarpBasePS<EMPCDIShader::WarpAndBlend>         FMPCDIWarpAndBlendPS;
typedef TMpcdiWarpBasePS<EMPCDIShader::Warp>                 FMPCDIWarpPS;

#if 0
// Cubemap support
typedef TMpcdiWarpBasePS<EMPCDIShader::WarpAndBlendCubeMap>  FMPCDIWarpAndBlendCubemapPS;
#endif
#if 0
// Gpu perfect frustum
typedef TMpcdiWarpBasePS<EMPCDIShader::BuildProjectedAABB>   FMPCDICalcBoundBoxPS;
#endif
