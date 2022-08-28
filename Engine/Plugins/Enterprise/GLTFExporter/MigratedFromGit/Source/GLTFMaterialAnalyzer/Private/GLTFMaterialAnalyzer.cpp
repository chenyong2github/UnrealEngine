// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFMaterialAnalyzer.h"
#include "GLTFMaterialAnalysis.h"
#include "GLTFMaterialBaking/Public/MaterialBakingStructures.h"
#include "Materials/HLSLMaterialTranslator.h"

UGLTFMaterialAnalyzer::UGLTFMaterialAnalyzer()
{
	ResetToDefaults();
}

void UGLTFMaterialAnalyzer::ResetToDefaults()
{
	Property = MP_MAX;
	CustomOutput = {};
	Material = nullptr;
	Analysis = nullptr;
}

void UGLTFMaterialAnalyzer::AnalyzeMaterialPropertyEx(const UMaterialInterface* InMaterial, const EMaterialProperty& InProperty, const FString& InCustomOutput, FGLTFMaterialAnalysis& OutAnalysis)
{
	// TODO: use a shared UGLTFMaterialAnalyzer instance instead of creating a new one for each invocation
	UGLTFMaterialAnalyzer* Analyzer = NewObject<UGLTFMaterialAnalyzer>();

	Analyzer->Property = InProperty;
	Analyzer->CustomOutput = InCustomOutput;
	Analyzer->Material = const_cast<UMaterialInterface*>(InMaterial);
	Analyzer->Analysis = &OutAnalysis;

	// NOTE: When analyzing custom outputs, the property *must* be set to MP_MAX or the compiler will refuse to compile the output
	const EMaterialProperty SafeProperty = InProperty == MP_CustomOutput ? MP_MAX : InProperty;

	int32 DummyNumTextureCoordinates; // Dummy value from built-in analysis not used since it's insufficient
	bool DummyRequiresVertexData; // Dummy value from built-in analysis not used since it's insufficient
	Analyzer->AnalyzeMaterialProperty(SafeProperty, DummyNumTextureCoordinates, DummyRequiresVertexData);

	Analyzer->ResetToDefaults();
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
	return Material != nullptr ? Material->GetMaterialResource(InFeatureLevel, QualityLevel) : nullptr;
}

int32 UGLTFMaterialAnalyzer::CompilePropertyEx(FMaterialCompiler* Compiler, const FGuid& AttributeID)
{
	if (Material == nullptr)
	{
		return INDEX_NONE;
	}

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
		if (CustomOutputExpression == nullptr)
		{
			return INDEX_NONE;
		}

		Result = CustomOutputExpression->Compile(Compiler, 0);
	}
	else
	{
		Result = Material->CompilePropertyEx(Translator, AttributeID);
	}

	Analysis->TextureCoordinates = Translator->AllocatedUserTexCoords;

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
	return Material != nullptr && Material->IsPropertyActive(InProperty);
}
