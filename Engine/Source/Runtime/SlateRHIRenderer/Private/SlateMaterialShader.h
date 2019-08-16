// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/RenderingCommon.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "MaterialShaderType.h"
#include "MaterialShader.h"

class FSlateMaterialShaderVS : public FMaterialShader
{
public:
	FSlateMaterialShaderVS() {}
	FSlateMaterialShaderVS(const FMaterialShaderType::CompiledShaderInitializerType& Initializer);

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters);

	/** 
	 * Sets the view projection parameter
	 *
	 * @param InViewProjection	The ViewProjection matrix to use when this shader is bound 
	 */
	void SetViewProjection(FRHICommandList& RHICmdList, const FMatrix& InViewProjection );

	void SetMaterialShaderParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FMaterialRenderProxy* MaterialRenderProxy, const FMaterial* Material);

	/**
	 * Sets the vertical axis multiplier to use depending on graphics api
	 */
	void SetVerticalAxisMultiplier(FRHICommandList& RHICmdList, float InMultiplier);

	/** Serializes the shader data */
	virtual bool Serialize( FArchive& Ar ) override;
private:
	/** ViewProjection parameter used by the shader */
	FShaderParameter ViewProjection;
	/** Parameter used to determine if we need to swtich the vertical axis for opengl */
	FShaderParameter SwitchVerticalAxisMultiplier;
};

class FSlateMaterialShaderPS : public FMaterialShader
{
public:

	/** Only compile shaders used with UI. */
	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters);

	/** Modifies the compilation of this shader. */
	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	FSlateMaterialShaderPS() {}
	FSlateMaterialShaderPS(const FMaterialShaderType::CompiledShaderInitializerType& Initializer);

	void SetBlendState(FGraphicsPipelineStateInitializer& GraphicsPSOInit, const FMaterial* Material);

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FMaterialRenderProxy* MaterialRenderProxy, const FMaterial* Material, const FVector4& InShaderParams);

	void SetDisplayGammaAndContrast(FRHICommandList& RHICmdList, float InDisplayGamma, float InContrast);

	void SetAdditionalTexture( FRHICommandList& RHICmdList, FRHITexture* InTexture, const FSamplerStateRHIRef SamplerState );

	virtual bool Serialize(FArchive& Ar) override;

private:
	FShaderParameter GammaAndAlphaValues;
	FShaderParameter ShaderParams;
	/** Extra texture (like a font atlas) to be used in addition to any material textures */
	FShaderResourceParameter TextureParameterSampler;
	FShaderResourceParameter AdditionalTextureParameter;
};

template<bool bUseInstancing>
class TSlateMaterialShaderVS : public FSlateMaterialShaderVS
{
public:
	DECLARE_SHADER_TYPE(TSlateMaterialShaderVS,Material);

	TSlateMaterialShaderVS() { }

	TSlateMaterialShaderVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FSlateMaterialShaderVS( Initializer )
	{ }
	
	/** Only compile shaders used with UI. */
	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		return FSlateMaterialShaderVS::ShouldCompilePermutation(Parameters);
	}

	/** Modifies the compilation of this shader. */
	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FSlateMaterialShaderVS::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("USE_SLATE_INSTANCING"), (uint32)( bUseInstancing ? 1 : 0 ));
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		return FSlateMaterialShaderVS::Serialize( Ar );
	}
};

template<ESlateShader ShaderType,bool bDrawDisabledEffect> 
class TSlateMaterialShaderPS : public FSlateMaterialShaderPS
{
public:
	DECLARE_SHADER_TYPE(TSlateMaterialShaderPS,Material);

	TSlateMaterialShaderPS() { }

	TSlateMaterialShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FSlateMaterialShaderPS( Initializer )
	{ }
	
	/** Only compile shaders used with UI. */
	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		return FSlateMaterialShaderPS::ShouldCompilePermutation(Parameters);
	}

	/** Modifies the compilation of this shader. */
	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FSlateMaterialShaderPS::ModifyCompilationEnvironment(Parameters,OutEnvironment);

		OutEnvironment.SetDefine(TEXT("SHADER_TYPE"), (uint32)ShaderType);
		OutEnvironment.SetDefine(TEXT("DRAW_DISABLED_EFFECT"), (uint32)(bDrawDisabledEffect ? 1 : 0));
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		return FSlateMaterialShaderPS::Serialize( Ar );
	}
};
