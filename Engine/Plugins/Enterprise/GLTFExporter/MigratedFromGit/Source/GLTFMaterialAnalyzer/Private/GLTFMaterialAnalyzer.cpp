// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFMaterialAnalyzer.h"
#include "GLTFMaterialStatistics.h"
#include "GLTFMaterialBaking/Public/MaterialBakingStructures.h"
#include "Materials/HLSLMaterialTranslator.h"

void UGLTFMaterialAnalyzer::AnalyzeMaterialProperty(const UMaterialInterface* InMaterial, const FMaterialPropertyEx& InProperty, FGLTFMaterialStatistics& OutMaterialStatistics)
{
	Property = &InProperty;
	Material = const_cast<UMaterialInterface*>(InMaterial);
	MaterialStatistics = &OutMaterialStatistics;

	int32 NumTextureCoordinates;
	bool RequiresVertexData;

	// NOTE: When analyzing custom outputs, the property *must* be set to MP_MAX or the compiler will refuse to compile the output
	const EMaterialProperty TempProperty = InProperty.Type == MP_CustomOutput ? MP_MAX : InProperty.Type;

	UMaterialInterface::AnalyzeMaterialProperty(TempProperty, NumTextureCoordinates, RequiresVertexData);

	Property = nullptr;
	Material = nullptr;
	MaterialStatistics = nullptr;
}

FMaterialResource* UGLTFMaterialAnalyzer::GetMaterialResource(ERHIFeatureLevel::Type InFeatureLevel, EMaterialQualityLevel::Type QualityLevel)
{
	return Material->GetMaterialResource(InFeatureLevel, QualityLevel);
}

int32 UGLTFMaterialAnalyzer::CompilePropertyEx(FMaterialCompiler* Compiler, const FGuid& AttributeID)
{
	class FTranslatorHack : public FHLSLMaterialTranslator
	{
		using FHLSLMaterialTranslator::FHLSLMaterialTranslator;

		friend UGLTFMaterialAnalyzer;
	};

	int32 Result = INDEX_NONE;

	if (Property->Type == MP_CustomOutput)
	{
		const FString CustomOutput = Property->CustomOutput.ToString();

		for (UMaterialExpression* Expression : Material->GetMaterial()->Expressions)
		{
			UMaterialExpressionCustomOutput* CustomOutputExpression = Cast<UMaterialExpressionCustomOutput>(Expression);
			if (CustomOutputExpression && CustomOutputExpression->GetDisplayName() == CustomOutput)
			{
				// TODO: can we rely on OutputIndex always being 0?
				Result = CustomOutputExpression->Compile(Compiler, 0);
				break;
			}
		}
	}
	else
	{
		Result = Material->CompilePropertyEx(Compiler, AttributeID);
	}

	FTranslatorHack* Translator = static_cast<FTranslatorHack*>(Compiler);

	MaterialStatistics->bUsesSceneDepth = Translator->bUsesSceneDepth;
	MaterialStatistics->bNeedsParticlePosition = Translator->bNeedsParticlePosition;
	MaterialStatistics->bNeedsParticleVelocity = Translator->bNeedsParticleVelocity;
	MaterialStatistics->bNeedsParticleTime = Translator->bNeedsParticleTime;
	MaterialStatistics->bUsesParticleMotionBlur = Translator->bUsesParticleMotionBlur;
	MaterialStatistics->bNeedsParticleRandom = Translator->bNeedsParticleRandom;
	MaterialStatistics->bUsesSphericalParticleOpacity = Translator->bUsesSphericalParticleOpacity;
	MaterialStatistics->bUsesParticleSubUVs = Translator->bUsesParticleSubUVs;
	MaterialStatistics->bUsesLightmapUVs = Translator->bUsesLightmapUVs;
	MaterialStatistics->bUsesAOMaterialMask = Translator->bUsesAOMaterialMask;
	MaterialStatistics->bUsesSpeedTree = Translator->bUsesSpeedTree;
	MaterialStatistics->bNeedsWorldPositionExcludingShaderOffsets = Translator->bNeedsWorldPositionExcludingShaderOffsets;
	MaterialStatistics->bNeedsParticleSize = Translator->bNeedsParticleSize;
	MaterialStatistics->bNeedsSceneTexturePostProcessInputs = Translator->bNeedsSceneTexturePostProcessInputs;
	MaterialStatistics->bUsesAtmosphericFog = Translator->bUsesAtmosphericFog;
	MaterialStatistics->bUsesSkyAtmosphere = Translator->bUsesSkyAtmosphere;
	MaterialStatistics->bUsesVertexColor = Translator->bUsesVertexColor;
	MaterialStatistics->bUsesParticleColor = Translator->bUsesParticleColor;
	MaterialStatistics->bUsesParticleLocalToWorld = Translator->bUsesParticleLocalToWorld;
	MaterialStatistics->bUsesParticleWorldToLocal = Translator->bUsesParticleWorldToLocal;
	MaterialStatistics->bUsesVertexPosition = Translator->bUsesVertexPosition;
	MaterialStatistics->bUsesTransformVector = Translator->bUsesTransformVector;
	MaterialStatistics->bCompilingPreviousFrame = Translator->bCompilingPreviousFrame;
	MaterialStatistics->bOutputsBasePassVelocities = Translator->bOutputsBasePassVelocities;
	MaterialStatistics->bUsesPixelDepthOffset = Translator->bUsesPixelDepthOffset;
	MaterialStatistics->bUsesWorldPositionOffset = Translator->bUsesWorldPositionOffset;
	MaterialStatistics->bUsesEmissiveColor = Translator->bUsesEmissiveColor;
	MaterialStatistics->bUsesDistanceCullFade = Translator->bUsesDistanceCullFade;
	MaterialStatistics->bIsFullyRough = Translator->bIsFullyRough;
	MaterialStatistics->bAllowCodeChunkGeneration = Translator->bAllowCodeChunkGeneration;
	MaterialStatistics->bUsesPerInstanceCustomData = Translator->bUsesPerInstanceCustomData;
	MaterialStatistics->AllocatedUserTexCoords = Translator->AllocatedUserTexCoords;
	MaterialStatistics->AllocatedUserVertexTexCoords = Translator->AllocatedUserVertexTexCoords;
	MaterialStatistics->DynamicParticleParameterMask = Translator->DynamicParticleParameterMask;
	MaterialStatistics->ShadingModelsFromCompilation = Translator->ShadingModelsFromCompilation;

	// TODO: investigate if we need to check more conditions to determine that vertex data is required
	MaterialStatistics->bRequiresVertexData =
		Translator->bUsesVertexColor ||
		Translator->bUsesTransformVector ||
		Translator->bNeedsWorldPositionExcludingShaderOffsets ||
		Translator->bUsesAOMaterialMask ||
		Translator->bUsesVertexPosition;

	return Result;
}

bool UGLTFMaterialAnalyzer::IsPropertyActive(EMaterialProperty InProperty) const
{
	return Material->IsPropertyActive(InProperty);
}
