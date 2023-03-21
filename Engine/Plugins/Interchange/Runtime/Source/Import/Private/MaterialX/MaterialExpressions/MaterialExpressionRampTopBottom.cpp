// Copyright Epic Games, Inc. All Rights Reserved.
#include "MaterialExpressionRampTopBottom.h"
#include "MaterialCompiler.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MaterialExpressionRampTopBottom)

#define LOCTEXT_NAMESPACE "MaterialExpressionRampLeftRight"

UMaterialExpressionRampTopBottom::UMaterialExpressionRampTopBottom(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_MaterialX;
		FText NAME_Procedural;
		FConstructorStatics()
			: NAME_MaterialX(LOCTEXT("MaterialX", "MaterialX"))
			, NAME_Procedural(LOCTEXT("Procedural", "Procedural"))
		{}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_MaterialX);
	MenuCategories.Add(ConstructorStatics.NAME_Procedural);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionRampTopBottom::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!A.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing A input"));
	}

	if(!B.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing B input"));
	}

	int32 CoordinateIndex = Coordinates.GetTracedInput().Expression ? Coordinates.Compile(Compiler) : Compiler->TextureCoordinate(ConstCoordinate, false, false);

	return Compiler->Lerp(B.Compile(Compiler), A.Compile(Compiler), Compiler->Saturate(Compiler->ComponentMask(CoordinateIndex, false, true, false, false)));
}

void UMaterialExpressionRampTopBottom::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("RampTB"));
}
#endif

#undef LOCTEXT_NAMESPACE 