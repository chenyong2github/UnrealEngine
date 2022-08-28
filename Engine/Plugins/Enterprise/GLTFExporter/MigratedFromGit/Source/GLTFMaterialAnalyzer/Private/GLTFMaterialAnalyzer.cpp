// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFMaterialAnalyzer.h"
#include "GLTFMaterialStatistics.h"
#include "GLTFMaterialBaking/Public/MaterialBakingStructures.h"
#include "Materials/HLSLMaterialTranslator.h"

void UGLTFMaterialAnalyzer::AnalyzeMaterialProperty(const UMaterialInterface* InMaterial, const EMaterialProperty& InProperty, const FString& InCustomOutput, FGLTFMaterialStatistics& OutMaterialStatistics)
{
	Property = InProperty;
	CustomOutput = InCustomOutput;
	Material = const_cast<UMaterialInterface*>(InMaterial);
	MaterialStatistics = &OutMaterialStatistics;

	// NOTE: When analyzing custom outputs, the property *must* be set to MP_MAX or the compiler will refuse to compile the output
	const EMaterialProperty SafeProperty = InProperty == MP_CustomOutput ? MP_MAX : InProperty;

	int32 DummyNumTextureCoordinates; // Dummy value from built-in analysis not used since it's insufficient
	bool DummyRequiresVertexData; // Dummy value from built-in analysis not used since it's insufficient
	Super::AnalyzeMaterialProperty(SafeProperty, DummyNumTextureCoordinates, DummyRequiresVertexData);

	Property = MP_MAX;
	CustomOutput = {};
	Material = nullptr;
	MaterialStatistics = nullptr;
}

UMaterialExpressionCustomOutput* UGLTFMaterialAnalyzer::GetCustomOutputExpression() const
{
	for (UMaterialExpression* Expression : Material->GetMaterial()->Expressions)
	{
		UMaterialExpressionCustomOutput* CustomOutputExpression = Cast<UMaterialExpressionCustomOutput>(Expression);
		if (CustomOutputExpression != nullptr && CustomOutputExpression->GetDisplayName() == CustomOutput)
		{
			return CustomOutputExpression;
		}
	}

	return nullptr;
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

	int32 Result;

	if (Property == MP_CustomOutput)
	{
		UMaterialExpressionCustomOutput* CustomOutputExpression = GetCustomOutputExpression();
		Result = CustomOutputExpression != nullptr ? CustomOutputExpression->Compile(Compiler, 0) : INDEX_NONE;
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
