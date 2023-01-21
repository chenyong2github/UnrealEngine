// Copyright Epic Games, Inc. All Rights Reserved.

#include "Styling/ModifierBoundWidgetStylesAsset.h"

#include "Styling/ClassBasedWidgetStyleDefinitions.h"
#include "Styling/ModifierBoundWidgetStyleDefinitions.h"

TArray<UWidgetStyleData*> UModifierBoundWidgetStylesAsset::GetStylesForModifier(UVCamComponent* VCamComponent, FName ModifierId) const
{
	return Rules ? Rules->GetStylesForModifier(VCamComponent, ModifierId) : TArray<UWidgetStyleData*>{};
}

TArray<UWidgetStyleData*> UModifierBoundWidgetStylesAsset::GetStylesForCategoryInModifier(UVCamComponent* VCamComponent, FName ModifierId, FName Category) const
{
	return Rules ? Rules->GetStylesForCategoryInModifier(VCamComponent, ModifierId, Category) : TArray<UWidgetStyleData*>{};
}

TArray<UWidgetStyleData*> UModifierBoundWidgetStylesAsset::GetStylesForCategoryWithoutModifier(FName Category) const
{
	return Rules ? Rules->GetStylesForCategoryWithoutModifier(Category) : TArray<UWidgetStyleData*>{};
}

UWidgetStyleData* UModifierBoundWidgetStylesAsset::GetStyleForModifierByClass(UVCamComponent* VCamComponent, FName ModifierId, TSubclassOf<UWidgetStyleData> Class) const
{
	return Rules ? Rules->GetStyleForModifierByClass(VCamComponent, ModifierId, Class) : nullptr;
}

UWidgetStyleData* UModifierBoundWidgetStylesAsset::GetStyleForCategoryByClassInModifier(UVCamComponent* VCamComponent, FName ModifierId, FName Category, TSubclassOf<UWidgetStyleData> Class) const
{
	return Rules ? Rules->GetStyleForCategoryByClassInModifier(VCamComponent, ModifierId, Category, Class) : nullptr;
}

UWidgetStyleData* UModifierBoundWidgetStylesAsset::GetStyleForCategoryWithoutModifier(FName Category, TSubclassOf<UWidgetStyleData> Class) const
{
	return Rules ? Rules->GetStyleForCategoryWithoutModifier(Category, Class) : nullptr;
}
