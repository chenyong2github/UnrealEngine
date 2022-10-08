// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterfacePropertyTypeCustomization.h"

#include "Chooser.h"
#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Text/STextBlock.h"
#include "SClassViewer.h"
#include "ObjectChooserWidgetFactories.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyEditor/Private/ObjectPropertyNode.h"
#include "PropertyEditor/Private/PropertyNode.h"

#define LOCTEXT_NAMESPACE "InterfacePropertyTypeCustomization"

namespace UE::ChooserEditor
{

bool FPropertyTypeIdentifier::IsPropertyTypeCustomized(const IPropertyHandle& PropertyHandle) const
{
	return (PropertyHandle.GetMetaData("EditInlineInterface") != "");
}

void FInterfacePropertyTypeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	FProperty* Property = PropertyHandle->GetProperty();
	FInterfaceProperty* IntProperty = CastField<FInterfaceProperty>( Property );

	void* ValuePtr;
	PropertyHandle->GetValueData(ValuePtr);
	FScriptInterface* PropertyValue = reinterpret_cast<FScriptInterface*>(ValuePtr);
	
	UClass* ContextClass = nullptr;
	if (const UObject* Object = PropertyValue->GetObject())
	{
		if (const UChooserTable* ChooserTable = Object->GetTypedOuter<UChooserTable>())
		{
			ContextClass = ChooserTable->ContextObjectType;
		}
	}
	
	TSharedPtr<SWidget> Widget = FObjectChooserWidgetFactories::CreateWidget(InterfaceType, PropertyValue->GetObject(), ContextClass,
			FOnClassPicked::CreateLambda([this, PropertyHandle](UClass* ChosenClass)
			{
				TArray<void*> RawData;
				PropertyHandle->AccessRawData(RawData);
				TArray<UPackage*> OuterObjects;
				PropertyHandle->GetOuterPackages(OuterObjects);

				for(int i=0;i<RawData.Num();i++)
				{
					UObject* NewValue = NewObject<UObject>(OuterObjects[i], ChosenClass);
				
					PropertyHandle->NotifyPreChange();
					PropertyHandle->SetValue(NewValue);
					PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
					PropertyHandle->GetPropertyNode()->GetParentNode()->RequestRebuildChildren();
				}
			}),
			nullptr
	);

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		Widget.ToSharedRef()
	];
}
	
void FInterfacePropertyTypeCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// todo: make this work correctly for multiselect
	void* ValuePtr;
	PropertyHandle->GetValueData(ValuePtr);
	FScriptInterface* PropertyValue = reinterpret_cast<FScriptInterface*>(ValuePtr);
	if (PropertyValue->GetObject())
	{
		TSharedPtr<FStructOnScope> StructData = MakeShareable(new FStructOnScope(PropertyValue->GetObject()->GetClass(), (uint8*)PropertyValue->GetObject()));
		StructData->SetPackage(PropertyValue->GetObject()->GetPackage());
		TArray<TSharedPtr<IPropertyHandle>> NewHandles = ChildBuilder.AddAllExternalStructureProperties(StructData.ToSharedRef());
		for (auto& NewHandle : NewHandles)
		{
			ChildBuilder.AddProperty(NewHandle.ToSharedRef());
		}
	}
}

}

#undef LOCTEXT_NAMESPACE