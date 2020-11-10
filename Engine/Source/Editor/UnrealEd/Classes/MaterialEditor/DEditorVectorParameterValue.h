// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MaterialEditor/DEditorParameterValue.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "DEditorVectorParameterValue.generated.h"

UCLASS(hidecategories=Object, collapsecategories, editinlinenew)
class UNREALED_API UDEditorVectorParameterValue : public UDEditorParameterValue
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category=DEditorVectorParameterValue, meta=(OnlyUpdateOnInteractionEnd))
	FLinearColor ParameterValue;

	UPROPERTY(Transient)
	bool bIsUsedAsChannelMask;

	UPROPERTY(Transient)
	FParameterChannelNames ChannelNames;
};

