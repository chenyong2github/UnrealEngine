// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyTypeCustomization.h"

#include "AnimNextGraph.h"
#include "WidgetFactories.h"
#include "DetailWidgetRow.h"
#include "Editor/PropertyEditor/Private/ObjectPropertyNode.h"
#include "Editor/PropertyEditor/Private/PropertyNode.h"
#include "Interface/IAnimNextInterface.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "SClassViewer.h"
#include "Widgets/Text/STextBlock.h"
#include "GraphEditorUtils.h"
#include "Param/ParamTypeHandle.h"

#define LOCTEXT_NAMESPACE "AnimNextPropertyTypeCustomization"

namespace UE::AnimNext::GraphEditor
{

bool FPropertyTypeIdentifier::IsPropertyTypeCustomized(const IPropertyHandle& PropertyHandle) const
{
	return (PropertyHandle.GetMetaData("AnimNextType") != "");
}

void FPropertyTypeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const FString TypeName = PropertyHandle->GetMetaData("AnimNextType");
	const FAnimNextParamType AnimNextType = FUtils::GetParameterTypeFromMetaData(TypeName);
	if(AnimNextType.IsValid())
	{
		void* ValuePtr;
		PropertyHandle->GetValueData(ValuePtr);
		FScriptInterface* PropertyValue = reinterpret_cast<FScriptInterface*>(ValuePtr);
		
		TSharedPtr<SWidget> Widget = FWidgetFactories::CreateAnimNextInterfaceWidget(AnimNextType.GetHandle(), PropertyValue->GetObject(),
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
}

void FPropertyTypeCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
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