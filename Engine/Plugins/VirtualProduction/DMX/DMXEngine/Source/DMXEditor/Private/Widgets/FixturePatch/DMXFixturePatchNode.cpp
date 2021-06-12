// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXFixturePatchNode.h"

#include "DMXEditor.h"
#include "DMXFixturePatchEditorDefinitions.h"
#include "DMXFixturePatchSharedData.h"
#include "SDMXFixturePatchFragment.h"
#include "SDMXPatchedUniverse.h"
#include "Library/DMXEntityFixturePatch.h"

#include "ScopedTransaction.h"
#include "Widgets/Layout/SGridPanel.h"


#define LOCTEXT_NAMESPACE "DMXFixturePatchNode"

void FDMXFixturePatchFragment::CreateFragments(TWeakObjectPtr<UDMXEntityFixturePatch> InFixturePatch, int32 ChannelID, TArray<TSharedPtr<FDMXFixturePatchFragment>>& OutFragments)
{
	OutFragments.Reset();
	if (!InFixturePatch.IsValid())
	{
		return;
	}

	// Remove ID offset
	ChannelID = ChannelID - FDMXChannelGridSpecs::ChannelIDOffset;

	int32 ChannelSpan = InFixturePatch->GetChannelSpan();
	check(ChannelSpan > 0);

	// Create a node for each row, at least one node
	TSharedPtr<FDMXFixturePatchFragment> PrevFragment;	
	do
	{
		TSharedPtr<FDMXFixturePatchFragment> NewFragment = MakeShared<FDMXFixturePatchFragment>();

		int32 Column = ChannelID % FDMXChannelGridSpecs::NumColumns;
		int32 Row = ChannelID / FDMXChannelGridSpecs::NumColumns;
		int32 ColumnSpan = Column + ChannelSpan < FDMXChannelGridSpecs::NumColumns ? ChannelSpan : FDMXChannelGridSpecs::NumColumns - Column;

		NewFragment->ID = ChannelID;
		NewFragment->Column = Column;
		NewFragment->Row = Row;
		NewFragment->ColumnSpan = ColumnSpan;

		OutFragments.Add(NewFragment);

		if (PrevFragment.IsValid())
		{
			PrevFragment->RhsFragment = NewFragment;
		}

		// Update for next node
		ChannelSpan -= ColumnSpan;
		ChannelID += ColumnSpan;
	} while (ChannelSpan > 0);

	// link lhs
	TSharedPtr<FDMXFixturePatchFragment> LhsLink = OutFragments[0];
	for (int idxNode = 1; idxNode < OutFragments.Num(); idxNode++)
	{
		OutFragments[idxNode]->LhsFragment = LhsLink;
		LhsLink = OutFragments[idxNode];
	}
}

TArray<TSharedPtr<FDMXFixturePatchFragment>> FDMXFixturePatchFragment::GetFragments()
{
	TArray<TSharedPtr<FDMXFixturePatchFragment>> Fragments;
	TSharedPtr<FDMXFixturePatchFragment> Root = SharedThis(this);
	while(Root->LhsFragment.IsValid())
	{
		Root = LhsFragment;
	}

	TSharedPtr<FDMXFixturePatchFragment> Fragment = Root;
	while (Fragment.IsValid())
	{
		Fragments.Add(Fragment);
		Fragment = Fragment->RhsFragment;
	}

	return Fragments;
}

TSharedPtr<FDMXFixturePatchNode> FDMXFixturePatchNode::Create(TWeakPtr<FDMXEditor> InDMXEditor, TWeakObjectPtr<UDMXEntityFixturePatch> InFixturePatch)
{
	TSharedPtr<FDMXFixturePatchNode> NewNode = MakeShared<FDMXFixturePatchNode>();

	NewNode->DMXEditor = InDMXEditor;
	NewNode->FixturePatch = InFixturePatch;
	NewNode->LastTransactedUniverseID = InFixturePatch->GetUniverseID();
	NewNode->LastTransactedChannelID = InFixturePatch->GetStartingChannel();

	// Bind to shared data's selection event 
	if (TSharedPtr<FDMXEditor> PinnedDMXEditor = NewNode->DMXEditor.Pin())
	{
		TSharedPtr<FDMXFixturePatchSharedData> SharedData = PinnedDMXEditor->GetFixturePatchSharedData();
		check(SharedData.IsValid());

		SharedData->OnFixturePatchSelectionChanged.AddSP(NewNode.ToSharedRef(), &FDMXFixturePatchNode::OnSelectionChanged);

		// Select the new node if it is selected in the entity list
		if (InFixturePatch.IsValid())
		{
			if (SharedData->GetSelectedFixturePatches().Contains(NewNode->FixturePatch))
			{
				NewNode->bSelected = true;
			}
		}	
	}

	return NewNode;
}

void FDMXFixturePatchNode::Update(TSharedPtr<SDMXPatchedUniverse> NewUniverse, int32 NewStartingChannel, int32 NewChannelSpan)
{
	check(NewUniverse.IsValid());
	check(NewStartingChannel > 0 && NewStartingChannel <= DMX_UNIVERSE_SIZE);
	check(NewChannelSpan > 0);

	Universe = NewUniverse;
	StartingChannel = NewStartingChannel;
	ChannelSpan = NewChannelSpan;

	// Fragment the patch to fit the ID
	TArray<TSharedPtr<FDMXFixturePatchFragment>> NewFragments;
	FDMXFixturePatchFragment::CreateFragments(FixturePatch, NewStartingChannel, NewFragments);


	// Update the Fragmented Widgets, reuse existing if possible
	if (NewFragments.Num() == FragmentedWidgets.Num())
	{
		for (int32 IdxFragment = 0; IdxFragment < NewFragments.Num(); IdxFragment++)
		{
			TSharedPtr<SDMXFixturePatchFragment> Widget = FragmentedWidgets[IdxFragment];
			Widget->SetColumn(NewFragments[IdxFragment]->Column);
			Widget->SetRow(NewFragments[IdxFragment]->Row);
			Widget->SetColumnSpan(NewFragments[IdxFragment]->ColumnSpan);
		}
	}
	else
	{
		FragmentedWidgets.Reset();
		for (const TSharedPtr<FDMXFixturePatchFragment>& Fragment : NewFragments)
		{
			FragmentedWidgets.Add(
				SNew(SDMXFixturePatchFragment, SharedThis(this))
				.DMXEditor(DMXEditor)
				.Text(FText::FromString(FixturePatch->GetDisplayName()))
				.Column(Fragment->Column)
				.Row(Fragment->Row)
				.ColumnSpan(Fragment->ColumnSpan)
				.bHighlight(bSelected)
				.Visibility(EVisibility::HitTestInvisible)
			);
		}
	}
}

void FDMXFixturePatchNode::CommitPatch(bool bTransacted)
{
	if (!FixturePatch.IsValid())
	{		
		return;
	}
	
	if (bTransacted)
	{	
		// As we allow the asset to update even when not transacted,
		// We have to set the last known values to enable proper undo
		FixturePatch->SetUniverseID(LastTransactedUniverseID);
		FixturePatch->SetManualStartingAddress(LastTransactedChannelID);

		const FScopedTransaction Transaction = FScopedTransaction(LOCTEXT("FixturePatchAssigned", "Patched Fixture"));
		FixturePatch->Modify();

		FixturePatch->SetUniverseID(Universe->GetUniverseID());
		FixturePatch->SetManualStartingAddress(StartingChannel);

		LastTransactedUniverseID = Universe->GetUniverseID();
		LastTransactedChannelID = StartingChannel;
	}
	else
	{
		FixturePatch->SetUniverseID(Universe->GetUniverseID());
		FixturePatch->SetManualStartingAddress(StartingChannel);
	}
}

bool FDMXFixturePatchNode::IsPatched() const
{
	return Universe.IsValid();
}

bool FDMXFixturePatchNode::IsSelected() const
{
	return bSelected;
}

void FDMXFixturePatchNode::OnSelectionChanged()
{
	if (TSharedPtr<FDMXEditor> PinnedDMXEditor = DMXEditor.Pin())
	{
		TSharedPtr<FDMXFixturePatchSharedData> SharedData = PinnedDMXEditor->GetFixturePatchSharedData();
		check(SharedData.IsValid());

		if (SharedData->GetSelectedFixturePatches().Contains(FixturePatch))
		{
			for (const TSharedPtr<SDMXFixturePatchFragment>& Fragment : FragmentedWidgets)
			{
				Fragment->SetHighlight(true);
			}

			bSelected = true;
		}
		else
		{
			for (const TSharedPtr<SDMXFixturePatchFragment>& Fragment : FragmentedWidgets)
			{
				Fragment->SetHighlight(false);
			}

			bSelected = false;
		}
	}
}

bool FDMXFixturePatchNode::OccupiesChannels(int32 Channel, int32 Span) const
{
	check(Span != 0);
	if ((Channel + Span <= StartingChannel) || (Channel >= StartingChannel + ChannelSpan))
	{
		return false;
	} 
	return true;
}

#undef LOCTEXT_NAMESPACE
