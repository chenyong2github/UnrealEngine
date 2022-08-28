// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Materials/MaterialInterface.h"
#include "GLTFMaterialAnalyzer.generated.h"

struct FGLTFMaterialAnalysis;

UCLASS()
class GLTFMATERIALANALYZER_API UGLTFMaterialAnalyzer : public UMaterialInterface
{
	GENERATED_BODY()

public:

	void AnalyzeMaterialProperty(const UMaterialInterface* InMaterial, const EMaterialProperty& InProperty, const FString& InCustomOutput, FGLTFMaterialAnalysis& OutAnalysis);

private:

	UMaterialExpressionCustomOutput* GetCustomOutputExpression() const;

	virtual FMaterialResource* GetMaterialResource(ERHIFeatureLevel::Type InFeatureLevel, EMaterialQualityLevel::Type QualityLevel) override;

	virtual int32 CompilePropertyEx(FMaterialCompiler* Compiler, const FGuid& AttributeID) override;

	virtual bool IsPropertyActive(EMaterialProperty InProperty) const override;

	EMaterialProperty Property;
	FString CustomOutput;

	// ReSharper disable once CppUE4ProbableMemoryIssuesWithUObject
	UMaterialInterface* Material;

	FGLTFMaterialAnalysis* Analysis;
};
