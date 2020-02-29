// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXInputConsole.h"
#include "Widgets/SDMXInputInfoSelecter.h"
#include "Widgets/SDMXInputInfo.h"
#include "Widgets/SDMXInputInfo.h"
#include "Widgets/Layout/SSeparator.h"

#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SDMXInputConsole"

void SDMXInputConsole::Construct(const FArguments& InArgs)
{
	InputInfoSelecter = SNew(SDMXInputInfoSelecter)
		.OnUniverseSelectionChanged(this, &SDMXInputConsole::OnUniverseSelectionChanged);
	InputInfo = SNew(SDMXInputInfo)
		.InfoSelecter(InputInfoSelecter);

	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
				.VAlign(VAlign_Top)
				.AutoHeight()
				[
					InputInfoSelecter.ToSharedRef()
				]
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(3.0f)
				[
					SNew(SSeparator)
						.Orientation(Orient_Horizontal)
				]
			+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					InputInfo.ToSharedRef()
				]
		];
}

void SDMXInputConsole::OnUniverseSelectionChanged(const FName& InProtocol)
{
	if (InputInfo.IsValid())
	{
		InputInfo->ResetUISequanceID();
	}
}

#undef LOCTEXT_NAMESPACE
