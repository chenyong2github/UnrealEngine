// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightCardActorDetails.h"

#include "DisplayClusterLightCardActor.h"
#include "IDisplayClusterLightCardActorExtender.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "PropertyHandle.h"
#include "Algo/Find.h"
#include "Features/IModularFeatures.h"

TSharedRef<IDetailCustomization> FDisplayClusterLightCardActorDetails::MakeInstance()
{
	return MakeShared<FDisplayClusterLightCardActorDetails>();
}

void FDisplayClusterLightCardActorDetails::CustomizeDetails(IDetailLayoutBuilder& InLayoutBuilder)
{
	// Add the detail views of components added for light card actor Extenders
	const TSharedRef<IPropertyHandle> ExtenderNameToComponentMapHandle = InLayoutBuilder.GetProperty(ADisplayClusterLightCardActor::GetExtenderNameToComponentMapMemberName());
	ExtenderNameToComponentMapHandle->MarkHiddenByCustomization();

	TArray<const void*> RawDatas;
	ExtenderNameToComponentMapHandle->AccessRawData(RawDatas);
	if (!RawDatas.IsEmpty())
	{
		IModularFeatures& ModularFeatures = IModularFeatures::Get();
		const TArray<IDisplayClusterLightCardActorExtender*> Extenders = ModularFeatures.GetModularFeatureImplementations<IDisplayClusterLightCardActorExtender>(IDisplayClusterLightCardActorExtender::ModularFeatureName);

		FName Category;
		bool bShouldShowSubcategories = false;
		TArray<UObject*> Components;
		for (const void* RawData : RawDatas)
		{
			TMap<FName, UActorComponent*>* ExtenderNameToComponentMapPtr = (TMap<FName, UActorComponent*>*)(RawData);
			check(ExtenderNameToComponentMapPtr);

			for (const TTuple<FName, UActorComponent*>& ExtenderNameToComponentPair : *ExtenderNameToComponentMapPtr)
			{
				if (!ensureMsgf(ExtenderNameToComponentPair.Value, TEXT("Trying to display component for Extender %s, but component is invalid."), *ExtenderNameToComponentPair.Key.ToString()))
				{
					continue;
				}

				IDisplayClusterLightCardActorExtender* const* ExtenderPtr = 
					Algo::FindByPredicate(Extenders, [ExtenderNameToComponentPair](const IDisplayClusterLightCardActorExtender* Extender)
						{
							return Extender->GetExtenderName() == ExtenderNameToComponentPair.Key;
						});
				if (!ensureMsgf(ExtenderPtr, TEXT("Cannot find Extender %s expected to exist for component %s."), *ExtenderNameToComponentPair.Key.ToString(), *ExtenderNameToComponentPair.Value->GetName()))
				{
					continue;
				}

				Category = (*ExtenderPtr)->GetCategory();
				bShouldShowSubcategories = (*ExtenderPtr)->ShouldShowSubcategories();
				Components.Add(ExtenderNameToComponentPair.Value);
			}
		}

		FAddPropertyParams AddPropertyParams;
		AddPropertyParams.CreateCategoryNodes(bShouldShowSubcategories);
		AddPropertyParams.HideRootObjectNode(true);

		InLayoutBuilder
			.EditCategory(Category)
			.AddExternalObjects(Components, EPropertyLocation::Default, AddPropertyParams);
	}
}
