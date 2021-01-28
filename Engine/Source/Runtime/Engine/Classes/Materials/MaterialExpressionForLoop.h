// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MaterialExpressionIO.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionForLoop.generated.h"

UCLASS(collapsecategories, hidecategories = Object, MinimalAPI, meta = (MaterialControlFlow))
class UMaterialExpressionForLoop : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	UPROPERTY()
	FExpressionInput Exec;

	UPROPERTY()
	FExpressionInput StartIndex;

	UPROPERTY()
	FExpressionInput EndIndex;

	UPROPERTY()
	FExpressionInput IndexStep;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetInputType(int32 InputIndex) override;
	virtual uint32 GetOutputType(int32 InputIndex) override;
	virtual FExpressionInput* GetExecInput() override { return &Exec; }
#endif
	//~ End UMaterialExpression Interface
};



