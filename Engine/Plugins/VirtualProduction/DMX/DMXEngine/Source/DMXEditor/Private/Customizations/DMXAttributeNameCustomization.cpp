// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXAttributeNameCustomization.h"

#include "DMXAttribute.h"
#include "DMXProtocolSettings.h"

#include "DetailWidgetRow.h"
#include "IPropertyTypeCustomization.h"
#include "IPropertyUtilities.h"
#include "ScopedTransaction.h"
#include "Widgets/SNameListPicker.h"


TSharedRef<IPropertyTypeCustomization> FDMXAttributeNameCustomization::MakeInstance()
{
	return MakeShared<FDMXAttributeNameCustomization>();
}

void FDMXAttributeNameCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	StructPropertyHandle = InPropertyHandle;
	check(CastFieldChecked<FStructProperty>(StructPropertyHandle->GetProperty())->Struct == FDMXAttributeName::StaticStruct());

	PropertyUtilities = CustomizationUtils.GetPropertyUtilities();

	NameHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXAttributeName, Name));

	UDMXProtocolSettings* ProtocolSettings = GetMutableDefault<UDMXProtocolSettings>();
	if (!ProtocolSettings)
	{
		return;
	}

	TArray<FName> Attributes;
	for (const FDMXAttribute& Attribute : ProtocolSettings->Attributes)
	{
		Attributes.Add(Attribute.Name);
	}

	InHeaderRow
		.IsEnabled(MakeAttributeLambda([=] 
			{ 
				return !InPropertyHandle->IsEditConst() && PropertyUtilities->IsPropertyEditingEnabled();
			}))
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(125.0f)
		.MaxDesiredWidth(0.0f)
		[
			SNew(SNameListPicker)
			.Font(CustomizationUtils.GetRegularFont())
			.HasMultipleValues(this, &FDMXAttributeNameCustomization::HasMultipleValues)
			.OptionsSource(Attributes)
			.Value(this, &FDMXAttributeNameCustomization::GetValue)
			.bDisplayWarningIcon(true)
			.OnValueChanged(this, &FDMXAttributeNameCustomization::SetValue)
		];

	ProtocolSettings->GetOnDefaultAttributesChanged().AddSP(this, &FDMXAttributeNameCustomization::ForceRefresh);
}

void FDMXAttributeNameCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{

}

FName FDMXAttributeNameCustomization::GetValue() const
{
	TArray<const void*> RawData;
	StructPropertyHandle->AccessRawData(RawData);

	for (const void* RawPtr : RawData)
	{
		if (RawPtr != nullptr)
		{
			// The types we use with this customization must have a cast constructor to FName
			return reinterpret_cast<const FDMXAttributeName*>(RawPtr)->Name;
		}
	}

	return FName();
}

void FDMXAttributeNameCustomization::SetValue(FName NewValue)
{
	const FScopedTransaction SetAttributeValueTransaction(NSLOCTEXT("DMXAttributeNameCustomization", "SetAttributeValueTransaction", "Set Fixture Function Attribute Name"));

	NameHandle->SetValue(NewValue);
}

bool FDMXAttributeNameCustomization::HasMultipleValues() const
{
	TArray<const void*> RawData;
	StructPropertyHandle->AccessRawData(RawData);
	if (RawData.Num() == 1)
	{
		return false;
	}

	TOptional<FDMXAttributeName> CompareAgainst;
	for (const void* RawPtr : RawData)
	{
		if (RawPtr == nullptr)
		{
			if (CompareAgainst.IsSet())
			{
				return false;
			}
		}
		else
		{
			const FDMXAttributeName* ThisValue = reinterpret_cast<const FDMXAttributeName*>(RawPtr);

			if (!CompareAgainst.IsSet())
			{
				CompareAgainst = *ThisValue;
			}
			else if (!(*ThisValue == CompareAgainst.GetValue()))
			{
				return true;
			}
		}
	}

	return false;
}

void FDMXAttributeNameCustomization::ForceRefresh()
{
	PropertyUtilities->ForceRefresh();
}
