// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXInputConsole.h"
#include "Widgets/SDMXInputInfoSelecter.h"
#include "Widgets/SDMXInputInfo.h"
#include "Widgets/SDMXInputInfo.h"
#include "Widgets/Layout/SSeparator.h"

#include "Widgets/SBoxPanel.h"
#include "DMXEditorLog.h"

#define LOCTEXT_NAMESPACE "SDMXInputConsole"

void SDMXInputConsole::Construct(const FArguments& InArgs)
{
	InputInfoSelecter = SNew(SDMXInputInfoSelecter)
		.OnListenForChanged(this, &SDMXInputConsole::OnListenForChanged)
		.OnUniverseSelectionChanged(this, &SDMXInputConsole::OnUniverseSelectionChanged)
		.OnClearUniverses(this, &SDMXInputConsole::OnClearUniverses)
		.OnClearChannelsView(this, &SDMXInputConsole::OnClearChannelsView);

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

	/** Choose which monitor we want to watch based on saved selection */
	InputInfoSelecter->InitializeInputInfo();
}

void SDMXInputConsole::OnListenForChanged(const FName& InListenFor)
{
	if (InputInfo.IsValid())
	{
		if (InListenFor == SDMXInputInfoSelecter::LookForAddresses)
		{
			InputInfo->ChangeToLookForAddresses();
		}
		else if (InListenFor == SDMXInputInfoSelecter::LookForUniverses)
		{
			InputInfo->ChangeToLookForUniverses();
		}
		else
		{
			UE_LOG_DMXEDITOR(Error, TEXT("Unknown Listen For Selector: %s!"), *(InListenFor.ToString()));
		}
	}
}

void SDMXInputConsole::OnClearUniverses()
{
	if (InputInfo.IsValid())
	{
		InputInfo->ClearUniverses();
	}
}

void SDMXInputConsole::OnClearChannelsView()
{
	if (InputInfo.IsValid())
	{
		InputInfo->ClearChannelsView();
	}
	}

void SDMXInputConsole::OnUniverseSelectionChanged(const FName& InProtocol)
{
	if (InputInfo.IsValid())
	{
		InputInfo->UniverseSelectionChanged();

	}
}


#undef LOCTEXT_NAMESPACE
