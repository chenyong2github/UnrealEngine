// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShadowRendering.h"
#include "Engine/Engine.h"

/*=============================================================================
	SceneOcclusion.h
=============================================================================*/

/**
* A vertex shader for rendering a texture on a simple element.
*/
class FOcclusionQueryVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FOcclusionQueryVS,Global);
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1); }

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("OUTPUT_GAMMA_SPACE"), IsMobileHDR() == false);
	}

	FOcclusionQueryVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
			StencilingGeometryParameters.Bind(Initializer.ParameterMap);
			ViewId.Bind(Initializer.ParameterMap, TEXT("ViewId"));
	}

	FOcclusionQueryVS() {}

	void SetParametersWithBoundingSphere(FRHICommandList& RHICmdList, const FViewInfo& View, const FSphere& BoundingSphere)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundVertexShader(), View.ViewUniformBuffer);

		FVector4 StencilingSpherePosAndScale;
		StencilingGeometry::GStencilSphereVertexBuffer.CalcTransform(StencilingSpherePosAndScale, BoundingSphere, View.ViewMatrices.GetPreViewTranslation());
		StencilingGeometryParameters.Set(RHICmdList, this, StencilingSpherePosAndScale);

		if (GEngine && GEngine->StereoRenderingDevice)
		{
			SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), ViewId, GEngine->StereoRenderingDevice->GetViewIndexForPass(View.StereoPass));
		}
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundVertexShader(),View.ViewUniformBuffer);

		// Don't transform if rendering frustum
		StencilingGeometryParameters.Set(RHICmdList, this, FVector4(0,0,0,1));

		if (GEngine && GEngine->StereoRenderingDevice)
		{
			SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), ViewId, GEngine->StereoRenderingDevice->GetViewIndexForPass(View.StereoPass));
		}
	}

private:
	LAYOUT_FIELD(FStencilingGeometryShaderParameters, StencilingGeometryParameters)
	LAYOUT_FIELD(FShaderParameter, ViewId)
};

/**
 * A pixel shader for rendering a texture on a simple element.
 */
class FOcclusionQueryPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FOcclusionQueryPS, Global);
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::ES3_1); }

	FOcclusionQueryPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer) {}
	FOcclusionQueryPS() {}
};

