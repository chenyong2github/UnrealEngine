// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerSectionLayoutBuilder.h"
#include "DisplayNodes/SequencerSectionCategoryNode.h"
#include "Channels/MovieSceneChannelEditorData.h"
#include "IKeyArea.h"


FSequencerSectionLayoutBuilder::FSequencerSectionLayoutBuilder(TSharedRef<FSequencerTrackNode> InRootTrackNode, UMovieSceneSection* InSection)
	: RootNode(InRootTrackNode)
	, CurrentNode(InRootTrackNode)
	, Section(InSection)
	, bHasAnyLayout(false)
{
	InsertIndexStack.Add(0);
}

TSharedPtr<FSequencerDisplayNode> FindAndRelocateExistingNode(TSharedRef<FSequencerDisplayNode> ParentNode, int32 ExpectedIndex, ESequencerNode::Type NodeType, FName NodeName)
{
	auto MatchNode = [NodeType, NodeName](const TSharedRef<FSequencerDisplayNode>& InNode)
	{
		return InNode->GetType() == NodeType && InNode->GetNodeName() == NodeName;
	};

	const TArray<TSharedRef<FSequencerDisplayNode>>& CurrentChildren = ParentNode->GetChildNodes();

	if (!ensureAlwaysMsgf(ExpectedIndex <= CurrentChildren.Num(), TEXT("Invalid desired index specified")))
	{
		ExpectedIndex = FMath::Clamp(ExpectedIndex, 0, CurrentChildren.Num());
	}

	// Common up-to-date case: check the desired insert index for an existing node that matches
	if (CurrentChildren.IsValidIndex(ExpectedIndex) && MatchNode(CurrentChildren[ExpectedIndex]))
	{
		// Node already exists and is in the correct position
		return CurrentChildren[ExpectedIndex];
	}
	// Rare-case: find an existing match at the wrong index, and move it to the correct index
	else for (int32 Index = 0; Index < CurrentChildren.Num(); ++Index)
	{
		if (MatchNode(CurrentChildren[Index]))
		{
			// *Important - we copy the child here so that it's still valid after it gets moved
			TSharedRef<FSequencerDisplayNode> Child = CurrentChildren[Index];

			// Node already exists but is at the wrong index - needs moving
			ParentNode->MoveChild(Index, ExpectedIndex);

			return Child;
		}
	}

	return nullptr;
}

void FSequencerSectionLayoutBuilder::PushCategory( FName CategoryName, const FText& DisplayLabel )
{
	bHasAnyLayout = true;

	const int32 DesiredInsertIndex = InsertIndexStack.Last();

	TSharedPtr<FSequencerDisplayNode>         ExistingNode = FindAndRelocateExistingNode(CurrentNode, DesiredInsertIndex, ESequencerNode::Category, CategoryName);
	TSharedPtr<FSequencerSectionCategoryNode> CategoryNode = StaticCastSharedPtr<FSequencerSectionCategoryNode>(ExistingNode);

	if (!ExistingNode)
	{
		CategoryNode = MakeShared<FSequencerSectionCategoryNode>(CategoryName, RootNode->GetParentTree());
		CategoryNode->SetParent(CurrentNode, DesiredInsertIndex);
	}

	CategoryNode->DisplayName = DisplayLabel;
	CategoryNode->TreeSerialNumber = RootNode->TreeSerialNumber;
	CurrentNode = CategoryNode.ToSharedRef();
	
	// Move onto the next index at this level
	++InsertIndexStack.Last();

	// Add a new index to add to inside the new current node
	InsertIndexStack.Add(0);
}

void FSequencerSectionLayoutBuilder::PopCategory()
{
	// Pop a category if the current node is a category
	if( TSharedPtr<FSequencerDisplayNode> Parent = CurrentNode->GetParent() )
	{
		if (CurrentNode->GetType() == ESequencerNode::Category)
		{
			CurrentNode = Parent.ToSharedRef();
			InsertIndexStack.Pop();
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

	const int32 DesiredInsertIndex = InsertIndexStack.Last();

	TSharedPtr<FSequencerDisplayNode>        ExistingNode = FindAndRelocateExistingNode(CurrentNode, DesiredInsertIndex, ESequencerNode::KeyArea, MetaData->Name);
	TSharedPtr<FSequencerSectionKeyAreaNode> KeyAreaNode  = StaticCastSharedPtr<FSequencerSectionKeyAreaNode>(ExistingNode);

	if (!KeyAreaNode.IsValid())
	{
		// No existing node found make a new one
		KeyAreaNode = MakeShared<FSequencerSectionKeyAreaNode>(MetaData->Name, CurrentNode->GetParentTree());
		KeyAreaNode->DisplayName = MetaData->DisplayText;
		KeyAreaNode->SetParent(CurrentNode, DesiredInsertIndex);
	}

	AddOrUpdateChannel(KeyAreaNode.ToSharedRef(), Channel);

	// Move onto the next index at this level
	int32& NextIndex = InsertIndexStack.Last();
	NextIndex = FMath::Clamp(NextIndex + 1, 0, CurrentNode->GetChildNodes().Num());
}

void FSequencerSectionLayoutBuilder::AddOrUpdateChannel(TSharedRef<FSequencerSectionKeyAreaNode> KeyAreaNode, const FMovieSceneChannelHandle& Channel)
{
	const FMovieSceneChannelMetaData* MetaData = Channel.GetMetaData();
	if (!ensureAlwaysMsgf(MetaData, TEXT("Attempting to update an expired channel handle to the node tree")))
	{
		return;
	}

	KeyAreaNode->TreeSerialNumber = RootNode->TreeSerialNumber;

	TSharedPtr<IKeyArea> KeyArea = KeyAreaNode->GetKeyArea(Section);
	if (!KeyArea)
	{
		// No key area for this section exists - create a new one
		TSharedRef<IKeyArea> NewKeyArea = MakeShared<IKeyArea>(Section, Channel);
		KeyAreaNode->AddKeyArea(NewKeyArea);
		return;
	}

	KeyArea->TreeSerialNumber = RootNode->TreeSerialNumber;
	if (KeyArea->GetChannel() != Channel)
	{
		// A key area exists but for a different channel handle so this needs re-creating
		KeyArea->Reinitialize(Section, Channel);
	}
	else
	{
		// Just ensure the name is up to date
		KeyArea->SetName(MetaData->Name);
	}
}

