// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialExpressionLogarithm.h"
#include "MaterialCompiler.h"
#include "MaterialHLSLGenerator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MaterialExpressionLogarithm)

#define LOCTEXT_NAMESPACE "MaterialExpressionMaterialXLogarithm"

UMaterialExpressionMaterialXLogarithm::UMaterialExpressionMaterialXLogarithm(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_MaterialX;
		FConstructorStatics()
			: NAME_MaterialX(LOCTEXT("MaterialX", "MaterialX"))
		{}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_MaterialX);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionMaterialXLogarithm::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing MaterialX Ln input"));
	}

	const int32 Log10Euler = Compiler->Constant(0.4342945);

	return Compiler->Div(Compiler->Logarithm10(Input.Compile(Compiler)), Log10Euler);
}

void UMaterialExpressionMaterialXLogarithm::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("MaterialX Ln"));
}
#endif

#undef LOCTEXT_NAMESPACE 