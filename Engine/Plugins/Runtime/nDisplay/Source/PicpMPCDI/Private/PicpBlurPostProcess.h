// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "PicpPostProcessing.h"
#include "TextureResource.h"

class UTexture;
class UTextureRenderTarget2D;

class FPicpBlurPostProcess
{
public:
	//on Gamethread
	static void ApplyBlur(UTextureRenderTarget2D* InOutRenderTarget, UTextureRenderTarget2D* TemporaryRenderTarget, int KernelRadius, float KernelScale, EPicpBlurPostProcessShaderType BlurType);
	static void ApplyCompose(UTexture* InputTexture, UTextureRenderTarget2D* OutputRenderTarget, UTextureRenderTarget2D* Result);
	static void ExecuteCompose();

	//onRenderThread
	static void ApplyBlur_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutRT, FRHITexture2D* TempRT, int KernelRadius, float KernelScale, EPicpBlurPostProcessShaderType BlurType);
	static void ApplyCompose_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* OverlayTexture, FRHITexture2D* DstRenderTarget, FRHITexture2D* DstTexture);

	// blend texture references
	static FTextureResource* SrcTexture;
	static FTextureRenderTargetResource* DstTexture;
	static FTextureRenderTargetResource* ResultTexture;
};

class FDirectProjectionVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FDirectProjectionVS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	/** Default constructor. */
	FDirectProjectionVS() 
	{
	}


public:
	/** Initialization constructor. */
	FDirectProjectionVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
};



template<uint32 ShaderType>
class TPicpBlurPostProcessPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TPicpBlurPostProcessPS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	/** Default constructor. */
	TPicpBlurPostProcessPS() 
	{
	}

	FShaderResourceParameter SrcTextureParameter;
	FShaderResourceParameter BilinearClampTextureSamplerParameter;
	FShaderParameter SampleOffsetParameter;
	FShaderParameter KernelRadiusParameter;


public:

	/** Initialization constructor. */
	TPicpBlurPostProcessPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		SrcTextureParameter.Bind(Initializer.ParameterMap, TEXT("SrcTexture"));
		BilinearClampTextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("BilinearClampTextureSampler"));
		SampleOffsetParameter.Bind(Initializer.ParameterMap, TEXT("SampleOffset"));
		KernelRadiusParameter.Bind(Initializer.ParameterMap, TEXT("KernelRadius"));
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		switch (static_cast<EPicpBlurPostProcessShaderType>(ShaderType))
		{
		case EPicpBlurPostProcessShaderType::Dilate:
			OutEnvironment.SetDefine(TEXT("BLUR_DILATE"), true);
			break;

		case EPicpBlurPostProcessShaderType::Gaussian:
			OutEnvironment.SetDefine(TEXT("BLUR_GAUSSIAN"), true);
			break;

		default:
			break;
		}
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SourceTexture, FVector2D SampleOffset, int KernelRadius)
	{
		FRHIPixelShader* ShaderRHI = GetPixelShader();

		SetSamplerParameter(RHICmdList, ShaderRHI, BilinearClampTextureSamplerParameter, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		SetTextureParameter(RHICmdList, ShaderRHI, SrcTextureParameter, SourceTexture);
		SetShaderValue(RHICmdList, ShaderRHI, SampleOffsetParameter, SampleOffset);
		SetShaderValue(RHICmdList, ShaderRHI, KernelRadiusParameter, KernelRadius);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		Ar << SrcTextureParameter;
		Ar << BilinearClampTextureSamplerParameter;
		Ar << SampleOffsetParameter;
		Ar << KernelRadiusParameter;

		return bShaderHasOutdatedParameters;
	}
};
typedef TPicpBlurPostProcessPS<(int)EPicpBlurPostProcessShaderType::Gaussian>   FPicpBlurPostProcessDefaultPS;
typedef TPicpBlurPostProcessPS<(int)EPicpBlurPostProcessShaderType::Dilate>     FPicpBlurPostProcessDilatePS;


//Compose
class FDirectComposePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FDirectComposePS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	/** Default constructor. */
	FDirectComposePS() 
	{
	}

	FShaderResourceParameter SrcTextureParameter;
	FShaderResourceParameter BilinearClampTextureSamplerParameter;

public:

	/** Initialization constructor. */
	FDirectComposePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		SrcTextureParameter.Bind(Initializer.ParameterMap, TEXT("SrcTexture"));
		BilinearClampTextureSamplerParameter.Bind(Initializer.ParameterMap, TEXT("BilinearClampTextureSampler"));
	}

	void SetParameters(FRHICommandListImmediate& RHICmdList, FTextureRHIRef SourceTexture)
	{
		FRHIPixelShader* ShaderRHI = GetPixelShader();

		SetSamplerParameter(RHICmdList, ShaderRHI, BilinearClampTextureSamplerParameter, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());
		SetTextureParameter(RHICmdList, ShaderRHI, SrcTextureParameter, SourceTexture);
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);

		Ar << SrcTextureParameter;
		Ar << BilinearClampTextureSamplerParameter;

		return bShaderHasOutdatedParameters;
	}
};
