// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SequencerSectionLayoutBuilder.h"
#include "DisplayNodes/SequencerSectionCategoryNode.h"
#include "Channels/MovieSceneChannelEditorData.h"
#include "IKeyArea.h"


FSequencerSectionLayoutBuilder::FSequencerSectionLayoutBuilder(TSharedRef<FSequencerTrackNode> InRootTrackNode, UMovieSceneSection* InSection)
	: RootNode(InRootTrackNode)
	, CurrentNode(InRootTrackNode)
	, Section(InSection)
	, bHasAnyLayout(false)
{}

void FSequencerSectionLayoutBuilder::PushCategory( FName CategoryName, const FText& DisplayLabel )
{
	bHasAnyLayout = true;

	TSharedPtr<FSequencerSectionCategoryNode> CategoryNode;

	// Attempt to re-use an existing key area of the same name
	for (const TSharedRef<FSequencerDisplayNode>& Child : CurrentNode->GetChildNodes())
	{
		if (Child->GetType() == ESequencerNode::Category && Child->GetNodeName() == CategoryName)
		{
			// Ensure its name is up to date
			CategoryNode = StaticCastSharedRef<FSequencerSectionCategoryNode>(Child);
		}
	}

	if (!CategoryNode)
	{
		CategoryNode = MakeShared<FSequencerSectionCategoryNode>(CategoryName, RootNode->GetParentTree());
		CategoryNode->SetParent(CurrentNode);
	}

	CategoryNode->DisplayName = DisplayLabel;
	CategoryNode->TreeSerialNumber = RootNode->TreeSerialNumber;
	CurrentNode = CategoryNode.ToSharedRef();
}

void FSequencerSectionLayoutBuilder::PopCategory()
{
	// Pop a category if the current node is a category
	if( TSharedPtr<FSequencerDisplayNode> Parent = CurrentNode->GetParent() )
	{
		if (CurrentNode->GetType() == ESequencerNode::Category)
		{
			CurrentNode = Parent.ToSharedRef();
		}
	}
}

void FSequencerSectionLayoutBuilder::SetTopLevelChannel( const FMovieSceneChannelHandle& Channel )
{
	bHasAnyLayout = true;

	ensureAlwaysMsgf(CurrentNode == RootNode, TEXT("Attempting to assign a top level channel when a category node is active. Top level key nodes will always be added to the outermost track node."));

	TSharedPtr<FSequencerSectionKeyAreaNode> KeyAreaNode = RootNode->GetTopLevelKeyNode();
	if (!KeyAreaNode.IsValid())
	{
		KeyAreaNode = MakeShared<FSequencerSectionKeyAreaNode>(RootNode->GetNodeName(), RootNode->GetParentTree());
		// does this need a parent?
		KeyAreaNode->SetParentDirectly(RootNode);

		RootNode->SetTopLevelKeyNode(KeyAreaNode);
	}

	AddOrUpdateChannel(KeyAreaNode.ToSharedRef(), Channel);
}

void FSequencerSectionLayoutBuilder::AddChannel( const FMovieSceneChannelHandle& Channel )
{
	// @todo: this is all pretty crusy - we're currently linear-searching for both the child node, and the IKeyArea within that node
	// Performance is generally acceptible however since we are dealing with small numbers of children, but this may need to be revisited.
	const FMovieSceneChannelMetaData* MetaData = Channel.GetMetaData();
	if (!ensureAlwaysMsgf(MetaData, TEXT("Attempting to add an expired channel handle to the node tree")))
	{
		return;
	}

	bHasAnyLayout = true;

	TSharedPtr<FSequencerSectionKeyAreaNode> KeyAreaNode;

	// Attempt to re-use an existing key area of the same name
	for (const TSharedRef<FSequencerDisplayNode>& Child : CurrentNode->GetChildNodes())
	{
		if (Child->GetType() == ESequencerNode::KeyArea && Child->GetNodeName() == MetaData->Name)
		{
			KeyAreaNode = StaticCastSharedRef<FSequencerSectionKeyAreaNode>(Child);
		}
	}

	if (!KeyAreaNode.IsValid())
	{
		// No existing node found make a new one
		KeyAreaNode = MakeShared<FSequencerSectionKeyAreaNode>(MetaData->Name, CurrentNode->GetParentTree());
		KeyAreaNode->DisplayName = MetaData->DisplayText;
		KeyAreaNode->SetParent(CurrentNode);
	}

	AddOrUpdateChannel(KeyAreaNode.ToSharedRef(), Channel);
}

void FSequencerSectionLayoutBuilder::AddOrUpdateChannel(TSharedRef<FSequencerSectionKeyAreaNode> KeyAreaNode, const FMovieSceneChannelHandle& Channel)
{
	KeyAreaNode->TreeSerialNumber = RootNode->TreeSerialNumber;

	TSharedPtr<IKeyArea> KeyArea = KeyAreaNode->GetKeyArea(Section);
	if (!KeyArea)
	{
		// No key area for this section exists - create a new one
		TSharedRef<IKeyArea> NewKeyArea = MakeShared<IKeyArea>(Section, Channel);
		KeyAreaNode->AddKeyArea(NewKeyArea);
	}
	else if (KeyArea->GetChannel() != Channel)
	{
		// A key area exists but for a different channel handle so this needs re-creating
		KeyArea->Reinitialize(Section, Channel);
	}
	else
	{
		// Just ensure the name is up to date
		const FMovieSceneChannelMetaData* MetaData = Channel.GetMetaData();
		if (!ensureAlwaysMsgf(MetaData, TEXT("Attempting to update an expired channel handle to the node tree")))
		{
			return;
		}

		KeyArea->SetName(MetaData->Name);
	}
}

