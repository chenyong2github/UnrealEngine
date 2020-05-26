// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXInputInfoChannelsView.h"
#include "Widgets/SDMXInputInfoChannelValue.h"
#include "Widgets/SDMXInputInfoSelecter.h"

#include "Interfaces/IDMXProtocol.h"
#include "Interfaces/IDMXProtocolUniverse.h"
#include "DMXEditorLog.h"

#include "Widgets/Layout/SWrapBox.h"


#define LOCTEXT_NAMESPACE "SDMXInputInfoChannelsView"

void SDMXInputInfoChannelsView::Construct(const FArguments& InArgs)
{
	WeakInfoSelecter = InArgs._InfoSelecter;

	ChildSlot
		[
			SAssignNew(ChannelValuesBox, SWrapBox)
			.UseAllottedWidth(true).InnerSlotPadding(FVector2D(1.0f))
		];
	
		// Tick each frame but update only if new data is coming
	// In case of ticking we update UI only once per frame if any DMX data is coming
	SetCanTick(true);

	UniverseID = 0xffffffff;

	ResetUISequanceID();
	CreateChannelValueWidgets();

	// Set buffer values to 0
	ChannelsValues.SetNum(DMX_UNIVERSE_SIZE);
	FMemory::Memset(ChannelsValues.GetData(), 0, DMX_UNIVERSE_SIZE);
	UpdateChannelWidgetsValues(ChannelsValues);
}

void SDMXInputInfoChannelsView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	CheckForSelectorChanges();
	UpdateChannelsValues();
}

void SDMXInputInfoChannelsView::CheckForSelectorChanges()
{
	if (TSharedPtr<SDMXInputInfoSelecter> InfoSelecterPtr = WeakInfoSelecter.Pin())
	{
		if (InfoSelecterPtr->GetCurrentUniverseID() != UniverseID)
		{
			UniverseID = InfoSelecterPtr->GetCurrentUniverseID();
			ResetUISequanceID();
		}
	}
}

void SDMXInputInfoChannelsView::CreateChannelValueWidgets()
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

void SDMXInputInfoChannelsView::UpdateChannelsValues()
{
	if (TSharedPtr<SDMXInputInfoSelecter> InfoSelecterPtr = WeakInfoSelecter.Pin())
	{
		if (IDMXProtocolPtr DMXProtocolPtr = IDMXProtocol::Get(InfoSelecterPtr->GetCurrentProtocolName()))
		{
			if (IDMXProtocolUniversePtr Universe = DMXProtocolPtr->GetUniverseById(InfoSelecterPtr->GetCurrentUniverseID()))
			{
				if (TSharedPtr<FDMXBuffer> DMXBuffer = Universe->GetInputDMXBuffer())
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

void SDMXInputInfoChannelsView::UpdateChannelWidgetsValues(const TArray<uint8>& NewValues)
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

void SDMXInputInfoChannelsView::Clear()
{
	if (TSharedPtr<SDMXInputInfoSelecter> InfoSelecterPtr = WeakInfoSelecter.Pin())
	{
		if (IDMXProtocolPtr DMXProtocolPtr = IDMXProtocol::Get(InfoSelecterPtr->GetCurrentProtocolName()))
		{
			if (IDMXProtocolUniversePtr Universe = DMXProtocolPtr->GetUniverseById(InfoSelecterPtr->GetCurrentUniverseID()))
			{
				if (TSharedPtr<FDMXBuffer> DMXBuffer = Universe->GetInputDMXBuffer())
				{
					// check the sequence ID 
					DMXBuffer->AccessDMXData([this](TArray<uint8>& InData)
						{
							FMemory::Memset(ChannelsValues.GetData(), 0, DMX_UNIVERSE_SIZE);
						});
					UpdateChannelWidgetsValues(ChannelsValues);
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE