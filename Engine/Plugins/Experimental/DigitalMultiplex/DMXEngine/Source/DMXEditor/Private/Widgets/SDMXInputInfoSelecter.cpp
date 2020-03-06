// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXInputInfoSelecter.h"

#include "DMXEditor.h"
#include "Library/DMXLibrary.h"
#include "DMXEditorLog.h"
#include "Interfaces/IDMXProtocol.h"
#include "DMXProtocolSettings.h"
#include "Widgets/SNameListPicker.h"

#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SWrapBox.h"

#define LOCTEXT_NAMESPACE "SDMXInputInfoSelecter"

void SDMXInputInfoSelecter::Construct(const FArguments& InArgs)
{
	// Sets delegates
	OnUniverseSelectionChanged = InArgs._OnUniverseSelectionChanged;

	SetVisibility(EVisibility::SelfHitTestInvisible);

	const float PaddingBorders = 15.0f;
	const float PaddingKeyVal = 10.0f;
	const float PaddingNewInput = 35.0f;
	const FVector2D PaddingInner(PaddingNewInput, 10.0f);

	// Get values from plugin settings
	UDMXProtocolSettings* ProtocolSettings = GetMutableDefault<UDMXProtocolSettings>();
	CurrentUniverseID = ProtocolSettings->InputConsoleUniverseID;
	if (ProtocolSettings->InputConsoleProtocol.IsNone())
	{
		// Create default ProtocolName struct, which will have a valid protocol name
		CurrentProtocol = FDMXProtocolName();
		ProtocolSettings->InputConsoleProtocol = CurrentProtocol;
		ProtocolSettings->SaveConfig();
	}
	else
	{
		CurrentProtocol = FDMXProtocolName(ProtocolSettings->InputConsoleProtocol);
	}

	ChildSlot
	.Padding(PaddingBorders)
	[
		// root
		SNew(SWrapBox)
		.InnerSlotPadding(PaddingInner)
		.UseAllottedWidth(true)

		// Protocol
		+ SWrapBox::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			//Label
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ProtocolLabel", "Protocol"))
			]
			// Protocol combo box
			+ SHorizontalBox::Slot()
			.Padding(PaddingKeyVal, 0, 0, 0)
			.AutoWidth()
			[
				SNew(SNameListPicker)
				.Value(this, &SDMXInputInfoSelecter::GetCurrentProtocolName)
				.OnValueChanged(this, &SDMXInputInfoSelecter::HandleProtocolChanged)
				.OptionsSource(FDMXProtocolName::GetPossibleValues())
			]
		]

		// Universe ID
		+ SWrapBox::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			//Label
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("UniverseIDLabel", "Universe ID"))
			]
			// Final Universe ID current value text.
			+ SHorizontalBox::Slot()
			.Padding(PaddingKeyVal, 0, 0, 0)
			.AutoWidth()
			[
				SAssignNew(UniverseIDField, SSpinBox<uint32>)
				.Value(this, &SDMXInputInfoSelecter::GetCurrentUniverseID)
				.OnValueChanged(this, &SDMXInputInfoSelecter::HandleUniverseIDChanged)
				.OnValueCommitted(this, &SDMXInputInfoSelecter::HandleUniverseIDValueCommitted)
				.MinValue(0).MaxValue(MAX_uint16)
				.MinSliderValue(0).MaxSliderValue(MAX_uint16)
				.MinDesiredWidth(50.0f)
			]
		]
	];

	// Update UniverseID field min and max values
	HandleProtocolChanged(CurrentProtocol);
}

TSharedRef<SWidget> SDMXInputInfoSelecter::GenerateProtocolItemWidget(TSharedPtr<FName> InItem)
{
	if (!InItem.IsValid())
	{
		UE_LOG_DMXEDITOR(Warning, TEXT("InItem for GenerateProtocolItemWidget was null!"));
		return SNew(STextBlock)
			.Text(LOCTEXT("NullComboBoxItemLabel", "Null Error"));
	}

	return SNew(STextBlock)
		.Text(FText::FromName(*InItem));
}

void SDMXInputInfoSelecter::HandleProtocolChanged(FName SelectedProtocol)
{
	FDMXProtocolName ProtocolName(SelectedProtocol);
	if (!ProtocolName.IsValid())
	{
		UE_LOG_DMXEDITOR(Error, TEXT("%S: Selected null protocol!"), __FUNCTION__);
		return;
	}

	CurrentProtocol = ProtocolName;

	uint16 MinUniverseID = CurrentProtocol.GetProtocol()->GetMinUniverseID();
	uint16 MaxUniverseID = CurrentProtocol.GetProtocol()->GetMaxUniverses();

	UniverseIDField->SetMinSliderValue(MinUniverseID);
	UniverseIDField->SetMinValue(MinUniverseID);
	UniverseIDField->SetMaxSliderValue(MaxUniverseID);
	UniverseIDField->SetMaxValue(MaxUniverseID);

	CurrentUniverseID = FMath::Clamp(CurrentUniverseID, MinUniverseID, MaxUniverseID);

	// Execute delegate
	OnUniverseSelectionChanged.ExecuteIfBound(SelectedProtocol);

	// Update stored settings
	UDMXProtocolSettings* ProtocolSettings = GetMutableDefault<UDMXProtocolSettings>();
	ProtocolSettings->InputConsoleProtocol = SelectedProtocol;
	ProtocolSettings->InputConsoleUniverseID = CurrentUniverseID;
	ProtocolSettings->SaveConfig();
}

void SDMXInputInfoSelecter::HandleUniverseIDChanged(uint32 NewValue)
{
	if (CurrentUniverseID != (uint16)NewValue)
	{
		CurrentUniverseID = FMath::Clamp(NewValue, (uint32)0u, (uint32)MAX_uint16);
	}
}

void SDMXInputInfoSelecter::HandleUniverseIDValueCommitted(uint32 NewValue, ETextCommit::Type CommitType)
{
	if (CurrentUniverseID != (uint16)NewValue)
	{
		CurrentUniverseID = FMath::Clamp(NewValue, (uint32)0u, (uint32)MAX_uint16);;
	}

	// Update stored settings
	UDMXProtocolSettings* ProtocolSettings = GetMutableDefault<UDMXProtocolSettings>();
	ProtocolSettings->InputConsoleUniverseID = CurrentUniverseID;
	ProtocolSettings->SaveConfig();
}

#undef LOCTEXT_NAMESPACE
