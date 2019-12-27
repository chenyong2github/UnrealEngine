// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Materials/MaterialExpressionParameter.h"
#include "MaterialExpressionVectorParameter.generated.h"

struct FPropertyChangedEvent;

UCLASS(collapsecategories, hidecategories=Object, MinimalAPI)
class UMaterialExpressionVectorParameter : public UMaterialExpressionParameter
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category=MaterialExpressionVectorParameter, meta=(OnlyUpdateOnInteractionEnd))
	FLinearColor DefaultValue;

	UPROPERTY(EditAnywhere, Category=CustomPrimitiveData)
	bool bUseCustomPrimitiveData = false;

	UPROPERTY(EditAnywhere, Category=CustomPrimitiveData, meta=(ClampMin="0"))
	uint8 PrimitiveDataIndex = 0;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = ParameterCustomization)
	FParameterChannelNames ChannelNames;
#endif

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
#endif
	//~ End UMaterialExpression Interface

	/** Return whether this is the named parameter, and fill in its value */
	bool IsNamedParameter(const FMaterialParameterInfo& ParameterInfo, FLinearColor& OutValue) const;

#if WITH_EDITOR
	virtual bool SetParameterValue(FName InParameterName, FLinearColor InValue);

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;	

	void ApplyChannelNames();
	

	virtual void ValidateParameterName(const bool bAllowDuplicateName) override;
	virtual bool HasClassAndNameCollision(UMaterialExpression* OtherExpression) const override;
	virtual void SetValueToMatchingExpression(UMaterialExpression* OtherExpression) override;
#endif

	virtual bool IsUsedAsChannelMask() const {return false;}

#if WITH_EDITOR
	FParameterChannelNames GetVectorChannelNames() const
	{
		return ChannelNames;
	}
#endif

	virtual void GetAllParameterInfo(TArray<FMaterialParameterInfo> &OutParameterInfo, TArray<FGuid> &OutParameterIds, const FMaterialParameterInfo& InBaseParameterInfo) const override;
};


