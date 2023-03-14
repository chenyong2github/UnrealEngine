// Copyright Epic Games, Inc. All Rights Reserved.
#include "MaterialExpressionLength.h"
#include "MaterialCompiler.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MaterialExpressionLength)

#define LOCTEXT_NAMESPACE "MaterialExpressionLength"

UMaterialExpressionLength::UMaterialExpressionLength(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_MaterialX;
		FText NAME_Math;
		FText NAME_VectorOps;
		FConstructorStatics()
			: NAME_MaterialX(LOCTEXT("MaterialX", "MaterialX"))
			, NAME_Math(LOCTEXT("Math", "Math"))
			, NAME_VectorOps(LOCTEXT("VectorOps", "VectorOps"))
		{}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_MaterialX);
	MenuCategories.Add(ConstructorStatics.NAME_Math);
	MenuCategories.Add(ConstructorStatics.NAME_VectorOps);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionLength::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing Length input"));
	}

	return Compiler->Length(Input.Compile(Compiler));
}

void UMaterialExpressionLength::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Length"));
}
#endif

#undef LOCTEXT_NAMESPACE 