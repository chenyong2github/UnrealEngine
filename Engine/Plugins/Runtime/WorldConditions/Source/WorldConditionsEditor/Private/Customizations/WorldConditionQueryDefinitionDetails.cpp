// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldConditionQueryDefinitionDetails.h"
#include "PropertyHandle.h"
#include "IDetailChildrenBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyCustomizationHelpers.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "ScopedTransaction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "WorldConditionQuery.h"

#define LOCTEXT_NAMESPACE "WorldCondition"

TSharedRef<IPropertyTypeCustomization> FWorldConditionQueryDefinitionDetails::MakeInstance()
{
	return MakeShareable(new FWorldConditionQueryDefinitionDetails);
}

void FWorldConditionQueryDefinitionDetails::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities().Get();

	EditableConditionsProperty = StructProperty->GetChildHandle(TEXT("EditableConditions"));

	// Keep the definition up to date as it's being edited.
	StructProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FWorldConditionQueryDefinitionDetails::InitializeDefinition));
	StructProperty->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FWorldConditionQueryDefinitionDetails::InitializeDefinition));
	
	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			EditableConditionsProperty->CreatePropertyValueWidget()
		];
}

void FWorldConditionQueryDefinitionDetails::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	check(EditableConditionsProperty.IsValid());

	// Place editable conditions directly under.
	if (EditableConditionsProperty.IsValid())
	{
		const TSharedRef<FDetailArrayBuilder> Builder = MakeShareable(new FDetailArrayBuilder(EditableConditionsProperty.ToSharedRef(), /*InGenerateHeader*/ false, /*InDisplayResetToDefault*/ true, /*InDisplayElementNum*/ false));
		Builder->OnGenerateArrayElementWidget(FOnGenerateArrayElementWidget::CreateLambda([](TSharedRef<IPropertyHandle> PropertyHandle, int32 ArrayIndex, IDetailChildrenBuilder& ChildrenBuilder)
		{
			ChildrenBuilder.AddProperty(PropertyHandle);
		}));
		StructBuilder.AddCustomBuilder(Builder);
	}
}

void FWorldConditionQueryDefinitionDetails::InitializeDefinition() const
{
	check(StructProperty);

	TArray<void*> RawNodeData;
	TArray<UObject*> OuterObjects;
		
	StructProperty->AccessRawData(RawNodeData);
	StructProperty->GetOuterObjects(OuterObjects);
	check(RawNodeData.Num() == OuterObjects.Num());
		
	for (int32 Index = 0; Index < RawNodeData.Num(); Index++)
	{
		if (UObject* Outer = OuterObjects[Index])
		{
			if (FWorldConditionQueryDefinition* QueryDefinition = static_cast<FWorldConditionQueryDefinition*>(RawNodeData[Index]))
			{
				QueryDefinition->Initialize(*Outer);
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE
