// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MaterialEditor/DEditorParameterValue.h"

#include "DEditorRuntimeVirtualTextureParameterValue.generated.h"

UCLASS(hidecategories=Object, collapsecategories)
class UNREALED_API UDEditorRuntimeVirtualTextureParameterValue : public UDEditorParameterValue
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category=DEditorTextureParameterValue)
	class URuntimeVirtualTexture* ParameterValue;
};
