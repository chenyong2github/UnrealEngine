// Copyright Epic Games, Inc. All Rights Reserved.
#include "MaterialExpressionRamp4.h"
#include "MaterialCompiler.h"

#define LOCTEXT_NAMESPACE "MaterialExpressionRamp4"

UMaterialExpressionRamp4::UMaterialExpressionRamp4(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_MaterialX;
		FText NAME_Utility;
		FConstructorStatics()
			: NAME_MaterialX(LOCTEXT("MaterialX", "MaterialX"))
			, NAME_Utility(LOCTEXT("Procedural", "Procedural"))
		{}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_MaterialX);
	MenuCategories.Add(ConstructorStatics.NAME_Utility);
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionRamp4::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
	if(!A.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing A input"));
	}

	if(!B.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing B input"));
	}
	
	if(!C.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing C input"));
	}

	if(!D.GetTracedInput().Expression)
	{
		return Compiler->Errorf(TEXT("Missing D input"));
	}

	int32 IndexCoordinates = Coordinates.GetTracedInput().Expression ? Coordinates.Compile(Compiler) : Compiler->TextureCoordinate(ConstCoordinate, false, false);
	int32 IndexValueTL = A.Compile(Compiler);
	int32 IndexValueTR = B.Compile(Compiler);
	int32 IndexValueBL = C.Compile(Compiler);
	int32 IndexValueBR = D.Compile(Compiler);

	int32 TexClamp = Compiler->Saturate(IndexCoordinates);

	int32 S = Compiler->ComponentMask(TexClamp, 1, 0, 0, 0);
	int32 T = Compiler->ComponentMask(TexClamp, 0, 1, 0, 0);
	int32 MixTop = Compiler->Lerp(IndexValueTL, IndexValueTR, S);
	int32 MixBot = Compiler->Lerp(IndexValueBL, IndexValueBR, S);

	return Compiler->Lerp(MixBot, MixTop, T);
}

void UMaterialExpressionRamp4::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(TEXT("Ramp4"));
}

void UMaterialExpressionRamp4::GetExpressionToolTip(TArray<FString>& OutToolTip)
{
	ConvertToMultilineToolTip(TEXT("A 4-corner bilinear value ramp."), 40, OutToolTip);
}
#endif

#undef LOCTEXT_NAMESPACE 