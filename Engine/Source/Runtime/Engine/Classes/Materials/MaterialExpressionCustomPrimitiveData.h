// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionCustomPrimitiveData.generated.h"

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

	/** Custom descriptions for each custom data pin, only for making things more readable, don't rely on this data for run-time things */
	UPROPERTY(EditAnywhere, Category=MaterialExpression)
	TArray<FString> CustomDescs;
#endif
};