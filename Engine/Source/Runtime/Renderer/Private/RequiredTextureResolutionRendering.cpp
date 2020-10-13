// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
RequiredTextureResolutionRendering.cpp: Contains definitions for rendering the viewmode.
=============================================================================*/

#include "RequiredTextureResolutionRendering.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

IMPLEMENT_MATERIAL_SHADER_TYPE(,FRequiredTextureResolutionPS,TEXT("/Engine/Private/RequiredTextureResolutionPixelShader.usf"),TEXT("Main"),SF_Pixel);

void FRequiredTextureResolutionInterface::GetDebugViewModeShaderBindings(
	const FDebugViewModePS& ShaderBase,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT Material,
	EDebugViewShaderMode DebugViewMode,
	const FVector& ViewOrigin,
	int32 VisualizeLODIndex,
	int32 VisualizeElementIndex,
	int32 NumVSInstructions,
	int32 NumPSInstructions,
	int32 ViewModeParam,
	FName ViewModeParamName,
	FMeshDrawSingleShaderBindings& ShaderBindings
) const
{
	const FRequiredTextureResolutionPS& Shader = static_cast<const FRequiredTextureResolutionPS&>(ShaderBase);
	int32 AnalysisIndex = INDEX_NONE;
	int32 TextureResolution = 64;
	FMaterialRenderContext MaterialContext(&MaterialRenderProxy, Material, nullptr);
	const FUniformExpressionSet& UniformExpressions = Material.GetUniformExpressions();
	if (ViewModeParam != INDEX_NONE && ViewModeParamName == NAME_None) // If displaying texture per texture indices
	{
		AnalysisIndex = ViewModeParam;

		for (int32 ParameterIndex = 0; ParameterIndex < UniformExpressions.GetNumTextures(EMaterialTextureParameterType::Standard2D); ++ParameterIndex)
		{
			const FMaterialTextureParameterInfo& Parameter = UniformExpressions.GetTextureParameter(EMaterialTextureParameterType::Standard2D, ParameterIndex);
			if (Parameter.TextureIndex == ViewModeParam)
			{
				const UTexture* Texture = nullptr;
				UniformExpressions.GetTextureValue(EMaterialTextureParameterType::Standard2D, ParameterIndex, MaterialContext, Material, Texture);
				if (Texture && Texture->Resource)
				{
					if (Texture->IsStreamable())
					{
						TextureResolution = 1 << (Texture->Resource->GetCurrentMipCount() - 1);
					}
					else
					{
						TextureResolution = FMath::Max(Texture->Resource->GetSizeX(), Texture->Resource->GetSizeY());
					}
				}
			}
		}
	}
	else if (ViewModeParam != INDEX_NONE) // Otherwise show only texture matching the given name
	{
		AnalysisIndex = 1024; // Make sure not to find anything by default.
		for (int32 ParameterIndex = 0; ParameterIndex < UniformExpressions.GetNumTextures(EMaterialTextureParameterType::Standard2D); ++ParameterIndex)
		{
			const UTexture* Texture = nullptr;
			UniformExpressions.GetTextureValue(EMaterialTextureParameterType::Standard2D, ParameterIndex, MaterialContext, Material, Texture);
			if (Texture && Texture->Resource && Texture->GetFName() == ViewModeParamName)
			{
				if (Texture->IsStreamable())
				{
					const FMaterialTextureParameterInfo& Parameter = UniformExpressions.GetTextureParameter(EMaterialTextureParameterType::Standard2D, ParameterIndex);
					AnalysisIndex = Parameter.TextureIndex;
					TextureResolution = 1 << (Texture->Resource->GetCurrentMipCount() - 1);
				}
				else
				{
					TextureResolution = FMath::Max(Texture->Resource->GetSizeX(), Texture->Resource->GetSizeY());
				}
			}
		}
	}

	ShaderBindings.Add(Shader.AnalysisParamsParameter, FIntPoint(AnalysisIndex, TextureResolution));
	ShaderBindings.Add(Shader.PrimitiveAlphaParameter, (!PrimitiveSceneProxy || PrimitiveSceneProxy->IsSelected()) ? 1.f : .2f);
}


#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
