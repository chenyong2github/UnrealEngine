// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXInputInfo.h"

#include "DMXEditor.h"
#include "Library/DMXLibrary.h"
#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "DMXEditorLog.h"
#include "DMXProtocolTypes.h"
#include "DMXProtocolConstants.h"
#include "Widgets/SDMXInputInfoChannelValue.h"
#include "Widgets/SDMXInputInfoSelecter.h"

#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "SDMXInputInfo"

void SDMXInputInfo::Construct(const FArguments& InArgs)
{
	WeakInfoSelecter = InArgs._InfoSelecter;

	SetVisibility(EVisibility::SelfHitTestInvisible);

	// Tick each frame but update only if new data is coming
	// In case of ticking we update UI only once per frame if any DMX data is coming
	SetCanTick(true);

	static const float PaddingBorders = 15.0f;
	static const float PaddingChannelValues = 3.0f;

	ChildSlot
	.Padding(PaddingBorders)
	[
		// root
		SNew(SScrollBox)
		.Orientation(Orient_Vertical)
		.ScrollBarAlwaysVisible(false)

		+SScrollBox::Slot()
		.HAlign(HAlign_Fill)
		[
			SAssignNew(ChannelValuesBox, SWrapBox)
			.UseAllottedWidth(true).InnerSlotPadding(FVector2D(1.0f))
		]
	];

	ResetUISequanceID();
	CreateChannelValueWidgets();

	// Set buffer values to 0
	ChannelsValues.SetNum(DMX_UNIVERSE_SIZE);
	FMemory::Memset(ChannelsValues.GetData(), 0, DMX_UNIVERSE_SIZE);
	UpdateChannelWidgetsValues(ChannelsValues);
}

void SDMXInputInfo::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	UpdateChannelsValues();
}

void SDMXInputInfo::CreateChannelValueWidgets()
{
	if (!ChannelValuesBox.IsValid())
	{
		return;
	}

	ChannelValueWidgets.Reserve(DMX_UNIVERSE_SIZE);

	for (uint32 ChannelIndex = 0; ChannelIndex < DMX_UNIVERSE_SIZE; ++ChannelIndex)
	{
		TSharedPtr<SDMXInputInfoChannelValue> ChannelValueWidget;

		ChannelValuesBox->AddSlot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SAssignNew(ChannelValueWidget, SDMXInputInfoChannelValue)
				.ID(ChannelIndex + 1) // +1 because channels start at 1
				.Value(0)
			];

		ChannelValueWidgets.Add(ChannelValueWidget);
	}
}

void SDMXInputInfo::UpdateChannelsValues()
{
	if (TSharedPtr<SDMXInputInfoSelecter> InfoSelecterPtr = WeakInfoSelecter.Pin())
	{
		if (IDMXProtocolPtr DMXProtocolPtr = IDMXProtocol::Get(InfoSelecterPtr->GetCurrentProtocolName()))
		{
			if (IDMXProtocolUniversePtr Universe = DMXProtocolPtr->GetUniverseById(InfoSelecterPtr->GetCurrentUniverseID()))
			{
				if (FDMXBufferPtr DMXBuffer = Universe->GetInputDMXBuffer())
				{
					// check the sequence ID 
					uint32 BufferSequenceID = DMXBuffer->GetSequenceID();
					if (BufferSequenceID != UISequenceID)
					{
						DMXBuffer->AccessDMXData([this](TArray<uint8>& InData)
							{
								FMemory::Memcpy(ChannelsValues.GetData(), InData.GetData(), DMX_UNIVERSE_SIZE);
							});
						UpdateChannelWidgetsValues(ChannelsValues);
					}

					UISequenceID = BufferSequenceID;
				}
			}
		}
	}
}

void SDMXInputInfo::UpdateChannelWidgetsValues(const TArray<uint8>& NewValues)
{
	if (NewValues.Num() != DMX_UNIVERSE_SIZE)
	{
		UE_LOG_DMXEDITOR(Error, TEXT("%S: Input values has the wrong number of channels!"), __FUNCTION__);
		return;
	}

	for (uint32 ChannelID = 0; ChannelID < DMX_UNIVERSE_SIZE; ++ChannelID)
	{
		ChannelValueWidgets[ChannelID]->SetValue(NewValues[ChannelID]);
	}
}

#undef LOCTEXT_NAMESPACE