// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionSkinningVertexOffsets.generated.h"

UCLASS(collapsecategories, hidecategories=Object, MinimalAPI)
class UE_DEPRECATED(4.26, "UMaterialExpressionSkinningVertexOffsets has been deprecated. Support will be dropped in the future.") UMaterialExpressionSkinningVertexOffsets : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual void GetExpressionToolTip(TArray<FString>& OutToolTip) override;
	virtual FText GetKeywords() const override {return FText::FromString(TEXT("pre post delta offset"));}
#endif
	//~ End UMaterialExpression Interface
};
