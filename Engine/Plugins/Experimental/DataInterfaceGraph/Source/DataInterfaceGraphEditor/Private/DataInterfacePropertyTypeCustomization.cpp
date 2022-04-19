// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterfacePropertyTypeCustomization.h"
#include "PropertyHandle.h"
#include "IDataInterface.h"
#include "DetailWidgetRow.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DataInterfacePropertyTypeCustomization"

namespace UE::DataInterfaceGraphEditor
{

bool FPropertyTypeIdentifier::IsPropertyTypeCustomized(const IPropertyHandle& PropertyHandle) const
{
	FInterfaceProperty* InterfaceProperty = CastField<FInterfaceProperty>(PropertyHandle.GetProperty());
	return InterfaceProperty && InterfaceProperty->InterfaceClass->IsChildOf(UDataInterface::StaticClass());
}

void FDataInterfacePropertyTypeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	FInterfaceProperty* InterfaceProperty = CastField<FInterfaceProperty>(PropertyHandle->GetProperty());

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SObjectPropertyEntryBox)
		.PropertyHandle(PropertyHandle)
		.DisplayUseSelected(false)
		.OnShouldFilterAsset_Lambda([InterfaceProperty](const FAssetData& InAssetData)
		{
			if(UClass* Class = InAssetData.GetClass())
			{
				if(Class->ImplementsInterface(InterfaceProperty->InterfaceClass))
				{
					return false;
				}
			}

			return true;
		})
	];
}

}

#undef LOCTEXT_NAMESPACE