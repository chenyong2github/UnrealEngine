// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXFixtureModeCustomization.h"

#include "DMXEditor.h"
#include "Library/DMXEntityFixtureType.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Widgets/Input/SEditableTextBox.h"


#define LOCTEXT_NAMESPACE "DMXFixtureTypeModeCustomization"

TSharedRef<IPropertyTypeCustomization> FDMXFixtureModeCustomization::MakeInstance()
{
	return MakeShared<FDMXFixtureModeCustomization>();
}

void FDMXFixtureModeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, class FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InStructCustomizationUtils)
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

void FDMXFixtureModeCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& InStructBuilder, IPropertyTypeCustomizationUtils& InStructCustomizationUtils)
{
	// Remember the outer fixture type
	TArray<UObject*> Outers;
	InStructPropertyHandle->GetOuterObjects(Outers);

	if (Outers.Num() == 1 && Outers[0]->IsA<UDMXEntityFixtureType>())
	{
		OuterFixtureType = Cast<UDMXEntityFixtureType>(Outers[0]);
	}

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

	// Remember the ModeName property
	ModeNameHandle = PropertyNamePropertyHandleMap.FindChecked(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, ModeName));

	// Add properties to the layout
	for (const TPair<FName, TSharedRef<IPropertyHandle>>& PropertyNamePropertyHandlePair : PropertyNamePropertyHandleMap)
	{
		// Don't add the Functions array, it has its own view in the editor.
		if (PropertyNamePropertyHandlePair.Key == GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions))
		{
			continue;
		}
	
		// Hide the reset to default button for all properties
		PropertyNamePropertyHandlePair.Value->MarkResetToDefaultCustomized();

		// Add the FixtureMatrixConfig struct and get its visibility depending on the bMatrixVisible property
		if (PropertyNamePropertyHandlePair.Key == GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, FixtureMatrixConfig))
		{
			IDetailPropertyRow& FixtureMatrixPropertyRow = InStructBuilder.AddProperty(PropertyNamePropertyHandlePair.Value);
			FixtureMatrixPropertyRow.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDMXFixtureModeCustomization::GetFixtureMatrixPropertyVisibility)));	
			FixtureMatrixPropertyRow.ShouldAutoExpand(true);
			FixtureMatrixPropertyRow.OverrideResetToDefault(FResetToDefaultOverride());
			continue;
		}

		if (PropertyNamePropertyHandlePair.Key == GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, ModeName))
		{
			// Customize the ModeName property
			BuildModeNameWidget(InStructBuilder);
			continue;
		}

		// Add other properties without further customizing them
		IDetailPropertyRow& PropertyRow = InStructBuilder.AddProperty(PropertyNamePropertyHandlePair.Value);
		PropertyRow.OverrideResetToDefault(FResetToDefaultOverride());
	}
}

void FDMXFixtureModeCustomization::BuildModeNameWidget(IDetailChildrenBuilder& InStructBuilder)
{
	InStructBuilder
		.AddCustomRow(LOCTEXT("ModeNameWidget", "ModeNameWidget"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FixtureModeNameLabel", "Mode Name"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MaxDesiredWidth(250.0f)
		[
			SAssignNew(ModeNameEditableTextBox, SEditableTextBox)
			.Text(this, &FDMXFixtureModeCustomization::GetModeName)
			.OnTextChanged(this, &FDMXFixtureModeCustomization::OnModeNameChanged)
			.OnTextCommitted(this, &FDMXFixtureModeCustomization::OnModeNameCommitted)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];
}

TArray<FString> FDMXFixtureModeCustomization::GetExistingModeNames() const
{
	TArray<FString> ExistingModeNames;

	// Get the parent Modes array property to be able to read the other Modes names inside the current Mode
	TSharedRef<IPropertyHandle> ModeStructHandle = ModeNameHandle->GetParentHandle().ToSharedRef();
	TSharedRef<IPropertyHandle> ModesArrayHandle = ModeStructHandle->GetParentHandle().ToSharedRef();

	uint32 NumModes;
	ModesArrayHandle->GetNumChildren(NumModes);

	for (uint32 ModeIndex = 0; ModeIndex < NumModes; ModeIndex++)
	{
		TSharedRef<IPropertyHandle> ModeHandle = ModesArrayHandle->GetChildHandle(ModeIndex).ToSharedRef();
		TSharedPtr<IPropertyHandle> NameHandle = ModeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, ModeName));

		FString ModeName;
		NameHandle->GetValue(ModeName);

		ExistingModeNames.Add(ModeName);
	}

	return ExistingModeNames;
}

void FDMXFixtureModeCustomization::OnModeNameChanged(const FText& InNewText)
{
	check(ModeNameEditableTextBox.IsValid() && ModeNameHandle.IsValid() && ModeNameHandle->IsValidHandle());

	if (FText::TrimPrecedingAndTrailing(InNewText).IsEmpty())
	{
		ModeNameEditableTextBox->SetError(LOCTEXT("ModeNameError_Empty", "The name can't be blank!"));
		return;
	}

	FString CurrentName;
	ModeNameHandle->GetValue(CurrentName);

	const FString& NewName = InNewText.ToString();
	if (CurrentName.Equals(NewName))
	{
		ModeNameEditableTextBox->SetError(FText::GetEmpty());
		return;
	}

	const TArray<FString> ExistingNames = GetExistingModeNames();
	if (ExistingNames.Contains(NewName))
	{
		ModeNameEditableTextBox->SetError(LOCTEXT("FixtureModeName_Existent", "This name is already used by another Mode in this mode!"));
	}
	else
	{
		ModeNameEditableTextBox->SetError(FText::GetEmpty());
	}
}

void FDMXFixtureModeCustomization::OnModeNameCommitted(const FText& InNewText, ETextCommit::Type InTextCommit)
{
	if (InTextCommit != ETextCommit::OnCleared && !ModeNameEditableTextBox->HasError())
	{
		const FString& NewName = InNewText.ToString();
		SetModeName(NewName);
	}
	ModeNameEditableTextBox->SetError(FText::GetEmpty());
}

FText FDMXFixtureModeCustomization::GetModeName() const
{
	FString Name;
	ModeNameHandle->GetValue(Name);

	return FText::FromString(Name);
}

void FDMXFixtureModeCustomization::SetModeName(const FString& NewName)
{
	if (ModeNameHandle.IsValid())
	{
		ModeNameHandle->SetValue(NewName);
	}
}

EVisibility FDMXFixtureModeCustomization::GetFixtureMatrixPropertyVisibility() const
{
	if (UDMXEntityFixtureType* FixtureType = OuterFixtureType.Get())
	{
		if (FixtureType->bFixtureMatrixEnabled)
		{
			return EVisibility::Visible;
		}
	}

	return EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
