// Copyright Epic Games, Inc. All Rights Reserved.
#include "MaterialExpressionExponential.h"
#include "MaterialCompiler.h"
#include "MaterialHLSLGenerator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MaterialExpressionExponential)

#define LOCTEXT_NAMESPACE "MaterialExpressionMaterialXExponential"

UMaterialExpressionMaterialXExponential::UMaterialExpressionMaterialXExponential(const FObjectInitializer& ObjectInitializer)
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
int32 UMaterialExpressionMaterialXExponential::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing MaterialX Exponential input"));
	}

	int32 Euler = Compiler->Constant(2.7182818);

	return Compiler->Power(Euler, Input.Compile(Compiler));
}

void UMaterialExpressionMaterialXExponential::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("MaterialX Exponential"));
}
#endif

#undef LOCTEXT_NAMESPACE 