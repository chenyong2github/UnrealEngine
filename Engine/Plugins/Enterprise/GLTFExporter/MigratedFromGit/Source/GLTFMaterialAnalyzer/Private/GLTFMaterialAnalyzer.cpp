// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFMaterialAnalyzer.h"
#include "Materials/HLSLMaterialTranslator.h"

void UGLTFMaterialAnalyzer::AnalyzeMaterialProperty(const UMaterialInterface* InMaterial, EMaterialProperty InProperty)
{
	Material = const_cast<UMaterialInterface*>(InMaterial);

	int32 NumTextureCoordinates;
	bool RequiresVertexData;
	UMaterialInterface::AnalyzeMaterialProperty(InProperty, NumTextureCoordinates, RequiresVertexData);
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

	const int32 Result = Material->CompilePropertyEx(Compiler, AttributeID);
	FTranslatorHack* Translator = static_cast<FTranslatorHack*>(Compiler);

	bUsesSceneDepth = Translator->bUsesSceneDepth;
	bNeedsParticlePosition = Translator->bNeedsParticlePosition;
	bNeedsParticleVelocity = Translator->bNeedsParticleVelocity;
	bNeedsParticleTime = Translator->bNeedsParticleTime;
	bUsesParticleMotionBlur = Translator->bUsesParticleMotionBlur;
	bNeedsParticleRandom = Translator->bNeedsParticleRandom;
	bUsesSphericalParticleOpacity = Translator->bUsesSphericalParticleOpacity;
	bUsesParticleSubUVs = Translator->bUsesParticleSubUVs;
	bUsesLightmapUVs = Translator->bUsesLightmapUVs;
	bUsesAOMaterialMask = Translator->bUsesAOMaterialMask;
	bUsesSpeedTree = Translator->bUsesSpeedTree;
	bNeedsWorldPositionExcludingShaderOffsets = Translator->bNeedsWorldPositionExcludingShaderOffsets;
	bNeedsParticleSize = Translator->bNeedsParticleSize;
	bNeedsSceneTexturePostProcessInputs = Translator->bNeedsSceneTexturePostProcessInputs;
	bUsesAtmosphericFog = Translator->bUsesAtmosphericFog;
	bUsesSkyAtmosphere = Translator->bUsesSkyAtmosphere;
	bUsesVertexColor = Translator->bUsesVertexColor;
	bUsesParticleColor = Translator->bUsesParticleColor;
	bUsesParticleLocalToWorld = Translator->bUsesParticleLocalToWorld;
	bUsesParticleWorldToLocal = Translator->bUsesParticleWorldToLocal;
	bUsesVertexPosition = Translator->bUsesVertexPosition;
	bUsesTransformVector = Translator->bUsesTransformVector;
	bCompilingPreviousFrame = Translator->bCompilingPreviousFrame;
	bOutputsBasePassVelocities = Translator->bOutputsBasePassVelocities;
	bUsesPixelDepthOffset = Translator->bUsesPixelDepthOffset;
	bUsesWorldPositionOffset = Translator->bUsesWorldPositionOffset;
	bUsesEmissiveColor = Translator->bUsesEmissiveColor;
	bUsesDistanceCullFade = Translator->bUsesDistanceCullFade;
	bIsFullyRough = Translator->bIsFullyRough;
	bAllowCodeChunkGeneration = Translator->bAllowCodeChunkGeneration;
	bUsesPerInstanceCustomData = Translator->bUsesPerInstanceCustomData;
	AllocatedUserTexCoords = Translator->AllocatedUserTexCoords;
	AllocatedUserVertexTexCoords = Translator->AllocatedUserVertexTexCoords;
	DynamicParticleParameterMask = Translator->DynamicParticleParameterMask;
	ShadingModelsFromCompilation = Translator->ShadingModelsFromCompilation;

	return Result;
}

bool UGLTFMaterialAnalyzer::IsPropertyActive(EMaterialProperty InProperty) const
{
	return Material->IsPropertyActive(InProperty);
}
