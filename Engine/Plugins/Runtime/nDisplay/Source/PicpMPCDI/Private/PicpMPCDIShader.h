// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"

#include "IMPCDI.h"
#include "IPicpMPCDI.h"

#include "Overlay/PicpProjectionOverlayRender.h"


class FPicpMPCDIShader
{
public:
	static bool ApplyWarpBlend(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& TextureWarpData, IMPCDI::FShaderInputData& ShaderInputData, FMPCDIData* MPCDIData, FPicpProjectionOverlayViewportData* ViewportOverlayData);
};



struct FPicpMpcdiWarpDrawRectangleParameters
{
	FVector4 DrawRectanglePosScaleBias;
	FVector4 DrawRectangleInvTargetSizeAndTextureSize;
	FVector4 DrawRectangleUVScaleBias;
};

class FPicpWarpVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPicpWarpVS, Global);

public:
	FPicpWarpVS()
	{ }

	FPicpWarpVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		DrawRectanglePosScaleBiasParameter.Bind(Initializer.ParameterMap, TEXT("DrawRectanglePosScaleBias"));
		DrawRectangleUVScaleBiasParameter.Bind(Initializer.ParameterMap, TEXT("DrawRectangleUVScaleBias"));
		DrawRectangleInvTargetSizeAndTextureSizeParameter.Bind(Initializer.ParameterMap, TEXT("DrawRectangleInvTargetSizeAndTextureSize"));
	}

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

public:
	template<typename TShaderRHIParamRef>
	void SetParameters(FRHICommandList& RHICmdList, const TShaderRHIParamRef ShaderRHI, const FPicpMpcdiWarpDrawRectangleParameters& ShaderInputData)
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


enum class EPicpShaderType : uint8
{
	PassThrought, // viewport frustum (no warpblend, only frustum math)
	WarpAndBlend, // Pure mpcdi warpblend for viewport

	//One-pass solution: (+overlay_over+camera+overlay_over +lut)
	WarpAndBlendOneCameraBack,
	WarpAndBlendOneCameraFront,
	WarpAndBlendOneCameraFull,
	WarpAndBlendOneCameraNone,

		// Multi-pass render:
	WarpAndBlendBg,                 // bg +lut
	WarpAndBlendBgOverlay,          // bg+overlay_under +lut
	WarpAndBlendAddCamera,          // +camera +lut   (Additive blend colorop)
	WarpAndBlendAddCameraOverlay,   // +camera+overlay_over +lut (Additive blend colorop)
	WarpAndBlendAddOverlay,         // +overlay_over +lut (Additive blend colorop)
	
	Invalid,
};



template<EPicpShaderType ShaderType>
class TPicpWarpBasePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TPicpWarpBasePS, Global);

public:
	TPicpWarpBasePS()
	{ }

	TPicpWarpBasePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		{
			PostprocessInputParameter0.Bind(Initializer.ParameterMap, TEXT("PostprocessInput0"));
			PostprocessInputParameterSampler0.Bind(Initializer.ParameterMap, TEXT("PostprocessInput0Sampler"));
		}

		TextureProjectionMatrixParameter.Bind(Initializer.ParameterMap, TEXT("TextureProjectionMatrix"));
		MultiViewportMatrixParameter.Bind(Initializer.ParameterMap, TEXT("MultiViewportMatrix"));

		if (ShaderType != EPicpShaderType::PassThrought)
		{
			AlphaEmbeddedGammaParameter.Bind(Initializer.ParameterMap, TEXT("AlphaEmbeddedGamma"));

			WarpMapParameter.Bind(Initializer.ParameterMap, TEXT("WarpMap"));
			WarpMapParameterSampler.Bind(Initializer.ParameterMap, TEXT("WarpMapSampler"));

			AlphaMapParameter.Bind(Initializer.ParameterMap, TEXT("AlphaMap"));
			AlphaMapParameterSampler.Bind(Initializer.ParameterMap, TEXT("AlphaMapSampler"));
			BetaMapParameter.Bind(Initializer.ParameterMap, TEXT("BetaMap"));
			BetaMapParameterSampler.Bind(Initializer.ParameterMap, TEXT("BetaMapSampler"));

			if (ShaderType > EPicpShaderType::WarpAndBlend)
			{
				CameraProjectionMatrixParameter.Bind(Initializer.ParameterMap, TEXT("CameraProjectionMatrix"));
				CameraSoftEdgeParameter.Bind(Initializer.ParameterMap, TEXT("CameraSoftEdge"));

				CameraMapParameter.Bind(Initializer.ParameterMap, TEXT("CameraMap"));
				CameraMapParameterSampler.Bind(Initializer.ParameterMap, TEXT("CameraMapSampler"));

				ViewportOverlayMapParameter.Bind(Initializer.ParameterMap, TEXT("ViewportOverlayMap"));
				ViewportOverlayMapParameterSampler.Bind(Initializer.ParameterMap, TEXT("ViewportOverlayMapSampler"));
				ViewportOverlayBackMapParameter.Bind(Initializer.ParameterMap, TEXT("ViewportOverlayBackMap"));
				ViewportOverlayBackMapParameterSampler.Bind(Initializer.ParameterMap, TEXT("ViewportOverlayBackMapSampler"));
			}
		}
	}

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		switch (ShaderType)
		{
		case EPicpShaderType::WarpAndBlendOneCameraNone:
			break;

		case EPicpShaderType::WarpAndBlendOneCameraBack:
			OutEnvironment.SetDefine(TEXT("USE_OVERLAY_BACK"), 1);
			break;

		case EPicpShaderType::WarpAndBlendOneCameraFront:
			OutEnvironment.SetDefine(TEXT("USE_OVERLAY_FRONT"), 1);
			break;

		case EPicpShaderType::WarpAndBlendOneCameraFull:
			OutEnvironment.SetDefine(TEXT("USE_OVERLAY_BACK"), 1);
			OutEnvironment.SetDefine(TEXT("USE_OVERLAY_FRONT"), 1);
			break;

		case EPicpShaderType::WarpAndBlendAddCamera:
			OutEnvironment.SetDefine(TEXT("RENDER_CASE_CAMERA"), 1);
			break;

		case EPicpShaderType::WarpAndBlendAddCameraOverlay:
			OutEnvironment.SetDefine(TEXT("RENDER_CASE_CAMERA"), 1);
			OutEnvironment.SetDefine(TEXT("RENDER_CASE_VIEWPORT"), 1);
			OutEnvironment.SetDefine(TEXT("RENDER_CASE_VIEWPORT_OVERLAY"), 1);
			break;

		case EPicpShaderType::WarpAndBlendAddOverlay:
			OutEnvironment.SetDefine(TEXT("RENDER_CASE_VIEWPORT"), 1);
			OutEnvironment.SetDefine(TEXT("RENDER_CASE_VIEWPORT_OVERLAY"), 1);
			break;

		case EPicpShaderType::WarpAndBlendBgOverlay:
			OutEnvironment.SetDefine(TEXT("RENDER_CASE_VIEWPORT"), 1);
			OutEnvironment.SetDefine(TEXT("RENDER_CASE_VIEWPORT_OVERLAY"), 1);
			OutEnvironment.SetDefine(TEXT("RENDER_CASE_VIEWPORT_BASE"), 1);
			break;

		case EPicpShaderType::WarpAndBlendBg:
			OutEnvironment.SetDefine(TEXT("RENDER_CASE_VIEWPORT"), 1);
			OutEnvironment.SetDefine(TEXT("RENDER_CASE_VIEWPORT_BASE"), 1);
			break;
		default:
			// Do nothing
			break;
		}
	}

public:
	template<typename TShaderRHIParamRef>
	void SetParameters(FRHICommandListImmediate& RHICmdList, const TShaderRHIParamRef ShaderRHI, const FMatrix& UVMatrix, const FMatrix& ViewportUVMatrix)
	{
		SetShaderValue(RHICmdList, ShaderRHI, TextureProjectionMatrixParameter, UVMatrix);
		SetShaderValue(RHICmdList, ShaderRHI, MultiViewportMatrixParameter, UVMatrix*ViewportUVMatrix);
	}


	template<typename TShaderRHIParamRef>
	void SetParameters(FRHICommandListImmediate& RHICmdList, const TShaderRHIParamRef ShaderRHI, FRHITexture2D* SourceTexture)
	{
		{
			SetTextureParameter(RHICmdList, ShaderRHI, PostprocessInputParameter0, SourceTexture);
			RHICmdList.SetShaderSampler(ShaderRHI, PostprocessInputParameterSampler0.GetBaseIndex(), TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
			//SetTextureParameter(RHICmdList, ShaderRHI, PostprocessInputParameter0, PostprocessInputParameterSampler0, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), SourceTexture);
		}
	}

	template<typename TShaderRHIParamRef>
	void SetParameters(FRHICommandListImmediate& RHICmdList, const TShaderRHIParamRef ShaderRHI, MPCDI::FMPCDIRegion& MPCDIRegionData)
	{
		if (ShaderType != EPicpShaderType::PassThrought)
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
	void SetPicpParameters(FRHICommandListImmediate& RHICmdList, const TShaderRHIParamRef ShaderRHI, const FPicpProjectionOverlayCamera* OverlayCamera)
	{
		if (OverlayCamera && ShaderType > EPicpShaderType::WarpAndBlend) // Only Picp techniques
		{
			SetShaderValue(RHICmdList, ShaderRHI, CameraProjectionMatrixParameter, OverlayCamera->GetRuntimeCameraProjection());
			SetShaderValue(RHICmdList, ShaderRHI, CameraSoftEdgeParameter, OverlayCamera->SoftEdge);

			SetTextureParameter(RHICmdList, ShaderRHI, CameraMapParameter, OverlayCamera->CameraTexture);
			RHICmdList.SetShaderSampler(ShaderRHI, CameraMapParameterSampler.GetBaseIndex(), TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		}
	}

	template<typename TShaderRHIParamRef>
	void SetPicpParameters(FRHICommandListImmediate& RHICmdList, const TShaderRHIParamRef ShaderRHI, const FPicpProjectionOverlayViewport* OverlayOverViewport, const FPicpProjectionOverlayViewport* OverlayBackViewport)
	{
		if (OverlayOverViewport && ShaderType > EPicpShaderType::WarpAndBlend) // Only Picp techniques
		{
			SetTextureParameter(RHICmdList, ShaderRHI, ViewportOverlayMapParameter, OverlayOverViewport->ViewportTexture);
			RHICmdList.SetShaderSampler(ShaderRHI, ViewportOverlayMapParameterSampler.GetBaseIndex(), TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		}

		if (OverlayBackViewport && ShaderType > EPicpShaderType::WarpAndBlend) // Only Picp techniques
		{
			SetTextureParameter(RHICmdList, ShaderRHI, ViewportOverlayBackMapParameter, OverlayBackViewport->ViewportTexture);
			RHICmdList.SetShaderSampler(ShaderRHI, ViewportOverlayBackMapParameterSampler.GetBaseIndex(), TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
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

			<< CameraProjectionMatrixParameter
			<< CameraSoftEdgeParameter
			<< CameraMapParameter
			<< CameraMapParameterSampler

			<< ViewportOverlayMapParameter
			<< ViewportOverlayMapParameterSampler
			<< ViewportOverlayBackMapParameter
			<< ViewportOverlayBackMapParameterSampler


			<< TextureProjectionMatrixParameter
			<< MultiViewportMatrixParameter
			<< AlphaEmbeddedGammaParameter

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

	FShaderParameter         CameraProjectionMatrixParameter;
	FShaderParameter         CameraSoftEdgeParameter;
	FShaderResourceParameter CameraMapParameter;
	FShaderResourceParameter CameraMapParameterSampler;

	FShaderResourceParameter ViewportOverlayMapParameter;
	FShaderResourceParameter ViewportOverlayMapParameterSampler;
	FShaderResourceParameter ViewportOverlayBackMapParameter;
	FShaderResourceParameter ViewportOverlayBackMapParameterSampler;

	FShaderParameter         TextureProjectionMatrixParameter;
	FShaderParameter         MultiViewportMatrixParameter;
	FShaderParameter         AlphaEmbeddedGammaParameter;

	FShaderParameter         EyePositionParameter;
	FShaderParameter         EyeLookAtParameter;
	FShaderParameter         VignetteEVParameter;
};

typedef TPicpWarpBasePS<EPicpShaderType::PassThrought>         FPicpMPCDIPassThroughtPS;
typedef TPicpWarpBasePS<EPicpShaderType::WarpAndBlend>         FPicpMPCDIWarpAndBlendPS;

typedef TPicpWarpBasePS<EPicpShaderType::WarpAndBlendBg>                 FPicpMPCDIWarpAndBlendBgPS;
typedef TPicpWarpBasePS<EPicpShaderType::WarpAndBlendBgOverlay>          FPicpMPCDIWarpAndBlendBgOverlayPS;

typedef TPicpWarpBasePS<EPicpShaderType::WarpAndBlendAddCamera>          FPicpMPCDIWarpAndBlendCameraPS;
typedef TPicpWarpBasePS<EPicpShaderType::WarpAndBlendAddCameraOverlay>   FPicpMPCDIWarpAndBlendCameraOverlayPS;
typedef TPicpWarpBasePS<EPicpShaderType::WarpAndBlendAddOverlay>         FPicpMPCDIWarpAndBlendOverlayPS;

typedef TPicpWarpBasePS<EPicpShaderType::WarpAndBlendOneCameraFull>      FPicpWarpAndBlendOneCameraFullPS;
typedef TPicpWarpBasePS<EPicpShaderType::WarpAndBlendOneCameraBack>      FPicpWarpAndBlendOneCameraBackPS;
typedef TPicpWarpBasePS<EPicpShaderType::WarpAndBlendOneCameraFront>     FPicpWarpAndBlendOneCameraFrontPS;
typedef TPicpWarpBasePS<EPicpShaderType::WarpAndBlendOneCameraNone>      FPicpWarpAndBlendOneCameraNonePS;

