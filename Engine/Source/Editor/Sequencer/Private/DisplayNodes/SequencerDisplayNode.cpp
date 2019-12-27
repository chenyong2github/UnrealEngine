// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayNodes/SequencerDisplayNode.h"
#include "Curves/KeyHandle.h"
#include "Widgets/SNullWidget.h"
#include "Rendering/DrawElements.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"
#include "Textures/SlateIcon.h"
#include "Framework/Commands/UIAction.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Images/SImage.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorStyleSet.h"
#include "DisplayNodes/SequencerObjectBindingNode.h"
#include "DisplayNodes/SequencerSectionCategoryNode.h"
#include "DisplayNodes/SequencerSectionKeyAreaNode.h"
#include "IKeyArea.h"
#include "DisplayNodes/SequencerTrackNode.h"
#include "Sequencer.h"
#include "SAnimationOutlinerTreeNode.h"
#include "SequencerSettings.h"
#include "SSequencerSectionAreaView.h"
#include "CommonMovieSceneTools.h"
#include "Framework/Commands/GenericCommands.h"
#include "CurveEditor.h"
#include "Tree/SCurveEditorTreePin.h"
#include "Tree/CurveEditorTreeFilter.h"
#include "ScopedTransaction.h"
#include "SequencerKeyTimeCache.h"
#include "SequencerNodeSortingMethods.h"
#include "SequencerSelectionCurveFilter.h"
#include "SequencerKeyCollection.h"

#define LOCTEXT_NAMESPACE "SequencerDisplayNode"


namespace SequencerNodeConstants
{
	extern const float CommonPadding;
	const float CommonPadding = 4.f;

	static const FVector2D KeyMarkSize = FVector2D(3.f, 21.f);

	static const uint8 DefaultSortBias[(__underlying_type(EDisplayNodeSortType))EDisplayNodeSortType::NUM] = {
		2, // Folders
		3, // Tracks
		4, // ObjectBindings
		1, // CameraCuts
		0, // Shots
		5, // Anything else
	};

	static const uint8 ObjectBindingSortBias[(__underlying_type(EDisplayNodeSortType))EDisplayNodeSortType::NUM] = {
		2, // Folders            - shouldn't exist inside object bindings
		1, // Tracks
		0, // ObjectBindings
		3, // CameraCuts         - shouldn't exist inside object bindings
		4, // Shots              - shouldn't exist inside object bindings
		5, // Anything else
	};

	static_assert(UE_ARRAY_COUNT(DefaultSortBias) == (__underlying_type(EDisplayNodeSortType))EDisplayNodeSortType::NUM, "Mismatched type/bias count");
	static_assert(UE_ARRAY_COUNT(ObjectBindingSortBias) == (__underlying_type(EDisplayNodeSortType))EDisplayNodeSortType::NUM, "Mismatched type/bias count");

	inline bool SortChildrenWithBias(const TSharedRef<FSequencerDisplayNode>& A, const TSharedRef<FSequencerDisplayNode>& B, const uint8* SortBias)
	{
		const uint8 BiasA = SortBias[(__underlying_type(EDisplayNodeSortType))A->GetSortType()];
		const uint8 BiasB = SortBias[(__underlying_type(EDisplayNodeSortType))B->GetSortType()];

		// For nodes of the same bias, sort by name
		if (BiasA == BiasB)
		{
			const int32 Compare = A->GetDisplayName().CompareToCaseIgnored(B->GetDisplayName());

			if (Compare != 0)
			{
				return Compare < 0;
			}

			// If the nodes have the same name, try to maintain current sorting order
			const int32 SortA = A->GetSortingOrder();
			const int32 SortB = B->GetSortingOrder();

			if (SortA >= 0 && SortB >= 0)
			{
				// Both nodes have persistent sort orders, use those
				return SortA < SortB;
			}
			else if (SortA >= 0 || SortB >= 0)
			{
				// Only one nodes has a persistent sort order, list it first
				return SortA > SortB;
			}
			
			// If same name and neither has a persistent sort order, then report them as equal
			return false;

		}
		return BiasA < BiasB;
	}

	inline bool SortObjectBindingChildren(const TSharedRef<FSequencerDisplayNode>& A, const TSharedRef<FSequencerDisplayNode>& B)
	{
		return SortChildrenWithBias(A, B, SequencerNodeConstants::ObjectBindingSortBias);
	}

	static bool SortChildrenDefault(const TSharedRef<FSequencerDisplayNode>& A, const TSharedRef<FSequencerDisplayNode>& B)
	{
		const int32 SortA = A->GetSortingOrder();
		const int32 SortB = B->GetSortingOrder();

		if (SortA >= 0 && SortB >= 0)
		{
			// Both nodes have persistent sort orders, use those
			return SortA < SortB;
		}

		// When either or neither node has a persistent sort order, we use the default ordering between the two nodes to ensure that 
		// New nodes get added to the correctly sorted position by default
		return SortChildrenWithBias(A, B, SequencerNodeConstants::DefaultSortBias);
	}

	static bool NodeMatchesTextFilterTerm(TSharedPtr<const FSequencerDisplayNode> Node, const FCurveEditorTreeTextFilterTerm& Term)
	{
		bool bMatched = false;

		for (const FCurveEditorTreeTextFilterToken& Token : Term.ChildToParentTokens)
		{
			if (!Node)
			{
				// No match - ran out of parents
				return false;
			}
			else if (!Token.Match(*Node->GetDisplayName().ToString()))
			{
				return false;
			}

			bMatched = true;
			Node = Node->GetParent();
		}

		return bMatched;
	}

	FText GetCurveEditorHighlightText(TWeakPtr<FCurveEditor> InCurveEditor)
	{
		TSharedPtr<FCurveEditor> PinnedCurveEditor = InCurveEditor.Pin();
		if (!PinnedCurveEditor)
		{
			return FText::GetEmpty();
		}

		const FCurveEditorTreeFilter* Filter = PinnedCurveEditor->GetTree()->FindFilterByType(ECurveEditorTreeFilterType::Text);
		if (Filter)
		{
			return static_cast<const FCurveEditorTreeTextFilter*>(Filter)->InputText;
		}

		return FText::GetEmpty();
	}
}

struct FNameAndSignature
{
	FGuid Signature;
	FName Name;

	bool IsValid() const
	{
		return Signature.IsValid() && !Name.IsNone();
	}

	friend bool operator==(const FNameAndSignature& A, const FNameAndSignature& B)
	{
		return A.Signature == B.Signature && A.Name == B.Name;
	}

	friend uint32 GetTypeHash(const FNameAndSignature& In)
	{
		return HashCombine(GetTypeHash(In.Signature), GetTypeHash(In.Name));
	}
};

class SSequencerCombinedKeysTrack
	: public SLeafWidget
{
public:

	SLATE_BEGIN_ARGS(SSequencerCombinedKeysTrack) {}
		/** The view range of the section area */
		SLATE_ATTRIBUTE( TRange<double>, ViewRange )
		/** The tick resolution of the current sequence*/
		SLATE_ATTRIBUTE( FFrameRate, TickResolution )
	SLATE_END_ARGS()

	/** SLeafWidget Interface */
	virtual int32 OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const override;
	virtual void Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime ) override;

	void Construct( const FArguments& InArgs, TSharedRef<FSequencerDisplayNode> InRootNode )
	{
		RootNode = InRootNode;
		
		ViewRange = InArgs._ViewRange;
		TickResolution = InArgs._TickResolution;
	}

protected:
	// Begin SWidget overrides.
	virtual FVector2D ComputeDesiredSize(float) const override;
	// End SWidget overrides.

private:

	/** Collects all key times from the root node */
	void GenerateCachedKeyPositions(const FGeometry& AllottedGeometry);

private:

	/** Root node of this track view panel */
	TSharedPtr<FSequencerDisplayNode> RootNode;

	/** The current view range */
	TAttribute< TRange<double> > ViewRange;
	/** The current tick resolution */
	TAttribute< FFrameRate > TickResolution;

	FSequencerKeyCollectionSignature KeyCollectionSignature;

	/** The cached tick resolution these positions were generated with */
	FFrameRate CachedTickResolution;
	/** The time-range for which KeyDrawPositions was generated */
	TRange<double> CachedViewRange;
	/** Cached pixel positions for all keys in the current view range */
	TArray<float> KeyDrawPositions;
	/** Cached key times per key area. Updated when section signature changes */
	TMap<FNameAndSignature, FSequencerCachedKeys> SectionToKeyTimeCache;
};


void SSequencerCombinedKeysTrack::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	SWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	FSequencerKeyCollectionSignature NewCollectionSignature = FSequencerKeyCollectionSignature::FromNodesRecursive({ RootNode.Get() }, 0);

	TRange<double> OldCachedViewRange = CachedViewRange;
	FFrameRate OldCachedTickResolution = CachedTickResolution;

	CachedViewRange = ViewRange.Get();
	CachedTickResolution = TickResolution.Get();

	if (NewCollectionSignature != KeyCollectionSignature || CachedViewRange != OldCachedViewRange || CachedTickResolution != OldCachedTickResolution)
	{
		KeyCollectionSignature = MoveTemp(NewCollectionSignature);
		GenerateCachedKeyPositions(AllottedGeometry);
	}
}

int32 SSequencerCombinedKeysTrack::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (RootNode->GetSequencer().GetSequencerSettings()->GetShowCombinedKeyframes())
	{
		for (float KeyPosition : KeyDrawPositions)
		{
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId+1,
				AllottedGeometry.ToPaintGeometry(
					FVector2D(
						KeyPosition - FMath::CeilToFloat(SequencerNodeConstants::KeyMarkSize.X/2.f),
						FMath::CeilToFloat(AllottedGeometry.GetLocalSize().Y/2.f - SequencerNodeConstants::KeyMarkSize.Y/2.f)
					),
					SequencerNodeConstants::KeyMarkSize
				),
				FEditorStyle::GetBrush("Sequencer.KeyMark"),
				ESlateDrawEffect::None,
				FLinearColor(1.f, 1.f, 1.f, 1.f)
			);
		}
		return LayerId+1;
	}

	return LayerId;
}


FVector2D SSequencerCombinedKeysTrack::ComputeDesiredSize( float ) const
{
	// Note: X Size is not used
	return FVector2D( 100.0f, RootNode->GetNodeHeight() );
}


void SSequencerCombinedKeysTrack::GenerateCachedKeyPositions(const FGeometry& AllottedGeometry)
{
	static float DuplicateThresholdPx = 3.f;

	// Swap the last frame's cache with a temporary so we start this frame's cache from a clean slate
	TMap<FNameAndSignature, FSequencerCachedKeys> PreviouslyCachedKeyTimes;
	Swap(PreviouslyCachedKeyTimes, SectionToKeyTimeCache);

	// Unnamed key areas are uncacheable, so we track those separately
	TArray<FSequencerCachedKeys> UncachableKeyTimes;

	TArray<double> SectionBoundTimes;

	// First off, accumulate (and cache) KeyDrawPositions as times, we convert to positions in the later loop
	for (auto& CachePair : KeyCollectionSignature.GetKeyAreas())
	{
		TSharedRef<IKeyArea> KeyArea = CachePair.Key;

		UMovieSceneSection* Section = KeyArea->GetOwningSection();

		if (Section)
		{
			if (Section->HasStartFrame())
			{
				SectionBoundTimes.Add(Section->GetInclusiveStartFrame() / CachedTickResolution);
			}

			if (Section->HasEndFrame())
			{
				SectionBoundTimes.Add(Section->GetExclusiveEndFrame() / CachedTickResolution);
			}
		}

		FNameAndSignature CacheKey{ CachePair.Value, KeyArea->GetName() };

		// If we cached this last frame, use those key times again
		FSequencerCachedKeys* CachedKeyTimes = CacheKey.IsValid() ? PreviouslyCachedKeyTimes.Find(CacheKey) : nullptr;
		if (CachedKeyTimes)
		{
			SectionToKeyTimeCache.Add(CacheKey, MoveTemp(*CachedKeyTimes));
			continue;
		}

		// Generate a new cache
		FSequencerCachedKeys TempCache;
		TempCache.Update(KeyArea, CachedTickResolution);

		if (CacheKey.IsValid())
		{
			SectionToKeyTimeCache.Add(CacheKey, MoveTemp(TempCache));
		}
		else
		{
			UncachableKeyTimes.Add(MoveTemp(TempCache));
		}
	}

	KeyDrawPositions.Reset();

	// Instead of accumulating all key times into a single array and then sorting (which doesn't scale well with large numbers),
	// we use a collection of iterators that are only incremented when they've been added to the KeyDrawPositions array
	struct FIter
	{
		FIter(TArrayView<const double> InTimes) : KeysInRange(InTimes), CurrentIndex(0) {}

		explicit      operator bool() const { return KeysInRange.IsValidIndex(CurrentIndex); }
		FIter&        operator++()          { ++CurrentIndex; return *this; }

		double        operator*() const     { return KeysInRange[CurrentIndex]; }
		const double* operator->() const    { return &KeysInRange[CurrentIndex]; }
	private:
		TArrayView<const double> KeysInRange;
		int32 CurrentIndex;
	};

	TArray<FIter> AllIterators;
	for (auto& Pair : SectionToKeyTimeCache)
	{
		TArrayView<const double> Times;
		Pair.Value.GetKeysInRange(CachedViewRange, &Times, nullptr, nullptr);
		AllIterators.Add(Times);
	}
	for (auto& Uncached: UncachableKeyTimes)
	{
		TArrayView<const double> Times;
		Uncached.GetKeysInRange(CachedViewRange, &Times, nullptr, nullptr);
		AllIterators.Add(Times);
	}
	AllIterators.Add(TArrayView<const double>(SectionBoundTimes));

	FTimeToPixel TimeToPixelConverter(AllottedGeometry, CachedViewRange, CachedTickResolution);

	// While any iterator is still valid, find and add the earliest time
	while (AllIterators.ContainsByPredicate([](FIter& It){ return It; }))
	{
		double EarliestTime = TNumericLimits<double>::Max();
		for (FIter& It : AllIterators)
		{
			if (It && *It < EarliestTime)
			{
				EarliestTime = *It;
			}
		}

		// Add the position as a pixel position
		const float KeyPosition = TimeToPixelConverter.SecondsToPixel(EarliestTime);
		KeyDrawPositions.Add(KeyPosition);

		// Increment any other iterators that are close enough to the time we just added
		for (FIter& It : AllIterators)
		{
			while (It && FMath::IsNearlyEqual(KeyPosition, TimeToPixelConverter.SecondsToPixel(*It), DuplicateThresholdPx))
			{
				++It;
			}
		}
	}
}


FSequencerDisplayNode::FSequencerDisplayNode( FName InNodeName, FSequencerNodeTree& InParentTree )
	: TreeSerialNumber(0)
	, VirtualTop( 0.f )
	, VirtualBottom( 0.f )
	, ParentTree( InParentTree )
	, NodeName( InNodeName )
	, bExpanded( false )
	, bPinned( false )
	, bInPinnedBranch( false )
	, bHasBeenInitialized( false )
{
	SortType = EDisplayNodeSortType::Undefined;
}

bool FSequencerDisplayNode::IsParentStillRelevant(uint32 SerialNumber) const
{
	TSharedPtr<FSequencerDisplayNode> ExistingParent = GetParent();
	return ExistingParent.IsValid() && ExistingParent->TreeSerialNumber == SerialNumber;
}

bool FSequencerDisplayNode::IsRootNode() const
{
	return ParentNode == ParentTree.GetRootNode();
}

void FSequencerDisplayNode::SetParentDirectly(TSharedPtr<FSequencerDisplayNode> InParent)
{
	ParentNode = InParent;
}

void FSequencerDisplayNode::SetParent(TSharedPtr<FSequencerDisplayNode> InParent, int32 DesiredChildIndex)
{
	TSharedPtr<FSequencerDisplayNode> CurrentParent = ParentNode.Pin();
	if (CurrentParent != InParent)
	{
		TSharedRef<FSequencerDisplayNode> ThisNode = AsShared();
		if (CurrentParent)
		{
			// Remove from parent
			CurrentParent->ChildNodes.Remove(ThisNode);
		}

		if (InParent)
		{
			// Add to new parent
			if (DesiredChildIndex != INDEX_NONE && ensureMsgf(DesiredChildIndex <= InParent->ChildNodes.Num(), TEXT("Invalid insert index specified")))
			{
				InParent->ChildNodes.Insert(ThisNode, DesiredChildIndex);
			}
			else
			{
				InParent->ChildNodes.Add(ThisNode);
			}

			bExpanded = ParentTree.GetSavedExpansionState( *this );

			if (InParent != ParentTree.GetRootNode())
			{
				bPinned = false;
				ParentTree.SavePinnedState(*this, false);
			}
		}
	}

	ParentNode = InParent;
}

void FSequencerDisplayNode::MoveChild(int32 InChildIndex, int32 InDesiredNewIndex)
{
	check(ChildNodes.IsValidIndex(InChildIndex) && InDesiredNewIndex >= 0 && InDesiredNewIndex <= ChildNodes.Num());

	TSharedRef<FSequencerDisplayNode> Child = ChildNodes[InChildIndex];
	ChildNodes.RemoveAt(InChildIndex, 1, false);

	if (InDesiredNewIndex > InChildIndex)
	{
		// Decrement the desired index to account for the removal
		--InDesiredNewIndex;
	}

	ChildNodes.Insert(Child, InDesiredNewIndex);
}

FSequencer& FSequencerDisplayNode::GetSequencer() const
{
	return ParentTree.GetSequencer();
}

void FSequencerDisplayNode::OnTreeRefreshed(float InVirtualTop, float InVirtualBottom)
{
	if (!bHasBeenInitialized)
	{
		// Assign the saved expansion state when this node is initialized for the first time
		bExpanded = ParentTree.GetSavedExpansionState( *this );
		if (IsRootNode())
		{
			bPinned = ParentTree.GetSavedPinnedState(*this);
		}
	}

	VirtualTop = InVirtualTop;
	VirtualBottom = InVirtualBottom;

	SortImmediateChildren();

	bHasBeenInitialized = true;
}

void FSequencerDisplayNode::SortImmediateChildren()
{
	const ESequencerNode::Type NodeType = GetType();
	if (ChildNodes.Num() == 0 || NodeType == ESequencerNode::Category || NodeType == ESequencerNode::Track)
	{
		return;
	}

	if (NodeType == ESequencerNode::Object)
	{
		// Objects never use their serialized sort order
		Algo::Sort(ChildNodes, SequencerNodeConstants::SortObjectBindingChildren);
	}
	else
	{
		Algo::Sort(ChildNodes, SequencerNodeConstants::SortChildrenDefault);
	}

	if (NodeType != ESequencerNode::Track || static_cast<FSequencerTrackNode*>(this)->GetSubTrackMode() == FSequencerTrackNode::ESubTrackMode::None)
	{
		// Set persistent sort orders
		for (int32 Index = 0; Index < ChildNodes.Num(); ++Index)
		{
			ChildNodes[Index]->SetSortingOrder(Index);
		}
	}
}

void FSequencerDisplayNode::ResortImmediateChildren()
{
	if (ChildNodes.Num() > 0)
	{
		// Unset persistent sort orders
		for (TSharedRef<FSequencerDisplayNode> Child : ChildNodes)
		{
			Child->SetSortingOrder(-1);
		}

		SortImmediateChildren();
	}
}

TSharedPtr<FSequencerObjectBindingNode> FSequencerDisplayNode::FindParentObjectBindingNode() const
{
	TSharedPtr<FSequencerDisplayNode> CurrentParentNode = GetParent();

	while (CurrentParentNode.IsValid())
	{
		if (CurrentParentNode->GetType() == ESequencerNode::Object)
		{
			return StaticCastSharedPtr<FSequencerObjectBindingNode>(CurrentParentNode);
		}
		CurrentParentNode = CurrentParentNode->GetParent();
	}

	return nullptr;
}

FGuid FSequencerDisplayNode::GetObjectGuid() const
{
	TSharedPtr<FSequencerObjectBindingNode> ObjectBindingNode = FindParentObjectBindingNode();
	return ObjectBindingNode ? ObjectBindingNode->GetObjectBinding() : FGuid();
}

bool FSequencerDisplayNode::Traverse_ChildFirst(const TFunctionRef<bool(FSequencerDisplayNode&)>& InPredicate, bool bIncludeThisNode)
{
	for (auto& Child : GetChildNodes())
	{
		if (!Child->Traverse_ChildFirst(InPredicate, true))
		{
			return false;
		}
	}

	return bIncludeThisNode ? InPredicate(*this) : true;
}


bool FSequencerDisplayNode::Traverse_ParentFirst(const TFunctionRef<bool(FSequencerDisplayNode&)>& InPredicate, bool bIncludeThisNode)
{
	if (bIncludeThisNode && !InPredicate(*this))
	{
		return false;
	}

	for (auto& Child : GetChildNodes())
	{
		if (!Child->Traverse_ParentFirst(InPredicate, true))
		{
			return false;
		}
	}

	return true;
}


bool FSequencerDisplayNode::TraverseVisible_ChildFirst(const TFunctionRef<bool(FSequencerDisplayNode&)>& InPredicate, bool bIncludeThisNode)
{
	// If the item is not expanded, its children ain't visible
	if (IsExpanded())
	{
		for (auto& Child : GetChildNodes())
		{
			if (!Child->IsHidden() && !Child->TraverseVisible_ChildFirst(InPredicate, true))
			{
				return false;
			}
		}
	}

	if (bIncludeThisNode && !IsHidden())
	{
		return InPredicate(*this);
	}

	// Continue iterating regardless of visibility
	return true;
}


bool FSequencerDisplayNode::TraverseVisible_ParentFirst(const TFunctionRef<bool(FSequencerDisplayNode&)>& InPredicate, bool bIncludeThisNode)
{
	if (bIncludeThisNode && !IsHidden() && !InPredicate(*this))
	{
		return false;
	}

	// If the item is not expanded, its children ain't visible
	if (IsExpanded())
	{
		for (auto& Child : GetChildNodes())
		{
			if (!Child->IsHidden() && !Child->TraverseVisible_ParentFirst(InPredicate, true))
			{
				return false;
			}
		}
	}

	return true;
}

bool FSequencerDisplayNode::IsDimmed() const
{
	auto FindInActiveSection = [](FSequencerDisplayNode& InNode, bool EmptyNotActive = true)
	{
		if (InNode.GetType() == ESequencerNode::KeyArea)
		{
			const FSequencerSectionKeyAreaNode& KeyAreaNode = static_cast<FSequencerSectionKeyAreaNode&>(InNode);
			auto KeyAreaNodes = KeyAreaNode.GetAllKeyAreas();
			if (KeyAreaNodes.Num() > 0)
			{
				for (const TSharedRef<IKeyArea>& KeyArea : KeyAreaNodes)
				{
					const UMovieSceneSection* Section = KeyArea->GetOwningSection();
					if (Section && Section->IsActive())
					{
						// Stop traversing
						return false;
					}
				}
			}
			else
			{
				return EmptyNotActive;
			}
		}
		else if (InNode.GetType() == ESequencerNode::Track)
		{
			TArray<TSharedRef<FSequencerSectionKeyAreaNode>> KeyAreaNodes;
			const FSequencerTrackNode& TrackNode = static_cast<FSequencerTrackNode&>(InNode);
			TrackNode.GetChildKeyAreaNodesRecursively(KeyAreaNodes);
			if (KeyAreaNodes.Num() > 0)
			{
				for (TSharedRef<FSequencerSectionKeyAreaNode> KeyAreaNode : KeyAreaNodes)
				{
					for (TSharedPtr<IKeyArea> KeyArea : KeyAreaNode->GetAllKeyAreas())
					{
						const UMovieSceneSection* Section = KeyArea->GetOwningSection();
						if (Section && Section->IsActive())
						{
							// Stop traversing
							return false;
						}
					}
				}
			}
			else
			{
				if (TrackNode.GetSections().Num() > 0)
				{
					for (auto Section : TrackNode.GetSections())
					{
						if (Section->GetSectionObject() && Section->GetSectionObject()->IsActive())
						{
							// Stop traversing
							return false;
						}
					}
				}
				else
				{
					return EmptyNotActive;
				}
			}
		}
		// Continue traversing
		return true;
	};

	if (GetSequencer().IsReadOnly())
	{
		return true;
	}

	FSequencerDisplayNode *This = const_cast<FSequencerDisplayNode*>(this);
	//if empty with no key areas or sections then it's active, otherwise
	//find first child with active section, then it's active, else inactive.
	bool bDimLabel = ChildNodes.Num() > 0 ? This->Traverse_ParentFirst(FindInActiveSection) :
		((this->GetType() == ESequencerNode::Track || this->GetType() == ESequencerNode::KeyArea) && FindInActiveSection(*(This), false))
		|| false;

	if (!bDimLabel)
	{
		// If the node is a track node, we can use the cached value in UMovieSceneTrack
		if (GetType() == ESequencerNode::Track)
		{
			UMovieSceneTrack* Track = static_cast<const FSequencerTrackNode*>(this)->GetTrack();
			if (Track && Track->IsEvalDisabled())
			{
				bDimLabel = true;
			}
		}
		else
		{
			if (ParentTree.IsNodeMute(this) || (ParentTree.HasSoloNodes() && !ParentTree.IsNodeSolo(this)))
			{
				bDimLabel = true;
			}
		}
	}

	return bDimLabel;
}

FLinearColor FSequencerDisplayNode::GetDisplayNameColor() const
{
	return IsDimmed() ? FLinearColor(0.6f, 0.6f, 0.6f, 0.6f) : FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
}

FText FSequencerDisplayNode::GetDisplayNameToolTipText() const
{
	return FText();
}

bool FSequencerDisplayNode::ValidateDisplayName(const FText& NewDisplayName, FText& OutErrorMessage) const
{
	if (NewDisplayName.IsEmpty())
	{
		OutErrorMessage = NSLOCTEXT("Sequencer", "RenameFailed_LeftBlank", "Labels cannot be left blank");
		return false;
	}
	return true;
}

TSharedRef<SWidget> FSequencerDisplayNode::GenerateContainerWidgetForOutliner(const TSharedRef<SSequencerTreeViewRow>& InRow)
{
	auto NewWidget = SNew(SAnimationOutlinerTreeNode, SharedThis(this), InRow)
	.IconBrush(this, &FSequencerDisplayNode::GetIconBrush)
	.IconColor(this, &FSequencerDisplayNode::GetIconColor)
	.IconOverlayBrush(this, &FSequencerDisplayNode::GetIconOverlayBrush)
	.IconToolTipText(this, &FSequencerDisplayNode::GetIconToolTipText)
	.CustomContent()
	[
		GetCustomOutlinerContent()
	];

	return NewWidget;
}

TSharedRef<SWidget> FSequencerDisplayNode::GetCustomOutlinerContent()
{
	return SNew(SSpacer);
}

const FSlateBrush* FSequencerDisplayNode::GetIconBrush() const
{
	return nullptr;
}

const FSlateBrush* FSequencerDisplayNode::GetIconOverlayBrush() const
{
	return nullptr;
}

FSlateColor FSequencerDisplayNode::GetIconColor() const
{
	return GetDisplayNameColor();
}

FText FSequencerDisplayNode::GetIconToolTipText() const
{
	return FText();
}

TSharedRef<SWidget> FSequencerDisplayNode::GenerateWidgetForSectionArea(const TAttribute< TRange<double> >& ViewRange)
{
	if (GetType() == ESequencerNode::Track && static_cast<FSequencerTrackNode&>(*this).GetSubTrackMode() != FSequencerTrackNode::ESubTrackMode::ParentTrack)
	{
		return SNew(SSequencerSectionAreaView, SharedThis(this))
			.ViewRange(ViewRange);
	}
	
	return SNew(SSequencerCombinedKeysTrack, SharedThis(this))
		.ViewRange(ViewRange)
		.IsEnabled(!GetSequencer().IsReadOnly())
		.Visibility_Lambda([this]()
		{
			if (GetType() == ESequencerNode::Track)
			{
				if (static_cast<FSequencerTrackNode&>(*this).GetSubTrackMode() == FSequencerTrackNode::ESubTrackMode::ParentTrack && IsExpanded())
				{
					return EVisibility::Hidden;
				}
			}
			return EVisibility::Visible;
		})
		.TickResolution(this, &FSequencerDisplayNode::GetTickResolution);
}

FFrameRate FSequencerDisplayNode::GetTickResolution() const
{
	return GetSequencer().GetFocusedTickResolution();
}

TSharedPtr<FSequencerDisplayNode> FSequencerDisplayNode::GetSectionAreaAuthority() const
{
	TSharedPtr<FSequencerDisplayNode> Authority = SharedThis(const_cast<FSequencerDisplayNode*>(this));

	while (Authority.IsValid())
	{
		if (Authority->GetType() == ESequencerNode::Object || Authority->GetType() == ESequencerNode::Track)
		{
			return Authority;
		}
		else
		{
			Authority = Authority->GetParent();
		}
	}

	return Authority;
}


FString FSequencerDisplayNode::GetPathName() const
{
	// First get our parent's path
	FString PathName;

	TSharedPtr<FSequencerDisplayNode> Parent = GetParent();
	if (Parent.IsValid())
	{
		ensure(Parent != SharedThis(this));
		PathName = Parent->GetPathName() + TEXT(".");
	}

	//then append our path
	PathName += GetNodeName().ToString();

	return PathName;
}


TSharedPtr<SWidget> FSequencerDisplayNode::OnSummonContextMenu()
{
	// @todo sequencer replace with UI Commands instead of faking it
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, GetSequencer().GetCommandBindings());

	// let track editors & object bindings populate the menu
	if (CanAddObjectBindingsMenu())
	{
		MenuBuilder.BeginSection("ObjectBindings");
		GetSequencer().BuildAddObjectBindingsMenu(MenuBuilder);
		MenuBuilder.EndSection();
	}

	if (CanAddTracksMenu())
	{
		MenuBuilder.BeginSection("AddTracks");
		GetSequencer().BuildAddTrackMenu(MenuBuilder);
		MenuBuilder.EndSection();
	}

	BuildContextMenu(MenuBuilder);

	return MenuBuilder.MakeWidget();
}


namespace
{
	void AddEvalOptionsPropertyMenuItem(FMenuBuilder& MenuBuilder, FCanExecuteAction InCanExecute, const TArray<UMovieSceneTrack*>& AllTracks, const FBoolProperty* Property, TFunction<bool(UMovieSceneTrack*)> Validator = nullptr)
	{
		bool bIsChecked = AllTracks.ContainsByPredicate(
			[=](UMovieSceneTrack* InTrack)
			{
				return (!Validator || Validator(InTrack)) && Property->GetPropertyValue(Property->ContainerPtrToValuePtr<void>(&InTrack->EvalOptions));
			});

		MenuBuilder.AddMenuEntry(
			Property->GetDisplayNameText(),
			Property->GetToolTipText(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([AllTracks, Property, Validator, bIsChecked]{
					FScopedTransaction Transaction(FText::Format(NSLOCTEXT("Sequencer", "TrackNodeSetRoundEvaluation", "Set '{0}'"), Property->GetDisplayNameText()));
					for (UMovieSceneTrack* Track : AllTracks)
					{
						if (Validator && !Validator(Track))
						{
							continue;
						}
						void* PropertyContainer = Property->ContainerPtrToValuePtr<void>(&Track->EvalOptions);
						Track->Modify();
						Property->SetPropertyValue(PropertyContainer, !bIsChecked);
					}
				}),
				InCanExecute,
				FIsActionChecked::CreateLambda([=]{ return bIsChecked; })
			),
			NAME_None,
			EUserInterfaceActionType::Check
		);
	}

	void AddDisplayOptionsPropertyMenuItem(FMenuBuilder& MenuBuilder, FCanExecuteAction InCanExecute, const TArray<UMovieSceneTrack*>& AllTracks, const FBoolProperty* Property, TFunction<bool(UMovieSceneTrack*)> Validator = nullptr)
	{
		bool bIsChecked = AllTracks.ContainsByPredicate(
			[=](UMovieSceneTrack* InTrack)
		{
			return (!Validator || Validator(InTrack)) && Property->GetPropertyValue(Property->ContainerPtrToValuePtr<void>(&InTrack->DisplayOptions));
		});

		MenuBuilder.AddMenuEntry(
			Property->GetDisplayNameText(),
			Property->GetToolTipText(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([AllTracks, Property, Validator, bIsChecked] {
			FScopedTransaction Transaction(FText::Format(NSLOCTEXT("Sequencer", "TrackNodeSetDisplayOption", "Set '{0}'"), Property->GetDisplayNameText()));
			for (UMovieSceneTrack* Track : AllTracks)
			{
				if (Validator && !Validator(Track))
				{
					continue;
				}
				void* PropertyContainer = Property->ContainerPtrToValuePtr<void>(&Track->DisplayOptions);
				Track->Modify();
				Property->SetPropertyValue(PropertyContainer, !bIsChecked);
			}
		}),
				InCanExecute,
			FIsActionChecked::CreateLambda([=] { return bIsChecked; })
			),
			NAME_None,
			EUserInterfaceActionType::Check
		);
	}
}


void FSequencerDisplayNode::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	TSharedRef<FSequencerDisplayNode> ThisNode = SharedThis(this);
	FSequencerDisplayNode* BaseNode = GetBaseNode();

	ESequencerNode::Type BaseNodeType = BaseNode->GetType();

	bool bCanSolo = (BaseNodeType == ESequencerNode::Track || BaseNodeType == ESequencerNode::Object || BaseNodeType == ESequencerNode::Folder);
	bool bIsReadOnly = !GetSequencer().IsReadOnly();
	FCanExecuteAction CanExecute = FCanExecuteAction::CreateLambda([bIsReadOnly]{ return bIsReadOnly; });

	MenuBuilder.BeginSection("Edit", LOCTEXT("EditContextMenuSectionName", "Edit"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ToggleNodeLock", "Locked"),
			LOCTEXT("ToggleNodeLockTooltip", "Lock or unlock this node or selected tracks"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(&GetSequencer(), &FSequencer::ToggleNodeLocked),
				CanExecute,
				FIsActionChecked::CreateSP(&GetSequencer(), &FSequencer::IsNodeLocked)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		// Only support pinning root nodes
		if (BaseNode->IsRootNode())
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ToggleNodePin", "Pinned"),
				LOCTEXT("ToggleNodePinTooltip", "Pin or unpin this node or selected tracks"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FSequencerDisplayNode::TogglePinned),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &FSequencerDisplayNode::IsPinned)
				),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
		}

		if (bCanSolo)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ToggleNodeSolo", "Solo"),
				LOCTEXT("ToggleNodeSoloTooltip", "Solo or unsolo this node or selected tracks"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateRaw(&ParentTree, &FSequencerNodeTree::ToggleSelectedNodesSolo),
					CanExecute,
					FIsActionChecked::CreateSP(&ParentTree, &FSequencerNodeTree::IsSelectedNodesSolo)
				),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("ToggleNodeMute", "Mute"),
				LOCTEXT("ToggleNodeMuteTooltip", "Mute or unmute this node or selected tracks"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateRaw(&ParentTree, &FSequencerNodeTree::ToggleSelectedNodesMute),
					CanExecute,
					FIsActionChecked::CreateSP(&ParentTree, &FSequencerNodeTree::IsSelectedNodesMute)
				),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
		}

		// Add cut, copy and paste functions to the tracks
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut);

		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
		
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
		
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
		
		MenuBuilder.AddMenuEntry(
			LOCTEXT("DeleteNode", "Delete"),
			LOCTEXT("DeleteNodeTooltip", "Delete this or selected tracks"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.Delete"),
			FUIAction(FExecuteAction::CreateSP(&GetSequencer(), &FSequencer::DeleteNode, ThisNode, false), CanExecute)
		);

		if (ThisNode->GetType() == ESequencerNode::Object)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("DeleteNodeAndKeepState", "Delete and Keep State"),
				LOCTEXT("DeleteNodeAndKeepStateTooltip", "Delete this object's tracks and keep its current animated state"),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.Delete"),
				FUIAction(FExecuteAction::CreateSP(&GetSequencer(), &FSequencer::DeleteNode, ThisNode, true), CanExecute)
			);
		}

		MenuBuilder.AddMenuEntry(
			LOCTEXT("RenameNode", "Rename"),
			LOCTEXT("RenameNodeTooltip", "Rename this track"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetActions.Rename"),
			FUIAction(
				FExecuteAction::CreateSP(this, &FSequencerDisplayNode::HandleContextMenuRenameNodeExecute),
				FCanExecuteAction::CreateSP(this, &FSequencerDisplayNode::HandleContextMenuRenameNodeCanExecute)
			)
		);
	}
	MenuBuilder.EndSection();

	TArray<UMovieSceneTrack*> AllTracks;
	TArray<TSharedRef<FSequencerDisplayNode> > DragableNodes;
	for (TSharedRef<FSequencerDisplayNode> Node : GetSequencer().GetSelection().GetSelectedOutlinerNodes())
	{
		if (Node->GetType() == ESequencerNode::Track)
		{
			UMovieSceneTrack* Track = static_cast<FSequencerTrackNode&>(Node.Get()).GetTrack();
			if (Track)
			{
				AllTracks.Add(Track);
			}
		}

		if (Node->CanDrag())
		{
			DragableNodes.Add(Node);
		}
	}

	MenuBuilder.BeginSection("Organize", LOCTEXT("OrganizeContextMenuSectionName", "Organize"));
	{
		if (DragableNodes.Num())
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("MoveTracksToNewFolder", "Move to New Folder"),
				LOCTEXT("MoveTracksToNewFolderTooltip", "Move the selected tracks to a new folder."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "ContentBrowser.AssetTreeFolderOpen"),
				FUIAction(FExecuteAction::CreateSP(&GetSequencer(), &FSequencer::MoveSelectedNodesToNewFolder)));
		}
	}
	MenuBuilder.EndSection();

	if (AllTracks.Num())
	{
		MenuBuilder.BeginSection("GeneralTrackOptions", NSLOCTEXT("Sequencer", "TrackNodeGeneralOptions", "Track Options"));
		{
			UStruct* EvalOptionsStruct = FMovieSceneTrackEvalOptions::StaticStruct();

			const FBoolProperty* NearestSectionProperty = CastField<FBoolProperty>(EvalOptionsStruct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMovieSceneTrackEvalOptions, bEvalNearestSection)));
			auto CanEvaluateNearest = [](UMovieSceneTrack* InTrack) { return InTrack->EvalOptions.bCanEvaluateNearestSection != 0; };
			if (NearestSectionProperty && AllTracks.ContainsByPredicate(CanEvaluateNearest))
			{
				TFunction<bool(UMovieSceneTrack*)> Validator = CanEvaluateNearest;
				AddEvalOptionsPropertyMenuItem(MenuBuilder, CanExecute, AllTracks, NearestSectionProperty, Validator);
			}

			const FBoolProperty* PrerollProperty = CastField<FBoolProperty>(EvalOptionsStruct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMovieSceneTrackEvalOptions, bEvaluateInPreroll)));
			if (PrerollProperty)
			{
				AddEvalOptionsPropertyMenuItem(MenuBuilder, CanExecute, AllTracks, PrerollProperty);
			}

			const FBoolProperty* PostrollProperty = CastField<FBoolProperty>(EvalOptionsStruct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMovieSceneTrackEvalOptions, bEvaluateInPostroll)));
			if (PostrollProperty)
			{
				AddEvalOptionsPropertyMenuItem(MenuBuilder, CanExecute, AllTracks, PostrollProperty);
			}
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("TrackDisplayOptions", NSLOCTEXT("Sequencer", "TrackNodeDisplayOptions", "Display Options"));
		{
			UStruct* DisplayOptionsStruct = FMovieSceneTrackDisplayOptions::StaticStruct();

			const FBoolProperty* ShowVerticalFramesProperty = CastField<FBoolProperty>(DisplayOptionsStruct->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMovieSceneTrackDisplayOptions, bShowVerticalFrames)));
			if (ShowVerticalFramesProperty)
			{
				AddDisplayOptionsPropertyMenuItem(MenuBuilder, CanExecute, AllTracks, ShowVerticalFramesProperty);
			}
		}
		MenuBuilder.EndSection();
	}
}


void FSequencerDisplayNode::GetChildKeyAreaNodesRecursively(TArray< TSharedRef<FSequencerSectionKeyAreaNode> >& OutNodes) const
{
	for (const TSharedRef<FSequencerDisplayNode>& Node : ChildNodes)
	{
		if (Node->GetType() == ESequencerNode::KeyArea)
		{
			OutNodes.Add(StaticCastSharedRef<FSequencerSectionKeyAreaNode>(Node));
		}

		Node->GetChildKeyAreaNodesRecursively(OutNodes);
	}
}


void FSequencerDisplayNode::SetExpansionState(bool bInExpanded)
{
	bExpanded = bInExpanded;

	// Expansion state has changed, save it to the movie scene now
	ParentTree.SaveExpansionState(*this, bExpanded);
}


bool FSequencerDisplayNode::IsExpanded() const
{
	return bExpanded;
}

FSequencerDisplayNode* FSequencerDisplayNode::GetBaseNode() const
{
	ESequencerNode::Type Type = GetType();

	if (IsRootNode() || Type == ESequencerNode::Folder || Type == ESequencerNode::Object
		|| (Type == ESequencerNode::Track && static_cast<const FSequencerTrackNode*>(this)->GetSubTrackMode() != FSequencerTrackNode::ESubTrackMode::SubTrack))
	{
		return (FSequencerDisplayNode*)this;
	}

	return GetParentOrRoot()->GetBaseNode();
}

void FSequencerDisplayNode::UpdateCachedPinnedState(bool bParentIsPinned)
{
	bInPinnedBranch = bPinned || bParentIsPinned;

	for (TSharedPtr<FSequencerDisplayNode> Child : ChildNodes)
	{
		Child->UpdateCachedPinnedState(bInPinnedBranch);
	}
}

bool FSequencerDisplayNode::IsPinned() const
{
	return bInPinnedBranch;
}

void FSequencerDisplayNode::TogglePinned()
{
	FSequencerDisplayNode* BaseNode = GetBaseNode();
	bool bShouldPin = !BaseNode->bPinned;
	ParentTree.UnpinAllNodes();

	BaseNode->bPinned = bShouldPin;
	ParentTree.SavePinnedState(*this, bShouldPin);
	
	ParentTree.GetSequencer().RefreshTree();
}

void FSequencerDisplayNode::Unpin()
{
	FSequencerDisplayNode* BaseNode = GetBaseNode();
	if (BaseNode->bPinned)
	{
		BaseNode->bPinned = false;
		ParentTree.SavePinnedState(*this, false);
	
		ParentTree.GetSequencer().RefreshTree();
	}
}


bool FSequencerDisplayNode::IsHidden() const
{
	return ParentTree.HasActiveFilter() && !ParentTree.IsNodeFiltered(AsShared());
}


bool FSequencerDisplayNode::IsVisible() const
{
	return !ParentTree.HasActiveFilter() || ParentTree.IsNodeFiltered(AsShared());
}


bool FSequencerDisplayNode::IsHovered() const
{
	return ParentTree.GetHoveredNode().Get() == this;
}


void FSequencerDisplayNode::HandleContextMenuRenameNodeExecute()
{
	RenameRequestedEvent.Broadcast();
}


bool FSequencerDisplayNode::HandleContextMenuRenameNodeCanExecute() const
{
	return CanRenameNode();
}


TSharedPtr<SWidget> FSequencerDisplayNode::GenerateCurveEditorTreeWidget(const FName& InColumnName, TWeakPtr<FCurveEditor> InCurveEditor, FCurveEditorTreeItemID InTreeItemID, const TSharedRef<ITableRow>& TableRow)
{
	if (InColumnName == ColumnNames.Label)
	{
		return SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SOverlay)

				+ SOverlay::Slot()
				[
					SNew(SImage)
					.Image(this, &FSequencerDisplayNode::GetIconBrush)
					.ColorAndOpacity(this, &FSequencerDisplayNode::GetIconColor)
				]

				+ SOverlay::Slot()
				.VAlign(VAlign_Top)
				.HAlign(HAlign_Right)
				[
					SNew(SImage)
					.Image(this, &FSequencerDisplayNode::GetIconOverlayBrush)
				]

				+ SOverlay::Slot()
				[
					SNew(SSpacer)
					.Visibility(EVisibility::Visible)
					.ToolTipText(this, &FSequencerDisplayNode::GetIconToolTipText)
				]
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 4.f, 0.f, 4.f))
			[
				SNew(STextBlock)
				.Text(this, &FSequencerDisplayNode::GetDisplayName)
				.HighlightText_Static(SequencerNodeConstants::GetCurveEditorHighlightText, InCurveEditor)
				.ToolTipText(this, &FSequencerDisplayNode::GetDisplayNameToolTipText)
			];
	}
	else if (InColumnName == ColumnNames.PinHeader)
	{
		return SNew(SCurveEditorTreePin, InCurveEditor, InTreeItemID, TableRow);
	}

	return nullptr;
}

void FSequencerDisplayNode::CreateCurveModels(TArray<TUniquePtr<FCurveModel>>& OutCurveModels)
{
}

bool FSequencerDisplayNode::PassesFilter(const FCurveEditorTreeFilter* InFilter) const
{
	if (InFilter->GetType() == ECurveEditorTreeFilterType::Text)
	{
		const FCurveEditorTreeTextFilter* Filter = static_cast<const FCurveEditorTreeTextFilter*>(InFilter);

		TSharedRef<const FSequencerDisplayNode> This = AsShared();
		for (const FCurveEditorTreeTextFilterTerm& Term : Filter->GetTerms())
		{
			if (SequencerNodeConstants::NodeMatchesTextFilterTerm(This, Term))
			{
				return true;
			}
		}

		return false;
	}
	else if (InFilter->GetType() == ISequencerModule::GetSequencerSelectionFilterType())
	{
		const FSequencerSelectionCurveFilter* Filter = static_cast<const FSequencerSelectionCurveFilter*>(InFilter);
		return Filter->Match(AsShared());
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
