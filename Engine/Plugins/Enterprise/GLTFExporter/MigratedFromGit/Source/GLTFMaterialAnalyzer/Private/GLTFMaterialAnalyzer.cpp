// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFMaterialAnalyzer.h"
#include "GLTFMaterialAnalysis.h"
#include "GLTFMaterialBaking/Public/MaterialBakingStructures.h"
#include "Materials/HLSLMaterialTranslator.h"

void UGLTFMaterialAnalyzer::AnalyzeMaterialProperty(const UMaterialInterface* InMaterial, const EMaterialProperty& InProperty, const FString& InCustomOutput, FGLTFMaterialAnalysis& OutAnalysis)
{
	Property = InProperty;
	CustomOutput = InCustomOutput;
	Material = const_cast<UMaterialInterface*>(InMaterial);
	Analysis = &OutAnalysis;

	// NOTE: When analyzing custom outputs, the property *must* be set to MP_MAX or the compiler will refuse to compile the output
	const EMaterialProperty SafeProperty = InProperty == MP_CustomOutput ? MP_MAX : InProperty;

	int32 DummyNumTextureCoordinates; // Dummy value from built-in analysis not used since it's insufficient
	bool DummyRequiresVertexData; // Dummy value from built-in analysis not used since it's insufficient
	Super::AnalyzeMaterialProperty(SafeProperty, DummyNumTextureCoordinates, DummyRequiresVertexData);

	Property = MP_MAX;
	CustomOutput = {};
	Material = nullptr;
	Analysis = nullptr;
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

	FTranslatorHack* Translator = static_cast<FTranslatorHack*>(Compiler);
	int32 Result;

	if (Property == MP_CustomOutput)
	{
		UMaterialExpressionCustomOutput* CustomOutputExpression = GetCustomOutputExpression();
		Result = CustomOutputExpression != nullptr ? CustomOutputExpression->Compile(Compiler, 0) : INDEX_NONE;
	}
	else
	{
		Result = Material->CompilePropertyEx(Translator, AttributeID);
	}

	Analysis->TextureCoordinates = Translator->AllocatedUserTexCoords;
	Analysis->NumTextureCoordinates = 0;

	for (int32 TexCoordIndex = 0; TexCoordIndex < Translator->AllocatedUserTexCoords.Num(); TexCoordIndex++)
	{
		const bool IsTexCoordUsed = Translator->AllocatedUserTexCoords[TexCoordIndex];
		Analysis->NumTextureCoordinates += IsTexCoordUsed ? 1 : 0;
	}

	// TODO: investigate if we need to check more conditions to determine that vertex data is required
	Analysis->bRequiresVertexData =
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
