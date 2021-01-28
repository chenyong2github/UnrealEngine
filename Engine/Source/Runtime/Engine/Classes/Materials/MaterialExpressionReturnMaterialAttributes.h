// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MaterialExpressionIO.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionReturnMaterialAttributes.generated.h"

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object, meta = (MaterialControlFlow))
class UMaterialExpressionReturnMaterialAttributes : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	UPROPERTY()
	FExpressionInput Exec;

	UPROPERTY()
	FMaterialAttributesInput MaterialAttributes;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual const TArray<FExpressionInput*> GetInputs() override;
	virtual FExpressionInput* GetInput(int32 InputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
	virtual FExpressionInput* GetExecInput() override { return &Exec; }
#endif
	//~ End UMaterialExpression Interface
};
