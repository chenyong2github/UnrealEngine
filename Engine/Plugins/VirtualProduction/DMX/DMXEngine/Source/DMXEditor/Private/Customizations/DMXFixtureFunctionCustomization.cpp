// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXFixtureFunctionCustomization.h"

#include "DMXEditor.h"
#include "Library/DMXEntityFixtureType.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"


#define LOCTEXT_NAMESPACE "DMXFixtureTypeFunctionCustomization"

TSharedRef<IPropertyTypeCustomization> FDMXFixtureFunctionCustomization::MakeInstance()
{
	return MakeShared<FDMXFixtureFunctionCustomization>();
}

void FDMXFixtureFunctionCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, class FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InStructCustomizationUtils)
{
	constexpr bool bDisplayDefaultPropertyButtons = false;

	InHeaderRow
		.NameContent()
		[
			InStructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			InStructPropertyHandle->CreatePropertyValueWidget(bDisplayDefaultPropertyButtons)
		];
}

void FDMXFixtureFunctionCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& InStructBuilder, IPropertyTypeCustomizationUtils& InStructCustomizationUtils)
{
	// Retrieve structure's child properties
	uint32 NumChildren;
	InStructPropertyHandle->GetNumChildren(NumChildren);
	
	TMap<FName, TSharedRef<IPropertyHandle>> PropertyNamePropertyHandleMap;
	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		TSharedRef<IPropertyHandle> ChildHandle = InStructPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef();
		const FName PropertyName = ChildHandle->GetProperty()->GetFName();
		PropertyNamePropertyHandleMap.Add(PropertyName, ChildHandle);
	}

	// Remember relevant property handles
	FunctionNameHandle = PropertyNamePropertyHandleMap.FindChecked(GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, FunctionName));
	DataTypeHandle = PropertyNamePropertyHandleMap.FindChecked(GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, DataType));
	DefaultValueHandle = PropertyNamePropertyHandleMap.FindChecked(GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, DefaultValue));
	UseLSBHandle = PropertyNamePropertyHandleMap.FindChecked(GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, bUseLSBMode));

	// Add properties to the layout
	for (const TPair<FName, TSharedRef<IPropertyHandle>>& PropertyNamePropertyHandlePair : PropertyNamePropertyHandleMap)
	{
		if (PropertyNamePropertyHandlePair.Key == GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, FunctionName))
		{
			// Customize the FunctionName property
			BuildFunctionNameWidget(InStructBuilder);
		}
		else if (PropertyNamePropertyHandlePair.Key == GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, DefaultValue))
		{
			// Customize the DefaultValue property (add a field for each byte of the default value, which will be displayed depending on the selected DataType)
			InStructBuilder.AddProperty(PropertyNamePropertyHandlePair.Value);
			AddDefaultValueFields(InStructBuilder);
		}
		else
		{
			// Add other properties
			InStructBuilder.AddProperty(PropertyNamePropertyHandlePair.Value);
		}
	}
}

void FDMXFixtureFunctionCustomization::BuildFunctionNameWidget(IDetailChildrenBuilder& InStructBuilder)
{
	InStructBuilder
		.AddCustomRow(LOCTEXT("FunctionNameWidget", "FunctionNameWidget"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FixtureFunctionNameLabel", "Function Name"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MaxDesiredWidth(250.0f)
		[
			SAssignNew(FunctionNameEditableTextBox, SEditableTextBox)
			.Text(this, &FDMXFixtureFunctionCustomization::GetFunctionName)
			.OnTextChanged(this, &FDMXFixtureFunctionCustomization::OnFunctionNameChanged)
			.OnTextCommitted(this, &FDMXFixtureFunctionCustomization::OnFunctionNameCommitted)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];
}

TArray<FString> FDMXFixtureFunctionCustomization::GetExistingFunctionNames() const
{
	TArray<FString> ExistingFunctionNames;

	// Get the parent functions array property to be able to read the other functions names inside the current Mode
	TSharedRef<IPropertyHandle> FunctionStructHandle = FunctionNameHandle->GetParentHandle().ToSharedRef();
	TSharedRef<IPropertyHandle> FunctionsArrayHandle = FunctionStructHandle->GetParentHandle().ToSharedRef();

	uint32 NumFunctions;
	FunctionsArrayHandle->GetNumChildren(NumFunctions);

	for (uint32 FunctionIndex = 0; FunctionIndex < NumFunctions; FunctionIndex++)
	{
		TSharedRef<IPropertyHandle> FunctionHandle = FunctionsArrayHandle->GetChildHandle(FunctionIndex).ToSharedRef();
		TSharedPtr<IPropertyHandle> NameHandle = FunctionHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, FunctionName));

		FString FunctionName;
		NameHandle->GetValue(FunctionName);

		ExistingFunctionNames.Add(FunctionName);
	}

	return ExistingFunctionNames;
}

void FDMXFixtureFunctionCustomization::OnFunctionNameChanged(const FText& InNewText)
{
	check(FunctionNameEditableTextBox.IsValid() && FunctionNameHandle.IsValid() && FunctionNameHandle->IsValidHandle());

	if (FText::TrimPrecedingAndTrailing(InNewText).IsEmpty())
	{
		FunctionNameEditableTextBox->SetError(LOCTEXT("FunctionNameError_Empty", "The name can't be blank!"));
		return;
	}

	FString CurrentName;
	FunctionNameHandle->GetValue(CurrentName);

	const FString& NewName = InNewText.ToString();
	if (CurrentName.Equals(NewName))
	{
		FunctionNameEditableTextBox->SetError(FText::GetEmpty());
		return;
	}

	const TArray<FString> ExistingNames = GetExistingFunctionNames();
	if (ExistingNames.Contains(NewName))
	{
		FunctionNameEditableTextBox->SetError(LOCTEXT("FixtureFunctionName_Existent", "This name is already used by another function in this mode!"));
	}
	else
	{
		FunctionNameEditableTextBox->SetError(FText::GetEmpty());
	}
}

void FDMXFixtureFunctionCustomization::OnFunctionNameCommitted(const FText& InNewText, ETextCommit::Type InTextCommit)
{
	if (InTextCommit != ETextCommit::OnCleared && !FunctionNameEditableTextBox->HasError())
	{
		const FString& NewName = InNewText.ToString();
		SetFunctionName(NewName);
	}
	FunctionNameEditableTextBox->SetError(FText::GetEmpty());
}

FText FDMXFixtureFunctionCustomization::GetFunctionName() const
{
	FString Name;
	FunctionNameHandle->GetValue(Name);

	return FText::FromString(Name);
}

void FDMXFixtureFunctionCustomization::SetFunctionName(const FString& NewName)
{
	if (FunctionNameHandle.IsValid())
	{
		FunctionNameHandle->SetValue(NewName);
	}
}

void FDMXFixtureFunctionCustomization::AddDefaultValueFields(IDetailChildrenBuilder& InStructBuilder)
{
	const FMargin Padding = FMargin(2.0f, 0.0f, 0.0f, 0.0f);

	InStructBuilder
		.AddCustomRow(LOCTEXT("DefaultValueRowName", "Default Value (Bytes)"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DefaultValueLabel", "Default Value (Bytes)"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ToolTipText(LOCTEXT("DefaultValueTooltip", "The default value for the channels of the Attribute"))
		]
		.ValueContent()
		.MinDesiredWidth(340.0f)
		.MaxDesiredWidth(340.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Fill)
			[
				CreateSingleDefaultValueChannelField(1, SNumericEntryBox<uint8>::RedLabelBackgroundColor)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Fill)
			.Padding(Padding)
			[
				CreateSingleDefaultValueChannelField(2, SNumericEntryBox<uint8>::GreenLabelBackgroundColor)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Fill)
			.Padding(Padding)
			[
				CreateSingleDefaultValueChannelField(3, SNumericEntryBox<uint8>::BlueLabelBackgroundColor)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Fill)
			.Padding(Padding)
			[
				CreateSingleDefaultValueChannelField(4)
			]
		]
		.Visibility(MakeAttributeSP(this, &FDMXFixtureFunctionCustomization::GetDefaultValueChannelVisibility, uint8(1)));

}

TSharedRef<SWidget> FDMXFixtureFunctionCustomization::CreateSingleDefaultValueChannelField(uint8 Channel, const FLinearColor& LabelColor)
{
	return SNew(SNumericEntryBox<uint8>)
		.MinValue(0)
		.MaxValue(255)
		.MaxSliderValue(255)
		.Value(this, &FDMXFixtureFunctionCustomization::GetDefaultValueChannelValue, Channel)
		.OnValueChanged(this, &FDMXFixtureFunctionCustomization::OnDefaultValueChannelValueChanged, Channel)
		.OnValueCommitted(this, &FDMXFixtureFunctionCustomization::OnDefaultValueChannelValueCommitted)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.AllowSpin(true)
		.Visibility(MakeAttributeSP(this, &FDMXFixtureFunctionCustomization::GetDefaultValueChannelVisibility, Channel))
		.LabelPadding(0)
		.Label()
		[
			SNumericEntryBox<uint8>::BuildLabel(
				FText::AsNumber(Channel),
				FLinearColor::White,
				LabelColor)
		];
}

TOptional<uint8> FDMXFixtureFunctionCustomization::GetDefaultValueChannelValue(uint8 Channel) const
{
	TOptional<uint8> RetVal;
	int64 Value;
	bool bUseLSBValue;
	if (DefaultValueHandle->GetValue(Value) == FPropertyAccess::Success
		&& UseLSBHandle->GetValue(bUseLSBValue) == FPropertyAccess::Success)
	{
		uint8 BytesOffset = 0;

		if (!bUseLSBValue)
		{
			void* DataTypePtr = nullptr;
			if (DataTypeHandle->GetValueData(DataTypePtr) == FPropertyAccess::Success)
			{
				const EDMXFixtureSignalFormat& DataType = *(EDMXFixtureSignalFormat*)DataTypePtr;
				const uint8 NumChannels = UDMXEntityFixtureType::NumChannelsToOccupy(DataType);
				BytesOffset = (NumChannels - Channel) * 8;
			}
		}
		else
		{
			BytesOffset = (Channel - 1) * 8;
		}

		RetVal = Value >> BytesOffset & 0xff;
	}

	return RetVal;
}

EVisibility FDMXFixtureFunctionCustomization::GetDefaultValueChannelVisibility(uint8 Channel) const
{
	void* DataTypePtr = nullptr;
	if (DataTypeHandle->GetValueData(DataTypePtr) == FPropertyAccess::Success)
	{
		EDMXFixtureSignalFormat* DataType = (EDMXFixtureSignalFormat*)DataTypePtr;
		if ((*DataType >= EDMXFixtureSignalFormat::E32Bit && Channel <= 4)
			|| (*DataType >= EDMXFixtureSignalFormat::E24Bit && Channel <= 3)
			|| (*DataType >= EDMXFixtureSignalFormat::E16Bit && Channel <= 2)
			|| (*DataType >= EDMXFixtureSignalFormat::E8Bit && Channel <= 1))
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

void FDMXFixtureFunctionCustomization::OnDefaultValueChannelValueChanged(uint8 NewValue, uint8 Channel)
{
	int64 DefaultValue;
	bool bUseLSBValue;
	if (DefaultValueHandle->GetValue(DefaultValue) == FPropertyAccess::Success
		&& UseLSBHandle->GetValue(bUseLSBValue) == FPropertyAccess::Success)
	{
		uint8* ValueBytes = reinterpret_cast<uint8*>(&DefaultValue);

		if (!bUseLSBValue)
		{
			void* DataTypePtr = nullptr;
			if (DataTypeHandle->GetValueData(DataTypePtr) == FPropertyAccess::Success)
			{
				const EDMXFixtureSignalFormat& DataType = *(EDMXFixtureSignalFormat*)DataTypePtr;
				const uint8 NumChannels = UDMXEntityFixtureType::NumChannelsToOccupy(DataType);
				ValueBytes[NumChannels - Channel] = NewValue;
			}
		}
		else
		{
			ValueBytes[Channel - 1] = NewValue;
		}

		DefaultValueHandle->SetValue(DefaultValue, EPropertyValueSetFlags::InteractiveChange);
	}
}

void FDMXFixtureFunctionCustomization::OnDefaultValueChannelValueCommitted(uint8 NewValue, ETextCommit::Type CommitType)
{
	int64 DefaultValue;
	if (DefaultValueHandle->GetValue(DefaultValue) == FPropertyAccess::Success)
	{
		// Set the value anew but without the InteractiveChange flag, to trigger a transaction when as the value is committed.
		DefaultValueHandle->SetValue(DefaultValue);
	}
}

#undef LOCTEXT_NAMESPACE
