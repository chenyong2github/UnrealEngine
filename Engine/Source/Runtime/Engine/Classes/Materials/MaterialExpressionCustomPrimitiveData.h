// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionCustomPrimitiveData.generated.h"

USTRUCT()
struct FPrimitiveDataIndex
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category=MaterialExpression)
	FString CustomDesc;
#endif

	UPROPERTY(EditAnywhere, Category=MaterialExpression, meta=(ClampMin="0"))
	uint8 PrimitiveDataIndex;
};

UCLASS(collapsecategories, hidecategories = Object)
class UMaterialExpressionCustomPrimitiveData: public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
	//~ Begin UMaterialExpression Interface
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UMaterialExpression Interface
#endif

	/** Custom index into the custom data for each pin, also includes custom descriptions for each custom data pin only for making things more readable */
	UPROPERTY(EditAnywhere, Category=MaterialExpression)
	TArray<FPrimitiveDataIndex> CustomIndices;
};