// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolTypes.h"

#include "CoreMinimal.h"
#include "DetailWidgetRow.h"
#include "IPropertyTypeCustomization.h"
#include "IPropertyUtilities.h"
#include "Widgets/SNameListPicker.h"


/**  
 * Customization for types that should be displayed as a dropdown of options from a FName array 
 */
template<typename TStructType>
class FDMXNameListCustomization
	: public IPropertyTypeCustomization
{
private:
	using TNameListType = FDMXNameListCustomization<TStructType>;

public:
	/** Construction requires a delegate that returns the source list of possible names */
	FDMXNameListCustomization()
	{}

	/** Creates an instance of this property type customization */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FDMXNameListCustomization<TStructType>>();
	}

	//~ IPropertyTypeCustomization interface begin
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		StructPropertyHandle = InPropertyHandle;
		TSharedPtr<IPropertyUtilities> PropertyUtils = CustomizationUtils.GetPropertyUtilities();

		check(CastFieldChecked<FStructProperty>(StructPropertyHandle->GetProperty())->Struct == TStructType::StaticStruct());

		InHeaderRow
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
				.HasMultipleValues(this, &TNameListType::HasMultipleValues)
				.OptionsSource(MakeAttributeLambda(&TStructType::GetPossibleValues))
				.UpdateOptionsDelegate(&TStructType::OnValuesChanged)
				.IsValid(this, &TNameListType::HideWarningIcon)
				.Value(this, &TNameListType::GetValue)
				.bCanBeNone(TStructType::bCanBeNone)
				.bDisplayWarningIcon(true)
				.OnValueChanged(this, &TNameListType::SetValue)
			]
		.IsEnabled(MakeAttributeLambda([=] { return !InPropertyHandle->IsEditConst() && PropertyUtils->IsPropertyEditingEnabled(); }));
	}

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {};
	//~ IPropertyTypeCustomization interface end

private:
	FName GetValue() const
	{
		TArray<const void*> RawData;
		StructPropertyHandle->AccessRawData(RawData);

		for (const void* RawPtr : RawData)
		{
			if (RawPtr != nullptr)
			{
				// The types we use with this customization must have a cast constructor to FName
				return reinterpret_cast<const TStructType*>(RawPtr)->GetName();
			}
		}

		return FName();
	}

	void SetValue(FName NewValue)
	{
		FStructProperty* StructProperty = CastFieldChecked<FStructProperty>(StructPropertyHandle->GetProperty());

		TArray<void*> RawData;
		StructPropertyHandle->AccessRawData(RawData);

		for (void* SingleRawData : RawData)
		{
			TStructType* PreviousValue = reinterpret_cast<TStructType*>(SingleRawData);
			TStructType NewProtocolName;
			NewProtocolName.SetFromName(NewValue);

			// Export new value to text format that can be imported later
			FString TextValue;
			StructProperty->Struct->ExportText(TextValue, &NewProtocolName, PreviousValue, nullptr, EPropertyPortFlags::PPF_None, nullptr);

			// Set values on edited property handle from exported text
			ensure(StructPropertyHandle->SetValueFromFormattedString(TextValue, EPropertyValueSetFlags::DefaultFlags) == FPropertyAccess::Result::Success);
		}
	}

	bool HasMultipleValues() const
	{
		TArray<const void*> RawData;
		StructPropertyHandle->AccessRawData(RawData);

		TOptional<TStructType> CompareAgainst;
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
				const TStructType* ThisValue = reinterpret_cast<const TStructType*>(RawPtr);

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

	bool HideWarningIcon() const
	{
		if (HasMultipleValues())
		{
			return true;
		}

		const FName CurrentValue = GetValue();
		if (CurrentValue.IsEqual(FDMXNameListItem::None))
		{
			return true;
		}

		return TStructType::IsValid(GetValue());
	}

private:
	TSharedPtr<IPropertyHandle> StructPropertyHandle;
};
