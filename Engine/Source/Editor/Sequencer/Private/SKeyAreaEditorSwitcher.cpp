// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SKeyAreaEditorSwitcher.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Sequencer.h"
#include "SKeyNavigationButtons.h"
#include "IKeyArea.h"
#include "DisplayNodes/SequencerSectionKeyAreaNode.h"

void SKeyAreaEditorSwitcher::Construct(const FArguments& InArgs, TSharedRef<FSequencerSectionKeyAreaNode> InKeyAreaNode)
{
	WeakKeyAreaNode = InKeyAreaNode;

	Rebuild();
}

int32 SKeyAreaEditorSwitcher::GetWidgetIndex() const
{
	return VisibleIndex;
}

void SKeyAreaEditorSwitcher::Rebuild()
{
	TSharedPtr<FSequencerSectionKeyAreaNode> KeyAreaNode = WeakKeyAreaNode.Pin();
	if (!KeyAreaNode.IsValid())
	{
		// Empty our cache so we don't persistently rebuild
		CachedKeyAreas.Empty();

		// Node is no longer valid so just nuke everything and make this a null widget
		ChildSlot
		[
			SNullWidget::NullWidget
		];
		return;
	}

	const bool bIsEnabled = !KeyAreaNode->GetSequencer().IsReadOnly();

	// Update the cached list so we know when to rebuild next
	CachedKeyAreas = KeyAreaNode->GetAllKeyAreas();

	// Index 0 is always the spacer node
	VisibleIndex = 0;

	TSharedRef<SWidgetSwitcher> Switcher = SNew(SWidgetSwitcher)
		.IsEnabled(bIsEnabled)
		.WidgetIndex(this, &SKeyAreaEditorSwitcher::GetWidgetIndex)

		+ SWidgetSwitcher::Slot()
		[
			SNullWidget::NullWidget
		];

	TSharedPtr<FSequencerObjectBindingNode> ParentObjectBinding = KeyAreaNode->FindParentObjectBindingNode();
	FGuid ObjectBindingID = ParentObjectBinding.IsValid() ? ParentObjectBinding->GetObjectBinding() : FGuid();

	for (TSharedRef<IKeyArea> KeyArea : CachedKeyAreas)
	{
		if (!KeyArea->CanCreateKeyEditor())
		{
			// Always generate a slot so that indices line up correctly
			Switcher->AddSlot()
			[
				SNullWidget::NullWidget
			];
		}
		else
		{
			Switcher->AddSlot()
			[
				SNew(SBox)
				.IsEnabled(bIsEnabled)
				.MinDesiredWidth(100)
				.HAlign(HAlign_Left)
				[
					KeyArea->CreateKeyEditor(KeyAreaNode->GetSequencer().AsShared(), ObjectBindingID)
				]
			];
		}
	}

	ChildSlot
	[
		Switcher
	];
}

void SKeyAreaEditorSwitcher::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	TSharedPtr<FSequencerSectionKeyAreaNode> KeyAreaNode = WeakKeyAreaNode.Pin();
	if (!KeyAreaNode.IsValid())
	{
		if (CachedKeyAreas.Num() != 0)
		{
			// Node is not valid but we have a valid cache - we need to rebuild the switcher now
			Rebuild();
		}
		return;
	}
	else
	{
		if (CachedKeyAreas != KeyAreaNode->GetAllKeyAreas())
		{
			// Node is valid but now has a different set of key areas. Must rebuild the widgets.
			Rebuild();
		}

		TArray<UMovieSceneSection*> AllSections;
		for (const TSharedRef<IKeyArea>& KeyArea : CachedKeyAreas)
		{
			AllSections.Add(KeyArea->GetOwningSection());
		}

		const int32 ActiveKeyArea = SequencerHelpers::GetSectionFromTime(AllSections, KeyAreaNode->GetSequencer().GetLocalTime().Time.FrameNumber);
		if (ActiveKeyArea != INDEX_NONE)
		{
			// Index 0 is the spacer node, so add 1 to the key area index to get the widget index
			VisibleIndex = 1 + ActiveKeyArea;
		}
		else
		{
			VisibleIndex = 0;
		}
	}
}
