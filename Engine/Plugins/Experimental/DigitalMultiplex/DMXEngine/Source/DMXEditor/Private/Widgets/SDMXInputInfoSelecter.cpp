// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXInputInfoSelecter.h"

#include "DMXEditor.h"
#include "Library/DMXLibrary.h"
#include "DMXEditorLog.h"
#include "Interfaces/IDMXProtocol.h"
#include "DMXProtocolSettings.h"
#include "Widgets/SNameListPicker.h"
#include "DetailLayoutBuilder.h"

#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "SDMXInputInfoSelecter"

FName SDMXInputInfoSelecter::LookForAddresses = FName(TEXT("Addresses"));
FName SDMXInputInfoSelecter::LookForUniverses = FName(TEXT("Universes"));


void SDMXInputInfoSelecter::Construct(const FArguments& InArgs)
{
	// Sets delegates
	OnUniverseSelectionChanged = InArgs._OnUniverseSelectionChanged;
	OnListenForChanged = InArgs._OnListenForChanged;
	OnClearUniverses = InArgs._OnClearUniverses;
	OnClearChannelsView = InArgs._OnClearChannelsView;

	ListenForOptions.Add(LookForAddresses);
	ListenForOptions.Add(LookForUniverses);
	CurrentListenFor = ListenForOptions[0];

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

	if (ProtocolSettings->InputConsoleListenFor.IsNone())
	{
		CurrentListenFor = LookForAddresses;
		ProtocolSettings->InputConsoleProtocol = CurrentListenFor;
		ProtocolSettings->SaveConfig();
	}
	else
	{
		CurrentListenFor = ProtocolSettings->InputConsoleListenFor;
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
				.IsValid(this, &SDMXInputInfoSelecter::DoesProtocolExist)
				.bDisplayWarningIcon(true)
			]
		]
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
				.Text(LOCTEXT("ListenForLabel", "Listen for"))
			]
			+ SHorizontalBox::Slot()
			.Padding(PaddingKeyVal, 0, 0, 0)
			.AutoWidth()
			[
				SNew(SNameListPicker)
				.Value(this, &SDMXInputInfoSelecter::GetCurrentListenFor)
				.OnValueChanged(this, &SDMXInputInfoSelecter::HandleListenForChanged)
				.OptionsSource(ListenForOptions)
			]
		]

		// Universe ID
		+ SWrapBox::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			SAssignNew(UniverseIDSelector, SHorizontalBox)
			//Label
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SAssignNew(UniverseIDLabel, STextBlock)
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
		// Clear button
		+ SWrapBox::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SAssignNew(ClearUniverseButton, SButton)
			.Text(LOCTEXT("ClearTextLabel", "Clear"))
			.OnClicked(this, &SDMXInputInfoSelecter::HandleClearButton)
		]
	];

	// Update UniverseID field min and max values
	HandleProtocolChanged(CurrentProtocol);

	HandleListenForChanged(CurrentListenFor);
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

void SDMXInputInfoSelecter::HandleListenForChanged(FName ListenFor)
{
	if (!ListenFor.IsValid())
	{
		UE_LOG_DMXEDITOR(Error, TEXT("%S: Selected null Listen For option!"), __FUNCTION__);
		return;
	}

	CurrentListenFor = ListenFor;
	if (UniverseIDField.IsValid() && UniverseIDLabel.IsValid())
	{
		if (CurrentListenFor == LookForAddresses)
		{
			UniverseIDField->SetVisibility(EVisibility::Visible);
			UniverseIDLabel->SetText(LOCTEXT("UniverseIDLabel", "Universe ID"));
		}
		else
		{
			UniverseIDField->SetVisibility(EVisibility::Hidden);
			// UniverseIDLabel ignores SetVisibility for some reason, so clear it by hand
			UniverseIDLabel->SetText(FText::GetEmpty());
		}
	}

	// Update stored settings
	UDMXProtocolSettings* ProtocolSettings = GetMutableDefault<UDMXProtocolSettings>();
	if (CurrentListenFor != ProtocolSettings->InputConsoleListenFor)
	{
		ProtocolSettings->InputConsoleListenFor = CurrentListenFor;
		ProtocolSettings->SaveConfig();
	}

	OnListenForChanged.ExecuteIfBound(CurrentListenFor);
}

void SDMXInputInfoSelecter::InitializeInputInfo()
{
	HandleListenForChanged(CurrentListenFor);
}

FReply SDMXInputInfoSelecter::HandleClearButton()
{
	OnClearUniverses.ExecuteIfBound();
	OnClearChannelsView.ExecuteIfBound();
	return FReply::Handled();
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

bool SDMXInputInfoSelecter::DoesProtocolExist() const
{
	return FDMXProtocolName::IsValid(GetCurrentProtocolName());
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
