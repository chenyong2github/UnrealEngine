// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXEditorPropertyEditorCustomization.h"
#include "DMXEditor.h"
#include "DMXEditorLog.h"

#include "DMXProtocolTypes.h"
#include "DMXEditorStyle.h"
#include "Library/DMXEntityReference.h"
#include "Library/DMXEntity.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Widgets/SDMXEntityDropdownMenu.h"
#include "Game/DMXComponent.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailPropertyRow.h"
#include "IDetailChildrenBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyCustomizationHelpers.h"
#include "IPropertyUtilities.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "ScopedTransaction.h"
#include "DMXProtocolSACN/Private/DMXProtocolSACNConstants.h"
#include "DMXProtocolArtNet/Private/DMXProtocolArtNetConstants.h"
#define LOCTEXT_NAMESPACE "DMXCustomizeDetails"

static void CollectChildPropertiesRecursive(TSharedPtr<IPropertyHandle> Node, TArray<TSharedPtr<IPropertyHandle>>& OutProperties)
{
	uint32 NodeNumChildren = 0;
	Node->GetNumChildren(NodeNumChildren);

	for (uint32 ChildIdx = 0; ChildIdx < NodeNumChildren; ++ChildIdx)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = Node->GetChildHandle(ChildIdx);
		CollectChildPropertiesRecursive(ChildHandle, OutProperties);

		if (ChildHandle->GetProperty())
		{
			OutProperties.AddUnique(ChildHandle);
		}
	}
}

void FDMXCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	// Customize the Name input field to check for repeated/invalid names
	NamePropertyHandle = DetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXEntity, Name), UDMXEntity::StaticClass());
	check(NamePropertyHandle->IsValidHandle());

	// Don't allow editing the Name if multiple Entities are selected
	FString Name;
	const bool bCanEditName = NamePropertyHandle->GetValue(Name) == FPropertyAccess::Success;

	DetailLayout.EditDefaultProperty(NamePropertyHandle)->CustomWidget()
		.NameContent()
		[
			NamePropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MaxDesiredWidth(250.0f)
		[
			SAssignNew(NameEditableTextBox, SEditableTextBox)
			.Text(this, &FDMXCustomization::OnGetEntityName)
			.ToolTipText(NamePropertyHandle->GetToolTipText())
			.OnTextChanged(this, &FDMXCustomization::OnEntityNameChanged)
			.OnTextCommitted(this, &FDMXCustomization::OnEntityNameCommitted)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.IsEnabled(bCanEditName)
		];

	// Keep Display Name as first property
	DetailLayout.EditCategory("Entity Properties", FText::GetEmpty(), ECategoryPriority::Important);
}

FText FDMXCustomization::OnGetEntityName() const
{
	check(NamePropertyHandle && NamePropertyHandle->IsValidHandle());

	FString Name;
	if (NamePropertyHandle->GetValue(Name) == FPropertyAccess::Success)
	{
		return FText::FromString(Name);
	}

	return LOCTEXT("EntityName_MultipleValues", "Multiple Values");
}

void FDMXCustomization::OnEntityNameChanged(const FText& InNewText)
{
	check(NameEditableTextBox.IsValid() && NamePropertyHandle.IsValid() && NamePropertyHandle->IsValidHandle());

	FString CurrentName;
	if (NamePropertyHandle->GetValue(CurrentName) != FPropertyAccess::Success)
	{
		return;
	}

	const FString& NewName = InNewText.ToString();
	if (CurrentName.Equals(NewName))
	{
		NameEditableTextBox->SetError(FText::GetEmpty());
		return;
	}

	check(DMXEditorPtr.IsValid());
	TArray<UObject*> SelectedEntities;
	NamePropertyHandle->GetOuterObjects(SelectedEntities);
	check(SelectedEntities.Num() > 0);

	FText OutErrorMessage;
	FDMXEditorUtils::ValidateEntityName(
		NewName,
		DMXEditorPtr.Pin()->GetDMXLibrary(),
		SelectedEntities[0]->GetClass(),
		OutErrorMessage
	);

	NameEditableTextBox->SetError(OutErrorMessage);
}

void FDMXCustomization::OnEntityNameCommitted(const FText& InNewText, ETextCommit::Type InCommitType)
{
	check(NameEditableTextBox.IsValid() && NamePropertyHandle.IsValid() && NamePropertyHandle->IsValidHandle());

	if (InCommitType != ETextCommit::OnCleared && !NameEditableTextBox->HasError())
	{
		NamePropertyHandle->SetValue(InNewText.ToString());
	}

	NameEditableTextBox->SetError(FText::GetEmpty());
}

FDMXFixtureTypeFunctionsDetails::FDMXFixtureTypeFunctionsDetails(TWeakPtr<FDMXEditor> InDMXEditorPtr)
	: DMXEditorPtr(InDMXEditorPtr)
{
	check(DMXEditorPtr.IsValid());

	const TArray<UDMXEntity*>&& SelectedEntities = DMXEditorPtr.Pin()->GetSelectedEntitiesFromTypeTab(UDMXEntityFixtureType::StaticClass());
	for (UDMXEntity* Entity : SelectedEntities)
	{
		if (UDMXEntityFixtureType* FixtureType = Cast<UDMXEntityFixtureType>(Entity))
		{
			SelectedFixtures.Add(FixtureType);
		}
	}
}

void FDMXFixtureTypeFunctionsDetails::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, class FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InStructCustomizationUtils)
{
	InHeaderRow
		.NameContent()
		[
			InStructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			InStructPropertyHandle->CreatePropertyValueWidget(false)
		];
}

void FDMXFixtureTypeFunctionsDetails::BuildFunctionNameWidget(IDetailChildrenBuilder& InStructBuilder, FText& NewPropertyLabel, FText& ToolTip)
{
	InStructBuilder
		.AddCustomRow(LOCTEXT("FunctionNameWidget", "FunctionNameWidget"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(NewPropertyLabel)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MaxDesiredWidth(250.0f)
		[
			SAssignNew(NameEditableTextBox, SEditableTextBox)
			.Text(this, &FDMXFixtureTypeFunctionsDetails::OnGetFunctionName)
			.ToolTipText(ToolTip)
			.OnTextChanged(this, &FDMXFixtureTypeFunctionsDetails::OnFunctionNameChanged)
			.OnTextCommitted(this, &FDMXFixtureTypeFunctionsDetails::OnFunctionNameCommitted)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];
}

void FDMXFixtureTypeFunctionsDetails::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& InStructBuilder, IPropertyTypeCustomizationUtils& InStructCustomizationUtils)
{
	// Retrieve structure's child properties
	uint32 NumChildren;
	InStructPropertyHandle->GetNumChildren(NumChildren);
	TMap<FName, TSharedPtr< IPropertyHandle > > PropertyHandles;
	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		TSharedRef<IPropertyHandle> ChildHandle = InStructPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef();
		const FName PropertyName = ChildHandle->GetProperty()->GetFName();
		PropertyHandles.Add(PropertyName, ChildHandle);
	}

	FText NewPropertyLabel;
	FText ToolTip;
	GetCustomNameFieldSettings(NewPropertyLabel, NamePropertyName, ToolTip, ExistingNameError);

	// Check validity of the name property handle
	check(PropertyHandles[NamePropertyName]->IsValidHandle());

	NamePropertyHandle = PropertyHandles[NamePropertyName];

	for (const TPair<FName, TSharedPtr<IPropertyHandle>>& PropertyHandlePair : PropertyHandles)
	{

		// Never show default name property, we will show custom editable text instead
		if (PropertyHandlePair.Key != NamePropertyName)
		{
			AddProperty(InStructBuilder, PropertyHandlePair.Key, PropertyHandlePair.Value.ToSharedRef());
		}
		else if (PropertyHandlePair.Key == NamePropertyName)
		{
			// Add custom name widget
			BuildFunctionNameWidget(InStructBuilder, NewPropertyLabel, ToolTip);
		}
	}
}

void FDMXFixtureTypeFunctionsDetails::AddProperty(IDetailChildrenBuilder& InStructBuilder, const FName& PropertyName, TSharedRef<IPropertyHandle> PropertyHandle)
{
	// Default implementation simply adds the properties
	InStructBuilder.AddProperty(PropertyHandle);
}

void FDMXFixtureTypeFunctionsDetails::OnFunctionNameChanged(const FText& InNewText)
{
	check(NameEditableTextBox.IsValid() && NamePropertyHandle.IsValid() && NamePropertyHandle->IsValidHandle());

	if (FText::TrimPrecedingAndTrailing(InNewText).IsEmpty())
	{
		NameEditableTextBox->SetError(LOCTEXT("FunctionNameError_Empty", "The name can't be blank!"));
		return;
	}

	FString CurrentName;
	NamePropertyHandle->GetValue(CurrentName);

	const FString& NewName = InNewText.ToString();
	if (CurrentName.Equals(NewName))
	{
		NameEditableTextBox->SetError(FText::GetEmpty());
		return;
	}

	const TArray<FString>&& ExistingNames = GetExistingNames();
	if (ExistingNames.Contains(NewName))
	{
		NameEditableTextBox->SetError(ExistingNameError);
	}
	else
	{
		NameEditableTextBox->SetError(FText::GetEmpty());
	}
}

void FDMXFixtureTypeFunctionsDetails::OnFunctionNameCommitted(const FText& InNewText, ETextCommit::Type InTextCommit)
{
	if (InTextCommit != ETextCommit::OnCleared && !NameEditableTextBox->HasError())
	{
		const FString& NewName = InNewText.ToString();
		SetFunctionName(NewName);
	}
	NameEditableTextBox->SetError(FText::GetEmpty());
}

FText FDMXFixtureTypeFunctionsDetails::OnGetFunctionName() const
{
	FString Name;
	NamePropertyHandle->GetValue(Name);
	return FText::FromString(Name);
}

void FDMXFixtureTypeFunctionsDetails::SetFunctionName(const FString& NewName)
{
	if (NamePropertyHandle.IsValid() && NamePropertyHandle->IsValidHandle())
	{
		NamePropertyHandle->SetValue(NewName);
	}
}

void FDMXFixtureModeDetails::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& InStructBuilder, IPropertyTypeCustomizationUtils& InStructCustomizationUtils)
{
	FText NewPropertyLabel;
	FText ToolTip;
	GetCustomNameFieldSettings(NewPropertyLabel, NamePropertyName, ToolTip, ExistingNameError);

	uint32 NumChildren;
	InStructPropertyHandle->GetNumChildren(NumChildren);

	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		TSharedRef<IPropertyHandle> ChildHandle = InStructPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef();
		if (ChildHandle->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions))
		{
			continue;
		}
		
		if (ChildHandle->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, FixtureMatrixConfig))
		{
			continue;
		}
		
		if (ChildHandle->GetProperty()->GetFName() == NamePropertyName)
		{
			NamePropertyHandle = ChildHandle;
			BuildFunctionNameWidget(InStructBuilder, NewPropertyLabel, ToolTip);
			continue;
		}

		InStructBuilder.AddProperty(ChildHandle);
	}

	TArray<UObject*> Outers;
	InStructPropertyHandle->GetOuterObjects(Outers);

	if (Outers.Num() == 1 && Outers[0]->IsA<UDMXEntityFixtureType>())
	{
		UDMXEntityFixtureType* FixtureType = Cast<UDMXEntityFixtureType>(Outers[0]);

		TSharedPtr<IPropertyHandle> CellMatrixConfigProperty = InStructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, FixtureMatrixConfig));
		// Retrieve structure's child properties
		CellMatrixConfigProperty->GetNumChildren(NumChildren);

		for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
		{
			TSharedRef<IPropertyHandle> ChildHandle = CellMatrixConfigProperty->GetChildHandle(ChildIndex).ToSharedRef();
			IDetailPropertyRow& Row = InStructBuilder.AddProperty(ChildHandle);
			Row.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDMXFixtureModeDetails::CheckFixtureMatrix, ChildHandle, TWeakObjectPtr<UDMXEntityFixtureType>(FixtureType))));
		}

		TSharedPtr<IPropertyHandle> FunctionsHandle = InStructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions));
		check(FunctionsHandle.IsValid() && FunctionsHandle->IsValidHandle());
		FunctionsHandle->MarkHiddenByCustomization();
	}
}

EVisibility FDMXFixtureModeDetails::CheckFixtureMatrix(TSharedRef<IPropertyHandle> PropertyHandle, TWeakObjectPtr<UDMXEntityFixtureType> FixtureType)
{
	if (FixtureType.IsValid())
	{
		if (FixtureType->bFixtureMatrixEnabled)
		{
			return EVisibility::Visible;
		}
	}
	return EVisibility::Collapsed;
}

void FDMXFixtureModeDetails::GetCustomNameFieldSettings(FText& OutNewPropertyLabel, FName& OutNamePropertyName, FText& OutToolTip, FText& OutExistingNameError)
{
	OutNewPropertyLabel = LOCTEXT("FixtureModeNameLabel", "Mode Name");
	OutNamePropertyName = GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, ModeName);
	OutToolTip = LOCTEXT("FixtureModeNameToolTip", "The name of this mode");
	OutExistingNameError = LOCTEXT("FixtureModeName_Existent", "This name is already used by another Mode in this fixture!");
}

TArray<FString> FDMXFixtureModeDetails::GetExistingNames() const
{
	TArray<FString> ExistingNames;
	for (UDMXEntityFixtureType* Fixture : SelectedFixtures)
	{
		if (Fixture == nullptr)
		{
			continue;
		}

		for (const FDMXFixtureMode& Mode : Fixture->Modes)
		{
			ExistingNames.Add(Mode.ModeName);
		}
	}

	return ExistingNames;
}

void FDMXFixtureFunctionDetails::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& InStructBuilder, IPropertyTypeCustomizationUtils& InStructCustomizationUtils)
{
	FDMXFixtureTypeFunctionsDetails::CustomizeChildren(InStructPropertyHandle, InStructBuilder, InStructCustomizationUtils);

	StructPropertyHandle = InStructPropertyHandle;

	DataTypeHandle = InStructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, DataType));
	check(DataTypeHandle && DataTypeHandle->IsValidHandle());

	DefaultValueHandle = InStructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, DefaultValue));
	check(DefaultValueHandle && DefaultValueHandle->IsValidHandle());

	UseLSBHandle = InStructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, bUseLSBMode));
	check(UseLSBHandle && UseLSBHandle->IsValidHandle());

	// Create fields for the individual channels (each byte) that will be displayed depending on the selected DataType
	AddChannelInputFields(InStructBuilder);
}

void FDMXFixtureFunctionDetails::AddChannelInputFields(IDetailChildrenBuilder& InStructBuilder)
{
	const FMargin Padding = FMargin(2.0f, 0.0f, 0.0f, 0.0f);

	InStructBuilder
		.AddCustomRow(LOCTEXT("ChannelsWidget", "Channels Widget"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ChannelsValues", "Channels Values"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ToolTipText(LOCTEXT("ChannelsToolTip", "Individual channels values. Useful for things like colors"))
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
				CreateChannelField(1, SNumericEntryBox<uint8>::RedLabelBackgroundColor)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Fill)
			.Padding(Padding)
			[
				CreateChannelField(2, SNumericEntryBox<uint8>::GreenLabelBackgroundColor)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Fill)
			.Padding(Padding)
			[
				CreateChannelField(3, SNumericEntryBox<uint8>::BlueLabelBackgroundColor)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Fill)
			.Padding(Padding)
			[
				CreateChannelField(4)
			]
		]
		.Visibility(MakeAttributeSP(this, &FDMXFixtureFunctionDetails::GetChannelInputVisibility, (uint8)1));
}

TSharedRef<SWidget> FDMXFixtureFunctionDetails::CreateChannelField(uint8 Channel, const FLinearColor& LabelColor /*= FLinearColor(0.0f, 0.0f, 0.0f, 0.5f)*/)
{
	return SNew(SNumericEntryBox<uint8>)
		.MinValue(0)
		.MaxValue(255)
		.MaxSliderValue(255)
		.Value(this, &FDMXFixtureFunctionDetails::GetChannelValue, Channel)
		.OnValueChanged(this, &FDMXFixtureFunctionDetails::HandleChannelValueChanged, Channel)
		.OnValueCommitted(this, &FDMXFixtureFunctionDetails::HandleChannelValueCommitted)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.AllowSpin(true)
		.Visibility(MakeAttributeSP(this, &FDMXFixtureFunctionDetails::GetChannelInputVisibility, Channel))
		.LabelPadding(0)
		.Label()
		[
			SNumericEntryBox<uint8>::BuildLabel(
				FText::AsNumber(Channel),
				FLinearColor::White,
				LabelColor)
		];
}

TOptional<uint8> FDMXFixtureFunctionDetails::GetChannelValue(uint8 Channel) const
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

EVisibility FDMXFixtureFunctionDetails::GetChannelInputVisibility(uint8 Channel) const
{
	void* DataTypePtr = nullptr;
	if (DataTypeHandle->GetValueData(DataTypePtr) == FPropertyAccess::Success)
	{
		EDMXFixtureSignalFormat* DataType = (EDMXFixtureSignalFormat*)DataTypePtr;
		if ((*DataType >= EDMXFixtureSignalFormat::E32Bit && Channel <= 4)
			|| (*DataType >= EDMXFixtureSignalFormat::E24Bit && Channel <= 3)
			|| (*DataType >= EDMXFixtureSignalFormat::E16Bit && Channel <= 2))
		{
			return EVisibility::Visible;
		}

		return EVisibility::Collapsed;
	}

	return EVisibility::Collapsed;
}

void FDMXFixtureFunctionDetails::HandleChannelValueChanged(uint8 NewValue, uint8 Channel)
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

void FDMXFixtureFunctionDetails::HandleChannelValueCommitted(uint8 NewValue, ETextCommit::Type CommitType)
{
	int64 DefaultValue;
	if (DefaultValueHandle->GetValue(DefaultValue) == FPropertyAccess::Success)
	{
		// We need to set the value without the InteractiveChange flag to register the transaction
		DefaultValueHandle->SetValue(DefaultValue);
	}
}

void FDMXFixtureFunctionDetails::GetCustomNameFieldSettings(FText& OutNewPropertyLabel, FName& OutNamePropertyName, FText& OutToolTip, FText& OutExistingNameError)
{
	OutNewPropertyLabel = LOCTEXT("FixtureFunctionNameLabel", "Function Name");
	OutNamePropertyName = GET_MEMBER_NAME_CHECKED(FDMXFixtureFunction, FunctionName);
	OutToolTip = LOCTEXT("FixtureFunctionNameToolTip", "The name of this function");
	OutExistingNameError = LOCTEXT("FixtureFunctionName_Existent", "This name is already used by another function in this mode!");
}

TArray<FString> FDMXFixtureFunctionDetails::GetExistingNames() const
{
	check(NamePropertyHandle.IsValid() && NamePropertyHandle->IsValidHandle());

	TArray<FString> ExistingNames;

	// Enumerate all properties indexes from the function in a map. This way, if we change the Functions
	// properties order in the future, we don't need to change this code because we'll find it by name.
	TSharedRef<IPropertyHandle> FunctionStruct = NamePropertyHandle->GetParentHandle().ToSharedRef();
	TMap<FName, uint32> FunctionPropertiesMap;
	uint32 NumFunctionProperties;
	FunctionStruct->GetNumChildren(NumFunctionProperties);
	for (uint32 PropertyIndex = 0; PropertyIndex < NumFunctionProperties; ++PropertyIndex)
	{
		TSharedRef<IPropertyHandle> PropertyHandle = FunctionStruct->GetChildHandle(PropertyIndex).ToSharedRef();
		const FName PropertyName = PropertyHandle->GetProperty()->GetFName();
		FunctionPropertiesMap.Add(PropertyName, PropertyIndex);
	}
	const uint32 NamePropertyIndex = FunctionPropertiesMap[NamePropertyName];

	// Get the parent functions array property to be able to read the other functions names inside the current Mode
	TSharedRef<IPropertyHandle> ParentFunctionsArray = FunctionStruct->GetParentHandle().ToSharedRef();

	uint32 NumFunctions;
	ParentFunctionsArray->GetNumChildren(NumFunctions);
	for (uint32 FunctionIndex = 0; FunctionIndex < NumFunctions; ++FunctionIndex)
	{
		// The current function in the functions array
		TSharedRef<IPropertyHandle> Function = ParentFunctionsArray->GetChildHandle(FunctionIndex).ToSharedRef();

		// Add the function name to the list of existing names
		TSharedRef<IPropertyHandle> NameHandle = Function->GetChildHandle(NamePropertyIndex).ToSharedRef();
		FString FunctionName;
		NameHandle->GetValue(FunctionName);
		ExistingNames.Add(FunctionName);
	}

	return ExistingNames;
}

void FDMXFixturePatchesDetails::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	FDMXCustomization::CustomizeDetails(DetailLayout);

	PropertyUtilities = DetailLayout.GetPropertyUtilities();

	// Bind to auto assign address changes to assign channels when it gets enabled
	AutoAssignAddressHandle = DetailLayout.GetProperty(UDMXEntityFixturePatch::GetAutoAssignAddressPropertyNameChecked());
	check(AutoAssignAddressHandle.IsValid() && AutoAssignAddressHandle->IsValidHandle());
	
	FSimpleDelegate OnAutoAssignAddressChangedDelegate = FSimpleDelegate::CreateSP(this, &FDMXFixturePatchesDetails::OnAutoAssignAddressChanged);
	AutoAssignAddressHandle->SetOnPropertyValueChanged(OnAutoAssignAddressChangedDelegate);

	// Handle mode changes of the parent fixture type
	UDMXEntityFixtureType::GetDataTypeChangeDelegate().AddSP(this, &FDMXFixturePatchesDetails::OnModesChanged);

	// Make a Fixture Types dropdown for the Fixture Type template property
	ParentFixtureTypeHandle = DetailLayout.GetProperty(UDMXEntityFixturePatch::GetParentFixtureTypeTemplatePropertyNameChecked());
	check(ParentFixtureTypeHandle.IsValid() && ParentFixtureTypeHandle->IsValidHandle());

	DetailLayout.EditDefaultProperty(ParentFixtureTypeHandle)->CustomWidget(false)
		.NameContent()
		[
			ParentFixtureTypeHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(200.0f)
		.MaxDesiredWidth(400.0f)
		[
			SNew(SDMXEntityPickerButton<UDMXEntityFixtureType>)
			.DMXEditor(DMXEditorPtr)
			.CurrentEntity(this, &FDMXFixturePatchesDetails::GetParentFixtureTemplate)
			.OnEntitySelected(this, &FDMXFixturePatchesDetails::OnParentTemplateSelected)
			.HasMultipleValues(this, &FDMXFixturePatchesDetails::GetParentFixtureTypeIsMultipleValues)
		];

	// Make a modes dropdown to select the active Fixture Type Mode, if a valid Fixture Type is selected
	ActiveModeHandle = DetailLayout.GetProperty(UDMXEntityFixturePatch::GetActiveModePropertyNameChecked());
	check(ActiveModeHandle->IsValidHandle());

	TSharedPtr<uint32> DefaultSelectedActiveMode = nullptr;
	GenerateActiveModeOptions();

	int32 ActiveMode;
	if (ensure(ActiveModeHandle->GetValue(ActiveMode)))
	{
		const bool bActiveModeExists = ActiveModeOptions.ContainsByPredicate([ActiveMode](TSharedPtr<uint32> Option) {
			return Option.IsValid() && *Option == ActiveMode;
			});

		if (!bActiveModeExists)
		{
			SetFixturePatchActiveMode(0);
		}
	}

	DetailLayout.EditDefaultProperty(ActiveModeHandle)->CustomWidget(false)
		.NameContent()
		[
			ActiveModeHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MaxDesiredWidth(160.0f)
		[
			SAssignNew(ActiveModeOptionsWidget, SComboBox< TSharedPtr<uint32> >)
			.IsEnabled(this, &FDMXFixturePatchesDetails::GetActiveModeEditable)
			.OptionsSource(&ActiveModeOptions)
			.OnGenerateWidget(this, &FDMXFixturePatchesDetails::GenerateActiveModeOptionWidget)
			.OnSelectionChanged(this, &FDMXFixturePatchesDetails::OnActiveModeChanged)
			.InitiallySelectedItem(DefaultSelectedActiveMode)
			[
				SNew(STextBlock)
				.MinDesiredWidth(50.0f)
				.Text(this, &FDMXFixturePatchesDetails::GetCurrentActiveModeLabel)
				.Font(DetailLayout.GetDetailFont())
			]
		];
}

void FDMXFixturePatchesDetails::SetFixturePatchActiveMode(int32 ModeIndex)
{
	check(ActiveModeHandle.IsValid());

	TArray<UObject*> OuterObjects;
	ActiveModeHandle->GetOuterObjects(OuterObjects);

	TArray<UDMXEntityFixturePatch*> FixturePatches;
	for (UObject* Object : OuterObjects)
	{
		UDMXEntityFixturePatch* Patch = CastChecked<UDMXEntityFixturePatch>(Object);
		Patch->SetActiveModeIndex(ModeIndex);
	}
}

void FDMXFixturePatchesDetails::OnAutoAssignAddressChanged()
{
	check(AutoAssignAddressHandle.IsValid());

	bool bAutoAssignAddress;
	if (ensure(AutoAssignAddressHandle->GetValue(bAutoAssignAddress)))
	{
		if (bAutoAssignAddress)
		{
			TArray<UObject*> OuterObjects;
			AutoAssignAddressHandle->GetOuterObjects(OuterObjects);

			TArray<UDMXEntityFixturePatch*> FixturePatches;
			for (UObject* Object : OuterObjects)
			{
				UDMXEntityFixturePatch* Patch = CastChecked<UDMXEntityFixturePatch>(Object);
				FixturePatches.Add(Patch);
			}

			FDMXEditorUtils::AutoAssignedAddresses(FixturePatches);
		}
	}
}

void FDMXFixturePatchesDetails::OnModesChanged(const UDMXEntityFixtureType* FixtureType, const FDMXFixtureMode& Mode)
{
	PropertyUtilities->ForceRefresh();
}

void FDMXFixturePatchesDetails::GenerateActiveModeOptions()
{
	ActiveModeOptions.Empty();

	UObject* Object = nullptr;
	if (ParentFixtureTypeHandle->GetValue(Object) == FPropertyAccess::Success)
	{
		if (UDMXEntityFixtureType* Fixture = Cast<UDMXEntityFixtureType>(Object))
		{
			const uint32 NumModes = Fixture->Modes.Num();
			for (uint32 ModeIndex = 0; ModeIndex < NumModes; ++ModeIndex)
			{
				ActiveModeOptions.Add(MakeShared<uint32>(ModeIndex));
			}
		}
	}
}

TWeakObjectPtr<UDMXEntityFixtureType> FDMXFixturePatchesDetails::GetParentFixtureTemplate() const
{
	UObject* Object;
	if (ParentFixtureTypeHandle->GetValue(Object) == FPropertyAccess::Success)
	{
		return Cast<UDMXEntityFixtureType>(Object);
	}
	return nullptr;
}

void FDMXFixturePatchesDetails::OnParentTemplateSelected(UDMXEntity* NewTemplate) const
{
	ParentFixtureTypeHandle->SetValue(Cast<UDMXEntityFixtureType>(NewTemplate));
}

bool FDMXFixturePatchesDetails::GetParentFixtureTypeIsMultipleValues() const
{
	UObject* Object;
	return ParentFixtureTypeHandle->GetValue(Object) == FPropertyAccess::MultipleValues;
}

bool FDMXFixturePatchesDetails::GetActiveModeEditable() const
{
	UObject* Object = nullptr;
	if (ParentFixtureTypeHandle->GetValue(Object) == FPropertyAccess::Success && Object != nullptr)
	{
		if (UDMXEntityFixtureType* Fixture = Cast<UDMXEntityFixtureType>(Object))
		{
			return Fixture->Modes.Num() > 0;
		}
		return false;
	}
	return false;
}

TSharedRef<SWidget> FDMXFixturePatchesDetails::GenerateActiveModeOptionWidget(const TSharedPtr<uint32> InMode) const
{
	UObject* Object = nullptr;
	if (ParentFixtureTypeHandle->GetValue(Object) == FPropertyAccess::Success && Object != nullptr)
	{
		if (UDMXEntityFixtureType* Patch = Cast<UDMXEntityFixtureType>(Object))
		{
			if (InMode.IsValid() && 
				Patch->Modes.IsValidIndex(*InMode))
			{
				return SNew(STextBlock)
					.Text(FText::FromString(Patch->Modes[*InMode].ModeName));
			}
		}
	}

	return SNullWidget::NullWidget;
}

void FDMXFixturePatchesDetails::OnActiveModeChanged(const TSharedPtr<uint32> InSelectedMode, ESelectInfo::Type SelectInfo)
{
	if (InSelectedMode.IsValid())
	{
		ActiveModeHandle->SetValue(*InSelectedMode);
	}
}

FText FDMXFixturePatchesDetails::GetCurrentActiveModeLabel() const
{
	static const FText MultipleValuesLabel = LOCTEXT("MultipleValues_Label", "Multiple Values");
	static const FText NullTypeLabel = LOCTEXT("NullFixtureType_Label", "No Fixture Type selected");
	static const FText MultipleTypesLabel = LOCTEXT("MultipleFixtureTypes_Label", "Multiple Types Selected");
	static const FText NoModesLabel = LOCTEXT("NoModes_Label", "No modes in Fixture Type");

	UObject* Object = nullptr;
	const FPropertyAccess::Result FixtureTemplateAccessResult = ParentFixtureTypeHandle->GetValue(Object);
	UDMXEntityFixtureType* FixtureTemplate = Cast<UDMXEntityFixtureType>(Object);

	// Is only one type of Fixture Type selected?
	if (FixtureTemplateAccessResult == FPropertyAccess::Success)
	{
		// Is this type valid?
		if (FixtureTemplate != nullptr)
		{
			// We can try to get the mode, although it could be a different one for each of the templates
			int32 ModeValue = 0;
			if (ActiveModeHandle->GetValue(ModeValue) == FPropertyAccess::Success)
			{
				const TArray<FDMXFixtureMode>& CurrentModes = FixtureTemplate->Modes;
				if (CurrentModes.Num() > 0 && CurrentModes.IsValidIndex(ModeValue))
				{
					return FText::FromString(
						CurrentModes[ModeValue].ModeName
					);
				}
				else
				{
					return NoModesLabel;
				}
			}
			return MultipleValuesLabel;
		}
		return NullTypeLabel;
	}
	return MultipleTypesLabel;
}

const FName FDMXEntityReferenceCustomization::NAME_DMXLibrary = GET_MEMBER_NAME_CHECKED(FDMXEntityReference, DMXLibrary);

void FDMXEntityReferenceCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	StructHandle = InPropertyHandle;

	// If we should display any children, leave the standard header. Otherwise, create custom picker
	TSharedRef<SWidget> ValueContent = !GetDisplayLibrary()
		? CreateEntityPickerWidget(InPropertyHandle)
		: InPropertyHandle->CreatePropertyValueWidget(false);

	InHeaderRow
		.NameContent()
		[
			InPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(200.0f)
		.MaxDesiredWidth(400.0f)
		[
			ValueContent
		];
}

void FDMXEntityReferenceCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	if (!GetDisplayLibrary())
	{
		// Don't add any child properties
		return;
	}

	// Retrieve structure's child properties
	uint32 NumChildren;
	InPropertyHandle->GetNumChildren(NumChildren);
	TMap<FName, TSharedPtr< IPropertyHandle > > PropertyHandles;
	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		TSharedRef<IPropertyHandle> ChildHandle = InPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef();
		const FName PropertyName = ChildHandle->GetProperty()->GetFName();
		PropertyHandles.Add(PropertyName, ChildHandle);
	}

	// Add the properties
	for (const TPair<FName, TSharedPtr<IPropertyHandle>>& PropertyHandlePair : PropertyHandles)
	{
		if ((PropertyHandlePair.Key == NAME_DMXLibrary && GetDisplayLibrary())
			|| (PropertyHandlePair.Key != NAME_DMXLibrary && PropertyHandlePair.Key != "EntityId")) // EntityId is private, so GET_MEMBER... won't work
		{
			InChildBuilder.AddProperty(PropertyHandlePair.Value.ToSharedRef());
		}
	}

	// Add the picker
	InChildBuilder.AddCustomRow(LOCTEXT("EntityReferencePickerSearchText", "Entity"))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(CustomizationUtils.GetRegularFont())
			.Text(this, &FDMXEntityReferenceCustomization::GetPickerPropertyLabel)
		]
		.ValueContent()
		.MinDesiredWidth(200.0f)
		.MaxDesiredWidth(400.0f)
		[
			CreateEntityPickerWidget(InPropertyHandle)
		];
}

bool FDMXEntityReferenceCustomization::GetDisplayLibrary() const
{
	TArray<const void*> RawDataArr;
	StructHandle->AccessRawData(RawDataArr);

	for (const void* RawData : RawDataArr)
	{
		const FDMXEntityReference* EntityRefPtr = reinterpret_cast<const FDMXEntityReference*>(RawData);
		if (!EntityRefPtr->bDisplayLibraryPicker)
		{
			return false;
		}
	}

	return true;
}

TSharedRef<SWidget> FDMXEntityReferenceCustomization::CreateEntityPickerWidget(TSharedPtr<IPropertyHandle> InPropertyHandle) const
{
	return SNew(SDMXEntityPickerButton<UDMXEntity>)
		.CurrentEntity(this, &FDMXEntityReferenceCustomization::GetCurrentEntity)
		.HasMultipleValues(this, &FDMXEntityReferenceCustomization::GetEntityIsMultipleValues)
		.OnEntitySelected(this, &FDMXEntityReferenceCustomization::OnEntitySelected)
		.EntityTypeFilter(this, &FDMXEntityReferenceCustomization::GetEntityType)
		.DMXLibrary(this, &FDMXEntityReferenceCustomization::GetDMXLibrary)
		.IsEnabled(this, &FDMXEntityReferenceCustomization::GetPickerEnabled);
}

FText FDMXEntityReferenceCustomization::GetPickerPropertyLabel() const
{
	if (TSubclassOf<UDMXEntity> EntityType = GetEntityType())
	{
		return FDMXEditorUtils::GetEntityTypeNameText(EntityType, false);
	}

	return LOCTEXT("GenericTypeEntityLabel", "Entity");
}

bool FDMXEntityReferenceCustomization::GetPickerEnabled() const
{
	return GetEntityType() != nullptr;
}

TWeakObjectPtr<UDMXEntity> FDMXEntityReferenceCustomization::GetCurrentEntity() const
{
	if (GetEntityIsMultipleValues()) return nullptr;

	void* StructPtr = nullptr;
	if (StructHandle->GetValueData(StructPtr) == FPropertyAccess::Success && StructPtr != nullptr)
	{
		const FDMXEntityReference* EntityRef = reinterpret_cast<FDMXEntityReference*>(StructPtr);
		return EntityRef->GetEntity();
	}

	return nullptr;
}

bool FDMXEntityReferenceCustomization::GetEntityIsMultipleValues() const
{
	TArray<void*> RawData;
	StructHandle->AccessRawData(RawData);
	if (RawData[0] == nullptr)
	{
		return true;
	}

	bool bFirstEntitySet = false;
	UDMXEntity* FirstEntityPtr = nullptr;

	for (const void* StructPtr : RawData)
	{
		const FDMXEntityReference* EntityRefPtr = reinterpret_cast<const FDMXEntityReference*>(StructPtr);

		if (bFirstEntitySet)
		{
			if (EntityRefPtr->GetEntity() != FirstEntityPtr)
			{
				return true;
			}
		}
		else
		{
			FirstEntityPtr = EntityRefPtr->GetEntity();
			bFirstEntitySet = true;
		}
	}

	return false;
}

void FDMXEntityReferenceCustomization::OnEntitySelected(UDMXEntity* NewEntity) const
{
	FDMXEntityReference NewStructValues;
	NewStructValues.SetEntity(NewEntity);

	// Export new values to text format that can be imported later into the actual struct properties
	FString TextValue;
	FDMXEntityReference::StaticStruct()->ExportText(TextValue, &NewStructValues, nullptr, nullptr, EPropertyPortFlags::PPF_None, nullptr);

	// Set values on edited property handle from exported text
	ensure(StructHandle->SetValueFromFormattedString(TextValue, EPropertyValueSetFlags::DefaultFlags) == FPropertyAccess::Success);
}

TSubclassOf<UDMXEntity> FDMXEntityReferenceCustomization::GetEntityType() const
{
	TArray<void*> RawData;
	StructHandle->AccessRawData(RawData);
	if (RawData[0] == nullptr)
	{
		return nullptr;
	}

	const TSubclassOf<UDMXEntity> FirstEntityType = reinterpret_cast<FDMXEntityReference*>(RawData[0])->GetEntityType();

	for (const void* StructPtr : RawData)
	{
		const FDMXEntityReference* EntityRefPtr = reinterpret_cast<const FDMXEntityReference*>(StructPtr);
		if (EntityRefPtr->GetEntityType() != FirstEntityType)
		{
			// Different types are selected
			return nullptr;
		}
	}

	return FirstEntityType;
}

TWeakObjectPtr<UDMXLibrary> FDMXEntityReferenceCustomization::GetDMXLibrary() const
{
	TSharedPtr<IPropertyHandle> LibraryHandle = StructHandle->GetChildHandle(NAME_DMXLibrary);
	UObject* Object = nullptr;
	if (LibraryHandle->GetValue(Object) == FPropertyAccess::Success)
	{
		return Cast<UDMXLibrary>(Object);
	}
	return nullptr;
}

void FDMXPixelMappingDistributionCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;

	TSharedRef<SUniformGridPanel> DistributionGridPanel = SNew(SUniformGridPanel).SlotPadding(FMargin(1.f));;

	for (int32 XIndex = 0; XIndex < DistributionGridNumXPanels; ++XIndex)
	{
		for (int32 YIndex = 0; YIndex < DistributionGridNumYPanels; ++YIndex)
		{
			FString BrushPath = FString::Printf(TEXT("DMXEditor.PixelMapping.DistributionGrid.%d.%d"), XIndex, YIndex);

			TSharedPtr<SButton> Button = SNew(SButton)
				.ButtonColorAndOpacity(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateSP(this, &FDMXPixelMappingDistributionCustomization::GetButtonColorAndOpacity, XIndex, YIndex)))
				.OnClicked(FOnClicked::CreateSP(this, &FDMXPixelMappingDistributionCustomization::OnGridButtonClicked, XIndex, YIndex))
				[
					SNew(SImage)
					.Image(FDMXEditorStyle::Get().GetBrush(*BrushPath))
				]
				;

				DistributionGridPanel->AddSlot(XIndex, YIndex)
				[
					Button.ToSharedRef()
				];
		}
	}

	InHeaderRow
		.NameContent()
		[
			InPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(200.0f)
		.MaxDesiredWidth(400.0f)
		[
			DistributionGridPanel
		];
}

FReply FDMXPixelMappingDistributionCustomization::OnGridButtonClicked(int32 GridIndexX, int32 GridIndexY)
{
	if (PropertyHandle.IsValid())
	{
		uint8 ChoosenDistribution = (GridIndexX * DistributionGridNumXPanels + GridIndexY);

		PropertyHandle->NotifyPreChange();
		PropertyHandle->SetValue(ChoosenDistribution);
		PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
	}

	return FReply::Handled();
}

FSlateColor FDMXPixelMappingDistributionCustomization::GetButtonColorAndOpacity(int32 GridIndexX, int32 GridIndexY)
{
	if (PropertyHandle.IsValid())
	{
		uint8 CurrentDistribution = 0;
		if (PropertyHandle->GetValue(CurrentDistribution) == FPropertyAccess::Result::Success)
		{
			if (CurrentDistribution == (GridIndexX * DistributionGridNumXPanels + GridIndexY))
			{
				return FLinearColor(0.2f, 0.2f, 0.2f, 1.f);
			}
		}
	}

	return FLinearColor::Transparent;
}


void FDMXPixelMappingDistributionCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

#undef LOCTEXT_NAMESPACE
