// Copyright Epic Games, Inc. All Rights Reserved.

#include "Styling/ModifierBoundWidgetStyleDefinitions.h"

#include "Styling/WidgetStyleData.h"

namespace UE::VCamExtensions::Private
{
	static UWidgetStyleData* Filter(const TArray<UWidgetStyleData*>& MetaDataArray, TSubclassOf<UWidgetStyleData> Class)
	{
		for (UWidgetStyleData* MetaData : MetaDataArray)
		{
			if (MetaData && MetaData->IsA(Class))
			{
				return MetaData;
			}
		}
		return nullptr;
	}
}

TArray<UWidgetStyleData*> UModifierBoundWidgetStyleDefinitions::GetStylesForModifier_Implementation(UVCamComponent* VCamComponent, FName ModifierId) const
{
	unimplemented();
	return {};
}

TArray<UWidgetStyleData*> UModifierBoundWidgetStyleDefinitions::GetStylesForCategoryInModifier_Implementation(UVCamComponent* VCamComponent, FName ModifierId, FName Category) const
{
	unimplemented();
	return {};
}

TArray<UWidgetStyleData*> UModifierBoundWidgetStyleDefinitions::GetStylesForCategoryWithoutModifier_Implementation(FName Category) const
{
	unimplemented();
	return {};
}

UWidgetStyleData* UModifierBoundWidgetStyleDefinitions::GetStyleForModifierByClass(UVCamComponent* VCamComponent, FName ModifierId, TSubclassOf<UWidgetStyleData> Class) const
{
	return UE::VCamExtensions::Private::Filter(
		GetStylesForModifier(VCamComponent, ModifierId),
		Class
		);
}

UWidgetStyleData* UModifierBoundWidgetStyleDefinitions::GetStyleForCategoryByClassInModifier(UVCamComponent* VCamComponent, FName ModifierId, FName Category, TSubclassOf<UWidgetStyleData> Class) const
{
	return UE::VCamExtensions::Private::Filter(
		GetStylesForCategoryInModifier(VCamComponent, ModifierId, Category),
		Class
		);
}

UWidgetStyleData* UModifierBoundWidgetStyleDefinitions::GetStyleForCategoryWithoutModifier(FName Category, TSubclassOf<UWidgetStyleData> Class) const
{
	return UE::VCamExtensions::Private::Filter(
			GetStylesForCategoryWithoutModifier(Category),
			Class
			);
}