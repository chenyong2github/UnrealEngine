// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MaterialExpressionIO.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionExecBegin.generated.h"

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object, meta = (MaterialControlFlow))
class UMaterialExpressionExecBegin : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex);
	virtual bool CanUserDeleteExpression() const override { return false; }
#endif
	//~ End UMaterialExpression Interface
};
