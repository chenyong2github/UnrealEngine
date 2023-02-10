// Copyright Epic Games, Inc. All Rights Reserved.

#include "Details/PCGGraphInstanceDetails.h"
#include "PCGGraph.h"

#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"

#define LOCTEXT_NAMESPACE "PCGGraphDetails"

TSharedRef<IDetailCustomization> FPCGGraphInstanceDetails::MakeInstance()
{
	return MakeShareable(new FPCGGraphInstanceDetails());
}

void FPCGGraphInstanceDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	const FName PCGCategoryName("PCG");
	IDetailCategoryBuilder& PCGCategory = DetailBuilder.EditCategory(PCGCategoryName);

	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);

	for (TWeakObjectPtr<UObject>& Object : ObjectsBeingCustomized)
	{
		UPCGGraphInstance* GraphInstance = Cast<UPCGGraphInstance>(Object.Get());
		if (ensure(GraphInstance))
		{
			SelectedGraphInstances.Add(GraphInstance);
		}
	}

	TArray<TSharedRef<IPropertyHandle>> AllProperties;
	bool bSimpleProperties = true;
	bool bAdvancedProperties = false;
	// Add all properties in the category in order
	PCGCategory.GetDefaultProperties(AllProperties, bSimpleProperties, bAdvancedProperties);

	for (auto& Property : AllProperties)
	{
		PCGCategory.AddProperty(Property);
	}
}

#undef LOCTEXT_NAMESPACE
