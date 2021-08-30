// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MaterialExpressionIO.h"
#include "Materials/MaterialExpressionParameter.h"
#include "MaterialExpressionStaticComponentMaskParameter.generated.h"

UCLASS(collapsecategories, hidecategories=Object, MinimalAPI)
class UMaterialExpressionStaticComponentMaskParameter : public UMaterialExpressionParameter
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FExpressionInput Input;
#endif

	UPROPERTY(EditAnywhere, Category=MaterialExpressionStaticComponentMaskParameter)
	uint32 DefaultR:1;

	UPROPERTY(EditAnywhere, Category=MaterialExpressionStaticComponentMaskParameter)
	uint32 DefaultG:1;

	UPROPERTY(EditAnywhere, Category=MaterialExpressionStaticComponentMaskParameter)
	uint32 DefaultB:1;

	UPROPERTY(EditAnywhere, Category=MaterialExpressionStaticComponentMaskParameter)
	uint32 DefaultA:1;


public:

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual void SetValueToMatchingExpression(UMaterialExpression* OtherExpression) override;
	virtual bool SetParameterValue(const FName& Name, const FMaterialParameterMetadata& Meta) override
	{
		if (Meta.Value.Type == EMaterialParameterType::StaticComponentMask)
		{
			return SetParameterValue(Name,
				Meta.Value.Bool[0],
				Meta.Value.Bool[1],
				Meta.Value.Bool[2],
				Meta.Value.Bool[3],
				Meta.ExpressionGuid);
		}
		return Super::SetParameterValue(Name, Meta);
	}
#endif
	//~ End UMaterialExpression Interface

	/** Return whether this is the named parameter, and fill in its value */
	bool IsNamedParameter(const FHashedMaterialParameterInfo& ParameterInfo, bool& OutR, bool& OutG, bool& OutB, bool& OutA, FGuid&OutExpressionGuid) const;

#if WITH_EDITOR
	bool SetParameterValue(FName InParameterName, bool InR, bool InG, bool InB, bool InA, FGuid InExpressionGuid);
#endif
};



