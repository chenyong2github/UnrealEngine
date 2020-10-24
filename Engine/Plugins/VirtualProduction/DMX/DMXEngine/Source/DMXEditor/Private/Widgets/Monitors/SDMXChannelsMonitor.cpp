// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Monitors/SDMXChannelsMonitor.h"

#include "DMXEditorLog.h"
#include "DMXEditorSettings.h"
#include "DMXEditorUtils.h"
#include "DetailLayoutBuilder.h"
#include "DMXProtocolCommon.h"
#include "Widgets/SNameListPicker.h"
#include "Widgets/SDMXChannel.h"

#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"


#define LOCTEXT_NAMESPACE "SDMXChannelsMonitor"

void SDMXChannelsMonitor::Construct(const FArguments& InArgs)
{
	SetCanTick(true);

	LoadMonitorSettings();

	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(15.0f)
			.AutoHeight()
			[
				SNew(SWrapBox)
				.InnerSlotPadding(FVector2D(35.0f, 10.0f))
				.UseAllottedWidth(true)

				// Protocol
				+SWrapBox::Slot()
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
					.Padding(10.0f, 0.0f, 0.0f, 0.0f)
					.AutoWidth()
					[
						SNew(SNameListPicker)
						.Value(this, &SDMXChannelsMonitor::GetProtocolName)
						.OnValueChanged(this, &SDMXChannelsMonitor::OnProtocolChanged)
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
						.Text(LOCTEXT("UniverseIDLabel", "Remote Universe"))
					]

					// Universe ID current value text.
					+ SHorizontalBox::Slot()
					.Padding(10.0f, 0.0f, 0.0f, 0.0f)
					.AutoWidth()
					[
						SAssignNew(UniverseIDSpinBox, SSpinBox<uint32>)						
						.OnValueCommitted(this, &SDMXChannelsMonitor::OnUniverseIDValueCommitted)
						.Value(UniverseID)
						.MinValue(0)
						.MaxValue(MAX_uint16)
						.MinSliderValue(0)
						.MaxSliderValue(MAX_uint16)
						.MinDesiredWidth(50.0f)
					]
				]

				// Clear button
				+ SWrapBox::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("ClearTextLabel", "Clear Values"))
					.OnClicked(this, &SDMXChannelsMonitor::OnClearButtonClicked)
				]
			]

			// Separator
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(3.0f)
			[
				SNew(SSeparator)
				.Orientation(Orient_Horizontal)
			]

			// Channel Widgets
			+ SVerticalBox::Slot()
			[
				SNew(SScrollBox)
				.Orientation(Orient_Vertical)
				.ScrollBarAlwaysVisible(false)

				+ SScrollBox::Slot()
				.HAlign(HAlign_Fill)
				[
					SAssignNew(ChannelValuesBox, SWrapBox)
					.UseAllottedWidth(true)
					.InnerSlotPadding(FVector2D(1.0f, 1.0f))
				]
			]
		];


	// Init buffer
	Buffer.AddZeroed(DMX_UNIVERSE_SIZE);

	// Init UI
	ResetUISequenceID();
	CreateChannelValueWidgets();
	UpdateChannelValueWidgets();
}

void SDMXChannelsMonitor::CreateChannelValueWidgets()
{
	if (!ChannelValuesBox.IsValid())
	{
		return;
	}

	ChannelValueWidgets.Reserve(DMX_UNIVERSE_SIZE);

	for (uint32 ChannelID = 1; ChannelID <= DMX_UNIVERSE_SIZE; ++ChannelID)
	{
		TSharedPtr<SDMXChannel> ChannelValueWidget;

		ChannelValuesBox->AddSlot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SAssignNew(ChannelValueWidget, SDMXChannel)
				.ID(ChannelID)
				.Value(0)
			];

		ChannelValueWidgets.Add(ChannelValueWidget);
	}
}

void SDMXChannelsMonitor::LoadMonitorSettings()
{
	UDMXEditorSettings* DMXEditorSettings = GetMutableDefault<UDMXEditorSettings>();

	if (DMXEditorSettings->ChannelsMonitorProtocol.IsNone())
	{
		// Create default ProtocolName struct, which will have a valid protocol name
		ProtocolName = FDMXProtocolName();
		DMXEditorSettings->ChannelsMonitorProtocol = ProtocolName;
		DMXEditorSettings->SaveConfig();
	}
	else
	{
		ProtocolName = FDMXProtocolName(DMXEditorSettings->ChannelsMonitorProtocol);
	}

	UniverseID = DMXEditorSettings->ChannelsMonitorUniverseID;
}

void SDMXChannelsMonitor::SaveMonitorSettings() const
{
	// Update stored settings
	UDMXEditorSettings* DMXEditorSettings = GetMutableDefault<UDMXEditorSettings>();
	check(DMXEditorSettings);

	DMXEditorSettings->ChannelsMonitorProtocol = ProtocolName;
	DMXEditorSettings->ChannelsMonitorUniverseID = UniverseID;
	DMXEditorSettings->SaveConfig();
}

FReply SDMXChannelsMonitor::OnClearButtonClicked()
{
	FDMXEditorUtils::ZeroAllDMXBuffers();

	Buffer.Reset(DMX_UNIVERSE_SIZE);
	Buffer.AddZeroed(DMX_UNIVERSE_SIZE);

	UpdateChannelValueWidgets();

	return FReply::Handled();
}

void SDMXChannelsMonitor::OnUniverseIDValueCommitted(uint32 NewValue, ETextCommit::Type CommitType)
{
	if (UniverseID != NewValue)
	{
		uint16 MinUniverseID = ProtocolName.GetProtocol()->GetMinUniverseID();
		uint16 MaxUniverseID = ProtocolName.GetProtocol()->GetMaxUniverses();

		UniverseID = FMath::Clamp(static_cast<uint16>(NewValue), MinUniverseID, MaxUniverseID);

		SaveMonitorSettings();
		ResetUISequenceID();

		// Update the universe spinbox
		check(UniverseIDSpinBox.IsValid());
		UniverseIDSpinBox->SetValue(UniverseID);
	}
}

void SDMXChannelsMonitor::OnProtocolChanged(FName SelectedProtocol)
{
	FDMXProtocolName NewProtocolName(SelectedProtocol);
	check(NewProtocolName.IsValid());

	ProtocolName = NewProtocolName;

	uint16 MinUniverseID = ProtocolName.GetProtocol()->GetMinUniverseID();
	uint16 MaxUniverseID = ProtocolName.GetProtocol()->GetMaxUniverses();

	UniverseIDSpinBox->SetMinSliderValue(MinUniverseID);
	UniverseIDSpinBox->SetMinValue(MinUniverseID);
	UniverseIDSpinBox->SetMaxSliderValue(MaxUniverseID);
	UniverseIDSpinBox->SetMaxValue(MaxUniverseID);

	UniverseID = FMath::Clamp(UniverseID, MinUniverseID, MaxUniverseID);

	// Update stored settings
	SaveMonitorSettings();
}

void SDMXChannelsMonitor::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	UpdateBuffer();
}

void SDMXChannelsMonitor::UpdateBuffer()
{
	if (IDMXProtocolPtr DMXProtocolPtr = IDMXProtocol::Get(ProtocolName))
	{
		const IDMXUniverseSignalMap& InboundSignalMap = DMXProtocolPtr->GameThreadGetInboundSignals();

		if (InboundSignalMap.Contains(UniverseID))
		{
			const TSharedPtr<FDMXSignal>& Signal = InboundSignalMap[UniverseID];

			if (Signal->ChannelData != Buffer)
			{
				Buffer = Signal->ChannelData;
				UpdateChannelValueWidgets();
			}
		}
	}
}

void SDMXChannelsMonitor::UpdateChannelValueWidgets()
{
	for (uint32 ChannelID = 0; ChannelID < DMX_UNIVERSE_SIZE; ++ChannelID)
	{
		ChannelValueWidgets[ChannelID]->SetValue(Buffer[ChannelID]);
	}
}

#undef LOCTEXT_NAMESPACE
