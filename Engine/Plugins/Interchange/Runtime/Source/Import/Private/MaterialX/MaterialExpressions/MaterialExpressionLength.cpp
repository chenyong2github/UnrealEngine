// Copyright Epic Games, Inc. All Rights Reserved.
#include "MaterialExpressionLength.h"
#include "MaterialCompiler.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MaterialExpressionLength)

#define LOCTEXT_NAMESPACE "MaterialExpressionMaterialXLength"

UMaterialExpressionMaterialXLength::UMaterialExpressionMaterialXLength(const FObjectInitializer& ObjectInitializer)
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
int32 UMaterialExpressionMaterialXLength::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!Input.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing MaterialX Length input"));
	}

	int32 Index = Input.Compile(Compiler);
	if(Compiler->GetType(Index) == MCT_Float)
	{
		// optimized
		return Compiler->Abs(Index);
	}

	return Compiler->Length(Index);
}

void UMaterialExpressionMaterialXLength::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("MaterialX Length"));
}
#endif

#undef LOCTEXT_NAMESPACE 