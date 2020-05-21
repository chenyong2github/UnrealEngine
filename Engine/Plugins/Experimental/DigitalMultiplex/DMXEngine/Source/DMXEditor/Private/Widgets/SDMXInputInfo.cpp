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
#include "Widgets/SDMXInputInfoChannelsView.h"
#include "Widgets/SDMXInputInfoUniverseMonitor.h"

#include "Widgets/Layout/SScrollBox.h"
#include "Async/Async.h"

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

		+ SScrollBox::Slot()
		.HAlign(HAlign_Fill)
		[
			SAssignNew(ChannelsView, SDMXInputInfoChannelsView)
			.InfoSelecter(WeakInfoSelecter)
		]
		+SScrollBox::Slot()
		.HAlign(HAlign_Fill)
		[
			SAssignNew(UniversesView, SDMXInputInfoUniverseMonitor)
			.InfoSelector(WeakInfoSelecter)
		]
	];
}

void SDMXInputInfo::ChangeToLookForAddresses()
{
	ChannelsView->SetVisibility(EVisibility::Visible);
	UniversesView->SetVisibility(EVisibility::Collapsed);
}

void SDMXInputInfo::ChangeToLookForUniverses()
{
	ChannelsView->SetVisibility(EVisibility::Collapsed);
	UniversesView->SetVisibility(EVisibility::Visible);

	UniversesView->SetupPacketReceiver();
}

void SDMXInputInfo::ClearUniverses()
{
	if (UniversesView.IsValid())
	{
		UniversesView->Clear();
	}
}

void SDMXInputInfo::ClearChannelsView()
{
	if (ChannelsView.IsValid())
	{
		ChannelsView->Clear();
	}
}

void SDMXInputInfo::UniverseSelectionChanged()
{
	ChannelsView->UniverseSelectionChanged();
}
#undef LOCTEXT_NAMESPACE