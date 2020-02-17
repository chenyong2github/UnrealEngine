// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "RenderResource.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "Rendering/RenderingCommon.h"
#include "RHIStaticStates.h"

extern EColorVisionDeficiency GSlateColorDeficiencyType;
extern int32 GSlateColorDeficiencySeverity;
extern bool GSlateColorDeficiencyCorrection;
extern bool GSlateShowColorDeficiencyCorrectionWithDeficiency;

/**
 * The vertex declaration for the slate vertex shader
 */
class FSlateVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	virtual ~FSlateVertexDeclaration() {}

	/** Initializes the vertex declaration RHI resource */
	virtual void InitRHI() override;

	/** Releases the vertex declaration RHI resource */
	virtual void ReleaseRHI() override;
};

/**
 * The vertex declaration for the slate instanced vertex shader
 */
class FSlateInstancedVertexDeclaration : public FSlateVertexDeclaration
{
public:
	virtual ~FSlateInstancedVertexDeclaration() {}
	
	/** Initializes the vertex declaration RHI resource */
	virtual void InitRHI() override;
};

class FSlateMaskingVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	virtual ~FSlateMaskingVertexDeclaration() {}

	/** Initializes the vertex declaration RHI resource */
	virtual void InitRHI() override;

	/** Releases the vertex declaration RHI resource */
	virtual void ReleaseRHI() override;
};

/** The slate Vertex shader representation */
class FSlateElementVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FSlateElementVS, Global);
public:
	/** Indicates that this shader should be cached */
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }

	/** Constructor.  Binds all parameters used by the shader */
	FSlateElementVS( const ShaderMetaType::CompiledShaderInitializerType& Initializer );

	FSlateElementVS() {}

	/** 
	 * Sets the view projection parameter
	 *
	 * @param InViewProjection	The ViewProjection matrix to use when this shader is bound 
	 */
	void SetViewProjection(FRHICommandList& RHICmdList, const FMatrix& InViewProjection );

	/** 
	 * Sets shader parameters for use in this shader
	 *
	 * @param ShaderParams	The shader params to be used
	 */
	void SetShaderParameters(FRHICommandList& RHICmdList, const FVector4& ShaderParams );

	/**
	 * Sets the vertical axis multiplier to use depending on graphics api
	 */
	void SetVerticalAxisMultiplier(FRHICommandList& RHICmdList, float InMultiplier);

	/** Serializes the shader data */
	//virtual bool Serialize( FArchive& Ar ) override;

private:
	/** ViewProjection parameter used by the shader */
	LAYOUT_FIELD(FShaderParameter, ViewProjection)
	/** Shader parmeters used by the shader */
	LAYOUT_FIELD(FShaderParameter, VertexShaderParams)
	/** Parameter used to determine if we need to swtich the vertical axis for opengl */
	LAYOUT_FIELD(FShaderParameter, SwitchVerticalAxisMultiplier)
};

/** 
 * Base class slate pixel shader for all elements
 */
class FSlateElementPS : public FGlobalShader
{
	DECLARE_TYPE_LAYOUT(FSlateElementPS, NonVirtual);
public:
	/** Indicates that this shader should be cached */
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return true; 
	}

	FSlateElementPS()
	{
	}

	/** Constructor.  Binds all parameters used by the shader */
	FSlateElementPS( const ShaderMetaType::CompiledShaderInitializerType& Initializer )
		: FGlobalShader( Initializer )
	{
		TextureParameter.Bind( Initializer.ParameterMap, TEXT("ElementTexture"));
		TextureParameterSampler.Bind( Initializer.ParameterMap, TEXT("ElementTextureSampler"));
		ShaderParams.Bind( Initializer.ParameterMap, TEXT("ShaderParams"));
		GammaAndAlphaValues.Bind( Initializer.ParameterMap,TEXT("GammaAndAlphaValues"));
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	/**
	 * Sets the texture used by this shader 
	 *
	 * @param Texture	Texture resource to use when this pixel shader is bound
	 * @param SamplerState	Sampler state to use when sampling this texture
	 */
	void SetTexture(FRHICommandList& RHICmdList, FRHITexture* InTexture, const FSamplerStateRHIRef SamplerState )
	{
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), TextureParameter, TextureParameterSampler, SamplerState, InTexture );
	}

	/**
	 * Sets shader params used by the shader
	 * 
	 * @param InShaderParams Shader params to use
	 */
	void SetShaderParams(FRHICommandList& RHICmdList, const FVector4& InShaderParams )
	{
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), ShaderParams, InShaderParams );
	}

	/**
	 * Sets the display gamma.
	 *
	 * @param DisplayGamma The display gamma to use
	 */
	void SetDisplayGammaAndInvertAlphaAndContrast(FRHICommandList& RHICmdList, float InDisplayGamma, float bInvertAlpha, float InContrast)
	{
		FVector4 Values( 2.2f / InDisplayGamma, 1.0f/InDisplayGamma, bInvertAlpha, InContrast);

		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), GammaAndAlphaValues, Values);
	}

private:
	
	/** Texture parameter used by the shader */
	LAYOUT_FIELD(FShaderResourceParameter, TextureParameter);
	LAYOUT_FIELD(FShaderResourceParameter, TextureParameterSampler);
	LAYOUT_FIELD(FShaderParameter, ShaderParams);
	LAYOUT_FIELD(FShaderParameter, GammaAndAlphaValues);
};

/** 
 * Pixel shader types for all elements
 */
template<ESlateShader ShaderType, bool bDrawDisabledEffect, bool bUseTextureAlpha=true>
class TSlateElementPS : public FSlateElementPS
{
	DECLARE_SHADER_TYPE( TSlateElementPS, Global );
public:

	TSlateElementPS()
	{
	}

	/** Constructor.  Binds all parameters used by the shader */
	TSlateElementPS( const ShaderMetaType::CompiledShaderInitializerType& Initializer )
		: FSlateElementPS( Initializer )
	{
	}


	/**
	 * Modifies the compilation of this shader
	 */
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		// Set defines based on what this shader will be used for
		OutEnvironment.SetDefine(TEXT("SHADER_TYPE"), (uint32)ShaderType);
		OutEnvironment.SetDefine(TEXT("DRAW_DISABLED_EFFECT"), (uint32)( bDrawDisabledEffect ? 1 : 0 ));
		OutEnvironment.SetDefine(TEXT("USE_TEXTURE_ALPHA"), (uint32)( bUseTextureAlpha ? 1 : 0 ));
		OutEnvironment.SetDefine(TEXT("USE_MATERIALS"), (uint32)0);

		FSlateElementPS::ModifyCompilationEnvironment( Parameters, OutEnvironment );
	}
};

/** 
 * Pixel shader for debugging Slate overdraw
 */
class FSlateDebugOverdrawPS : public FSlateElementPS
{	
	DECLARE_SHADER_TYPE( FSlateDebugOverdrawPS, Global );
public:
	/** Indicates that this shader should be cached */
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return true; 
	}

	FSlateDebugOverdrawPS()
	{
	}

	/** Constructor.  Binds all parameters used by the shader */
	FSlateDebugOverdrawPS( const ShaderMetaType::CompiledShaderInitializerType& Initializer )
		: FSlateElementPS( Initializer )
	{
	}
};

/** 
 * Pixel shader for debugging Slate overdraw
 */
class FSlateDebugBatchingPS : public FSlateElementPS
{	
	DECLARE_SHADER_TYPE(FSlateDebugBatchingPS, Global );
public:
	/** Indicates that this shader should be cached */
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return true; 
	}

	FSlateDebugBatchingPS()
	{
	}

	/** Constructor.  Binds all parameters used by the shader */
	FSlateDebugBatchingPS( const ShaderMetaType::CompiledShaderInitializerType& Initializer )
		: FSlateElementPS( Initializer )
	{
		BatchColor.Bind(Initializer.ParameterMap, TEXT("BatchColor"));
	}

	/**
	* Sets shader params used by the shader
	*
	* @param InShaderParams Shader params to use
	*/
	void SetBatchColor(FRHICommandList& RHICmdList, const FLinearColor& InBatchColor)
	{
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), BatchColor, InBatchColor);
	}

private:
	LAYOUT_FIELD(FShaderParameter, BatchColor);
};

const int32 MAX_BLUR_SAMPLES = 127;

class FSlatePostProcessBlurPS : public FSlateElementPS
{
	DECLARE_SHADER_TYPE(FSlatePostProcessBlurPS, Global);
public:
	/** Indicates that this shader should be cached */
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FSlatePostProcessBlurPS()
	{
	}

	/** Constructor.  Binds all parameters used by the shader */
	FSlatePostProcessBlurPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FSlateElementPS(Initializer)
	{
		BufferSizeAndDirection.Bind(Initializer.ParameterMap, TEXT("BufferSizeAndDirection"));
		WeightAndOffsets.Bind(Initializer.ParameterMap, TEXT("WeightAndOffsets"));
		SampleCount.Bind(Initializer.ParameterMap, TEXT("SampleCount"));
		UVBounds.Bind(Initializer.ParameterMap, TEXT("UVBounds"));
	}

	void SetBufferSizeAndDirection(FRHICommandList& RHICmdList, const FVector2D& InBufferSize, const FVector2D& InDir)
	{
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), BufferSizeAndDirection, FVector4(InBufferSize, InDir));
	}

	void SetWeightsAndOffsets(FRHICommandList& RHICmdList, const TArray<FVector4>& InWeightsAndOffsets, int32 NumSamples )
	{
		check(InWeightsAndOffsets.Num() <= MAX_BLUR_SAMPLES);
		SetShaderValueArray<FRHIPixelShader*, FVector4>(RHICmdList, RHICmdList.GetBoundPixelShader(), WeightAndOffsets, InWeightsAndOffsets.GetData(), InWeightsAndOffsets.Num() );
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), SampleCount, NumSamples);
	}

	void SetUVBounds(FRHICommandList& RHICmdList, const FVector4& InUVBounds)
	{
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), UVBounds, InUVBounds);
	}

private:
	LAYOUT_FIELD(FShaderParameter, BufferSizeAndDirection);
	LAYOUT_FIELD(FShaderParameter, WeightAndOffsets);
	LAYOUT_FIELD(FShaderParameter, SampleCount);
	LAYOUT_FIELD(FShaderParameter, UVBounds);
};


class FSlatePostProcessDownsamplePS : public FSlateElementPS
{
	DECLARE_SHADER_TYPE(FSlatePostProcessDownsamplePS, Global);
public:
	/** Indicates that this shader should be cached */
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FSlatePostProcessDownsamplePS()
	{
	}

	/** Constructor.  Binds all parameters used by the shader */
	FSlatePostProcessDownsamplePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FSlateElementPS(Initializer)
	{
		UVBounds.Bind(Initializer.ParameterMap, TEXT("UVBounds"));
	}

	void SetUVBounds(FRHICommandList& RHICmdList, const FVector4& InUVBounds)
	{
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), UVBounds, InUVBounds);
	}

private:
	LAYOUT_FIELD(FShaderParameter, UVBounds);
};


class FSlatePostProcessColorDeficiencyPS : public FSlateElementPS
{
	DECLARE_SHADER_TYPE(FSlatePostProcessColorDeficiencyPS, Global);
public:
	/** Indicates that this shader should be cached */
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FSlatePostProcessColorDeficiencyPS()
	{
	}

	/** Constructor.  Binds all parameters used by the shader */
	FSlatePostProcessColorDeficiencyPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FSlateElementPS(Initializer)
	{
		ColorVisionDeficiencyType.Bind(Initializer.ParameterMap, TEXT("ColorVisionDeficiencyType"));
		ColorVisionDeficiencySeverity.Bind(Initializer.ParameterMap, TEXT("ColorVisionDeficiencySeverity"));
		bCorrectDeficiency.Bind(Initializer.ParameterMap, TEXT("bCorrectDeficiency"));
		bSimulateCorrectionWithDeficiency.Bind(Initializer.ParameterMap, TEXT("bSimulateCorrectionWithDeficiency"));
	}

	void SetColorRules(FRHICommandList& RHICmdList, bool bCorrect, EColorVisionDeficiency DeficiencyType, int32 Severity)
	{
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), ColorVisionDeficiencyType, (float)DeficiencyType);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), ColorVisionDeficiencySeverity, (float)Severity);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), bCorrectDeficiency, bCorrect ? 1.0f : 0.0f);
	}

	void SetShowCorrectionWithDeficiency(FRHICommandList& RHICmdList, bool bShowCorrectionWithDeficiency)
	{
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), bSimulateCorrectionWithDeficiency, bShowCorrectionWithDeficiency ? 1.0f : 0.0f);
	}

private:
	LAYOUT_FIELD(FShaderParameter, ColorVisionDeficiencyType);
	LAYOUT_FIELD(FShaderParameter, ColorVisionDeficiencySeverity);
	LAYOUT_FIELD(FShaderParameter, bCorrectDeficiency);
	LAYOUT_FIELD(FShaderParameter, bSimulateCorrectionWithDeficiency);
};


class FSlateMaskingVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FSlateMaskingVS, Global);
public:
	/** Indicates that this shader should be cached */
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FSlateMaskingVS()
	{
	}

	/** Constructor.  Binds all parameters used by the shader */
	FSlateMaskingVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer);

	/**
	* Sets the view projection parameter
	*
	* @param InViewProjection	The ViewProjection matrix to use when this shader is bound
	*/
	void SetViewProjection(FRHICommandList& RHICmdList, const FMatrix& InViewProjection);

	/**
	 * Sets the vertical axis multiplier to use depending on graphics api
	 */
	void SetVerticalAxisMultiplier(FRHICommandList& RHICmdList, float InMultiplier);

	/**
	 * Sets the mask rect positions
	 */
	void SetMaskRect(FRHICommandList& RHICmdList, const FVector2D& TopLeft, const FVector2D& TopRight, const FVector2D& BotLeft, const FVector2D& BotRight);

	//virtual bool Serialize(FArchive& Ar) override;

private:
	/** Mask rect parameter */
	LAYOUT_FIELD(FShaderParameter, MaskRect)
	/** ViewProjection parameter used by the shader */
	LAYOUT_FIELD(FShaderParameter, ViewProjection)
	/** Parameter used to determine if we need to swtich the vertical axis for opengl */
	LAYOUT_FIELD(FShaderParameter, SwitchVerticalAxisMultiplier)
};

class FSlateMaskingPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FSlateMaskingPS, Global);
public:
	/** Indicates that this shader should be cached */
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FSlateMaskingPS()
	{
	}

	/** Constructor.  Binds all parameters used by the shader */
	FSlateMaskingPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
	}
};


#if WITH_EDITOR
// Pixel shader to convert UI from linear rec709 to PQ 2020 for HDR monitors
class FHDREditorConvertPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FHDREditorConvertPS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FHDREditorConvertPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		SceneTexture.Bind(Initializer.ParameterMap, TEXT("SceneTexture"));
		SceneSampler.Bind(Initializer.ParameterMap, TEXT("SceneSampler"));
		UILevel.Bind(Initializer.ParameterMap, TEXT("UILevel"));
	}
	FHDREditorConvertPS() {}

	void SetParameters(FRHICommandList& RHICmdList, FRHITexture* SceneTextureRHI)
	{
		static const auto CVarOutputDevice = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.HDR.Display.OutputDevice"));

		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), SceneTexture, SceneSampler, TStaticSamplerState<SF_Point>::GetRHI(), SceneTextureRHI);
		
		static auto CVarHDRNITLevel = IConsoleManager::Get().FindConsoleVariable(TEXT("Editor.HDRNITLevel"));
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), UILevel, CVarHDRNITLevel->GetFloat());
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static const TCHAR* GetSourceFilename()
	{
		return TEXT("/Engine/Private/CompositeUIPixelShader.usf");
	}

	static const TCHAR* GetFunctionName()
	{
		return TEXT("HDREditorConvert");
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, SceneTexture);
	LAYOUT_FIELD(FShaderResourceParameter, SceneSampler);
	LAYOUT_FIELD(FShaderParameter, UILevel);
};
#endif

/** The simple element vertex declaration. */
extern TGlobalResource<FSlateVertexDeclaration> GSlateVertexDeclaration;

/** The instanced simple element vertex declaration. */
extern TGlobalResource<FSlateInstancedVertexDeclaration> GSlateInstancedVertexDeclaration;

/** The vertex declaration for rendering stencil masks. */
extern TGlobalResource<FSlateMaskingVertexDeclaration> GSlateMaskingVertexDeclaration;
