// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Absolute value material expression for user-defined materials
 *
 */

#pragma once
#include "Materials/MaterialExpressionRerouteBase.h"
#include "MaterialExpressionReroute.generated.h"

UCLASS(collapsecategories, hidecategories=Object, DisplayName = "Reroute")
class ENGINE_API UMaterialExpressionReroute : public UMaterialExpressionRerouteBase
{
	GENERATED_UCLASS_BODY()

	/** Link to the input expression to be evaluated */
	UPROPERTY()
	FExpressionInput Input;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;

	virtual FText GetCreationDescription() const override;
	virtual FText GetCreationName() const override;
#endif
	//~ End UMaterialExpression Interface

protected:
	//~ Begin UMaterialExpressionRerouteBase Interface
	virtual bool GetRerouteInput(FExpressionInput& OutInput) const override;
	//~ End UMaterialExpressionRerouteBase Interface
};