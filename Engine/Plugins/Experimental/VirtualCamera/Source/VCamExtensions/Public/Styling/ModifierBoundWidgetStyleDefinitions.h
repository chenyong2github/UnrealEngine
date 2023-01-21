// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WidgetStyleData.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"
#include "ModifierBoundWidgetStyleDefinitions.generated.h"

class UWidgetStyleData;
class UVCamComponent;

/**
 * Associates information with modifiers and their connection points.
 * You can use it to retrieve custom information assigned to a modifier and / or its connections,
 * such as what icon a button representing that widget should have.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew)
class VCAMEXTENSIONS_API UModifierBoundWidgetStyleDefinitions : public UObject
{
	GENERATED_BODY()
public:

	/** Retrieves all meta data that is associated for a given modifier. */
	UFUNCTION(BlueprintPure, BlueprintNativeEvent, Category = "Virtual Camera")
	TArray<UWidgetStyleData*> GetStylesForModifier(UVCamComponent* VCamComponent, FName ModifierId) const;

	/** Retrieves all meta data that is associated for a given modifier and a sub-category name. */
	UFUNCTION(BlueprintPure, BlueprintNativeEvent, Category = "Virtual Camera")
	TArray<UWidgetStyleData*> GetStylesForCategoryInModifier(UVCamComponent* VCamComponent, FName ModifierId, FName Category) const;

	/** Retrieves all meta data that is associated with a given a category name; this data is not associated with any kind of modifier. */
	UFUNCTION(BlueprintPure, BlueprintNativeEvent, Category = "Virtual Camera")
	TArray<UWidgetStyleData*> GetStylesForCategoryWithoutModifier(FName Category) const;

	UFUNCTION(BlueprintPure, Category = "Virtual Camera", meta = (DeterminesOutputType = "Class"))
	UWidgetStyleData* GetStyleForModifierByClass(UVCamComponent* VCamComponent, FName ModifierId, TSubclassOf<UWidgetStyleData> Class) const;
	
	UFUNCTION(BlueprintPure, Category = "Virtual Camera", meta = (DeterminesOutputType = "Class"))
	UWidgetStyleData* GetStyleForCategoryByClassInModifier(UVCamComponent* VCamComponent, FName ModifierId, FName Category, TSubclassOf<UWidgetStyleData> Class) const;
	
	UFUNCTION(BlueprintPure, Category = "Virtual Camera", meta = (DeterminesOutputType = "Class"))
	UWidgetStyleData* GetStyleForCategoryWithoutModifier( FName Category, TSubclassOf<UWidgetStyleData> Class) const;
};
