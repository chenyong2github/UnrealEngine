// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayNodes/SequencerSectionKeyAreaNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "SSequencer.h"
#include "IKeyArea.h"
#include "SKeyNavigationButtons.h"
#include "SKeyAreaEditorSwitcher.h"
#include "CurveModel.h"


/* FSectionKeyAreaNode interface
 *****************************************************************************/

FSequencerSectionKeyAreaNode::FSequencerSectionKeyAreaNode(FName NodeName, FSequencerNodeTree& InParentTree)
	: FSequencerDisplayNode(NodeName, InParentTree)
{
}

void FSequencerSectionKeyAreaNode::AddKeyArea(TSharedRef<IKeyArea> KeyArea)
{
	KeyArea->TreeSerialNumber = TreeSerialNumber;
	KeyAreas.Add(KeyArea);
}

void FSequencerSectionKeyAreaNode::RemoveStaleKeyAreas()
{
	for (int32 Index = KeyAreas.Num() - 1; Index >= 0; --Index)
	{
		if (KeyAreas[Index]->TreeSerialNumber != TreeSerialNumber)
		{
			KeyAreas.RemoveAt(Index, 1, false);
		}
	}
}

TSharedPtr<IKeyArea> FSequencerSectionKeyAreaNode::GetKeyArea(UMovieSceneSection* Section) const
{
	for (const TSharedRef<IKeyArea>& KeyArea : KeyAreas)
	{
		if (KeyArea->GetOwningSection() == Section)
		{
			return KeyArea;
		}
	}
	return nullptr;
}

/* FSequencerDisplayNode interface
 *****************************************************************************/

bool FSequencerSectionKeyAreaNode::CanRenameNode() const
{
	return false;
}

EVisibility FSequencerSectionKeyAreaNode::GetKeyEditorVisibility() const
{
	return KeyAreas.Num() == 0 ? EVisibility::Collapsed : EVisibility::Visible;
}

TSharedRef<SWidget> FSequencerSectionKeyAreaNode::GetCustomOutlinerContent()
{
	// Even if this key area node doesn't have any key areas right now, it may in the future, so we always create the switcher, and just hide it if it is not relevant
	return SNew(SHorizontalBox)
		.Visibility(this, &FSequencerSectionKeyAreaNode::GetKeyEditorVisibility)

		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SNew(SKeyAreaEditorSwitcher, SharedThis(this))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SKeyNavigationButtons, SharedThis(this))
		];
}


FText FSequencerSectionKeyAreaNode::GetDisplayName() const
{
	return DisplayName;
}


float FSequencerSectionKeyAreaNode::GetNodeHeight() const
{
	//@todo sequencer: should be defined by the key area probably
	return SequencerLayoutConstants::KeyAreaHeight;
}


FNodePadding FSequencerSectionKeyAreaNode::GetNodePadding() const
{
	return FNodePadding(0.f);//FNodePadding(0.f, 1.f);
}


ESequencerNode::Type FSequencerSectionKeyAreaNode::GetType() const
{
	return ESequencerNode::KeyArea;
}


void FSequencerSectionKeyAreaNode::SetDisplayName(const FText& NewDisplayName)
{
	check(false);
}

FSlateFontInfo FSequencerSectionKeyAreaNode::GetDisplayNameFont() const
{
	bool bAnyKeyAreaAnimated = false;
	for (const TSharedRef<IKeyArea>& KeyArea : KeyAreas)
	{
		FMovieSceneChannel* Channel = KeyArea->ResolveChannel();
		if (Channel && Channel->GetNumKeys() > 0)
		{
			return FEditorStyle::GetFontStyle("Sequencer.AnimationOutliner.ItalicFont");
		}
	}
	return FSequencerDisplayNode::GetDisplayNameFont();
}

void FSequencerSectionKeyAreaNode::CreateCurveModels(TArray<TUniquePtr<FCurveModel>>& OutCurveModels)
{
	TSharedRef<ISequencer> Sequencer = GetSequencer().AsShared();

	for (const TSharedRef<IKeyArea>& KeyArea : KeyAreas)
	{
		TUniquePtr<FCurveModel> NewCurve = KeyArea->CreateCurveEditorModel(Sequencer);
		if (NewCurve.IsValid())
		{
			OutCurveModels.Add(MoveTemp(NewCurve));
		}
	}
}
