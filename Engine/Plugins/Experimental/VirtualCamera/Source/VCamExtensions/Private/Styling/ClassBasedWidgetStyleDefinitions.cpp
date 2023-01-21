// Copyright Epic Games, Inc. All Rights Reserved.

#include "Styling/ClassBasedWidgetStyleDefinitions.h"

#include "VCamComponent.h"
#include "Algo/ForEach.h"

DEFINE_LOG_CATEGORY_STATIC(LogClassBasedModifierMetaDataRules, All, All);

namespace UE::VCamExtensions::Private
{
	static TArray<UWidgetStyleData*> AccumulateMetaDataRecursively(
		const TMap<TSubclassOf<UVCamModifier>, FPerModifierClassWidgetSytleData>& Config,
		UVCamComponent* VCamComponent, 
		FName ModifierId,
		TFunctionRef<const FWidgetStyleDataConfig*(const FPerModifierClassWidgetSytleData& ClassConfig)> RetrieveConfig)
	{
		TArray<UWidgetStyleData*> Result;
		if (!IsValid(VCamComponent))
		{
			return Result;
		}

		UVCamModifier* Modifier = VCamComponent->GetModifierByName(ModifierId);
		if (!Modifier)
		{
			UE_LOG(LogClassBasedModifierMetaDataRules, Warning, TEXT("Unknown modifier %s (on component %s)"), *ModifierId.ToString(), *VCamComponent->GetPathName());
			return Result;
		}

		for (UClass* ModifierClass = Modifier->GetClass(); ModifierClass != UVCamModifier::StaticClass()->GetSuperClass(); ModifierClass = ModifierClass->GetSuperClass())
		{
			const FPerModifierClassWidgetSytleData* ClassConfig = Config.Find(ModifierClass);
			if (!ClassConfig)
			{
				continue;
			}
			
			const FWidgetStyleDataConfig* MetaDataConfig = RetrieveConfig(*ClassConfig);
			if (!MetaDataConfig)
			{
				continue;
			}

			Algo::ForEach(MetaDataConfig->ModifierMetaData, [&Result](const TObjectPtr<UWidgetStyleData>& MetaData) { Result.Add(MetaData); });
			if (!MetaDataConfig->bInherit)
			{
				break;
			}
		}
		return Result;
	}
}


TArray<UWidgetStyleData*> UClassBasedWidgetStyleDefinitions::GetStylesForModifier_Implementation(UVCamComponent* VCamComponent, FName ModifierId) const
{
	return UE::VCamExtensions::Private::AccumulateMetaDataRecursively(
		Config,
		VCamComponent,
		ModifierId,
		[](const FPerModifierClassWidgetSytleData& ClassConfig)
		{
			return &ClassConfig.ModifierStyles;
		});
}

TArray<UWidgetStyleData*> UClassBasedWidgetStyleDefinitions::GetStylesForCategoryInModifier_Implementation(UVCamComponent* VCamComponent, FName ModifierId, FName ConnectionPointId) const
{
	return UE::VCamExtensions::Private::AccumulateMetaDataRecursively(
		Config,
		VCamComponent,
		ModifierId,
		[ConnectionPointId](const FPerModifierClassWidgetSytleData& ClassConfig)
		{
			return ClassConfig.CategorizedStyles.Find(ConnectionPointId);
		});
}

TArray<UWidgetStyleData*> UClassBasedWidgetStyleDefinitions::GetStylesForCategoryWithoutModifier_Implementation(FName Category) const
{
	const FWidgetStyleDataArray* Result = CategoriesWithoutModifier.Find(Category);
	return Result
		? Result->Styles
		: TArray<UWidgetStyleData*>{};
}
