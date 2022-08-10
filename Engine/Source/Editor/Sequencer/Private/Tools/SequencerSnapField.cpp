// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/SequencerSnapField.h"
#include "MovieScene.h"
#include "SSequencer.h"
#include "MovieSceneTimeHelpers.h"
#include "MovieSceneSequence.h"
#include "ISequencerSection.h"
#include "MVVM/ViewModels/ViewModel.h"
#include "MVVM/Views/SOutlinerView.h"
#include "MVVM/Extensions/ISnappableExtension.h"

struct FSnapGridVisitor : ISequencerEntityVisitor, UE::Sequencer::ISnapField
{
	FSnapGridVisitor(UE::Sequencer::ISnapCandidate& InCandidate, uint32 EntityMask)
		: ISequencerEntityVisitor(EntityMask)
		, Candidate(InCandidate)
	{}

	virtual void VisitKey(FKeyHandle KeyHandle, FFrameNumber KeyTime, const UE::Sequencer::TViewModelPtr<UE::Sequencer::FChannelModel>& Channel, UMovieSceneSection* Section) const
	{
		using namespace UE::Sequencer;

		if (Candidate.IsKeyApplicable(KeyHandle, Channel))
		{
			Snaps.Add(FSnapPoint{ FSnapPoint::Key, KeyTime });
		}
	}
	virtual void VisitDataModel(UE::Sequencer::FViewModel* DataModel) const
	{
		using namespace UE::Sequencer;

		if (ISnappableExtension* Snappable = DataModel->CastThis<ISnappableExtension>())
		{
			Snappable->AddToSnapField(Candidate, *const_cast<FSnapGridVisitor*>(this));
		}
	}

	virtual void AddSnapPoint(const UE::Sequencer::FSnapPoint& SnapPoint) override
	{
		Snaps.Add(SnapPoint);
	}

	UE::Sequencer::ISnapCandidate& Candidate;
	mutable TArray<UE::Sequencer::FSnapPoint> Snaps;
};

FSequencerSnapField::FSequencerSnapField(const FSequencer& InSequencer, UE::Sequencer::ISnapCandidate& Candidate, uint32 EntityMask)
{
	Initialize(InSequencer, Candidate, EntityMask);
	Finalize();
}

void FSequencerSnapField::AddExplicitSnap(UE::Sequencer::FSnapPoint InSnap)
{
	SortedSnaps.Add(InSnap);
}

void FSequencerSnapField::Initialize(const FSequencer& InSequencer, UE::Sequencer::ISnapCandidate& Candidate, uint32 EntityMask)
{
	using namespace UE::Sequencer;

	TSharedPtr<SOutlinerView> TreeView = StaticCastSharedRef<SSequencer>(InSequencer.GetSequencerWidget())->GetTreeView();

	TArray<TViewModelPtr<IOutlinerExtension>> VisibleItems;
	TreeView->GetVisibleItems(VisibleItems);

	TRange<double> ViewRange = InSequencer.GetViewRange();
	FSequencerEntityWalker Walker(
		FSequencerEntityRange(ViewRange, InSequencer.GetFocusedTickResolution()),
		FVector2D(SequencerSectionConstants::KeySize));

	// Traverse the visible space, collecting snapping times as we go
	FSnapGridVisitor Visitor(Candidate, EntityMask);
	for (const TViewModelPtr<IOutlinerExtension>& Item : VisibleItems)
	{
		Walker.Traverse(Visitor, Item);
	}

	// Add the playback range start/end bounds as potential snap candidates
	TRange<FFrameNumber> PlaybackRange = InSequencer.GetFocusedMovieSceneSequence()->GetMovieScene()->GetPlaybackRange();
	if(UE::MovieScene::DiscreteSize(PlaybackRange) > 0)
	{
		Visitor.Snaps.Add(FSnapPoint{ FSnapPoint::PlaybackRange, UE::MovieScene::DiscreteInclusiveLower(PlaybackRange)});
		Visitor.Snaps.Add(FSnapPoint{ FSnapPoint::PlaybackRange, UE::MovieScene::DiscreteExclusiveUpper(PlaybackRange)});
	}

	// Add the current time as a potential snap candidate
	Visitor.Snaps.Add(FSnapPoint{ FSnapPoint::CurrentTime, InSequencer.GetLocalTime().Time.FrameNumber });

	// Add the selection range bounds as a potential snap candidate
	TRange<FFrameNumber> SelectionRange = InSequencer.GetFocusedMovieSceneSequence()->GetMovieScene()->GetSelectionRange();
	if (UE::MovieScene::DiscreteSize(SelectionRange) > 0)
	{
		Visitor.Snaps.Add(FSnapPoint{ FSnapPoint::InOutRange, UE::MovieScene::DiscreteInclusiveLower(SelectionRange)});
		Visitor.Snaps.Add(FSnapPoint{ FSnapPoint::InOutRange, UE::MovieScene::DiscreteExclusiveUpper(SelectionRange) - 1});
	}

	// Add in the marked frames
	for (const FMovieSceneMarkedFrame& MarkedFrame : InSequencer.GetFocusedMovieSceneSequence()->GetMovieScene()->GetMarkedFrames())
	{
		Visitor.Snaps.Add( FSnapPoint{ FSnapPoint::Mark, MarkedFrame.FrameNumber } );
	}

	// Add in the global marked frames
	for (const FMovieSceneMarkedFrame& MarkedFrame : InSequencer.GetGlobalMarkedFrames())
	{
		Visitor.Snaps.Add(FSnapPoint{ FSnapPoint::Mark, MarkedFrame.FrameNumber });
	}


	if (SortedSnaps.Num() == 0)
	{
		SortedSnaps = MoveTemp(Visitor.Snaps);
	}
	else
	{
		SortedSnaps.Append(Visitor.Snaps);
	}
}

void FSequencerSnapField::Finalize()
{
	using namespace UE::Sequencer;

	// Sort
	SortedSnaps.Sort([](const FSnapPoint& A, const FSnapPoint& B){
		return A.Time < B.Time;
	});

	// Remove duplicates
	for (int32 Index = 0; Index < SortedSnaps.Num(); ++Index)
	{
		const FFrameNumber CurrentTime = SortedSnaps[Index].Time;

		int32 NumToMerge = 0;
		for (int32 DuplIndex = Index + 1; DuplIndex < SortedSnaps.Num(); ++DuplIndex)
		{
			if (CurrentTime != SortedSnaps[DuplIndex].Time)
			{
				break;
			}
			++NumToMerge;
		}

		if (NumToMerge)
		{
			SortedSnaps.RemoveAt(Index + 1, NumToMerge, false);
		}
	}
}

TOptional<FFrameNumber> FSequencerSnapField::Snap(FFrameNumber InTime, int32 Threshold) const
{
	int32 Min = 0;
	int32 Max = SortedSnaps.Num();

	// Binary search, then linearly search a range
	for ( ; Min != Max ; )
	{
		int32 SearchIndex = Min + (Max - Min) / 2;

		FFrameNumber ProspectiveSnapPos = SortedSnaps[SearchIndex].Time;
		if (ProspectiveSnapPos > InTime + Threshold)
		{
			Max = SearchIndex;
		}
		else if (ProspectiveSnapPos < InTime - Threshold)
		{
			Min = SearchIndex + 1;
		}
		else
		{
			// Linearly search forwards and backwards to find the closest snap

			FFrameNumber SnapDelta = ProspectiveSnapPos - InTime;

			// Search forwards while we're in the threshold
			for (int32 FwdIndex = SearchIndex+1; FwdIndex < Max-1 && SortedSnaps[FwdIndex].Time < InTime + Threshold; ++FwdIndex)
			{
				FFrameNumber ThisSnapDelta = InTime - SortedSnaps[FwdIndex].Time;
				if (FMath::Abs(ThisSnapDelta) < FMath::Abs(SnapDelta))
				{
					SnapDelta = ThisSnapDelta;
					ProspectiveSnapPos = SortedSnaps[FwdIndex].Time;
				}
			}

			// Search backwards while we're in the threshold
			for (int32 BckIndex = SearchIndex-1; BckIndex >= Min && SortedSnaps[BckIndex].Time > InTime + Threshold; --BckIndex)
			{
				FFrameNumber ThisSnapDelta = InTime - SortedSnaps[BckIndex].Time;
				if (FMath::Abs(ThisSnapDelta) < FMath::Abs(SnapDelta))
				{
					SnapDelta = ThisSnapDelta;
					ProspectiveSnapPos = SortedSnaps[BckIndex].Time;
				}
			}

			return ProspectiveSnapPos;
		}
	}

	return TOptional<FFrameNumber>();
}

TOptional<FSequencerSnapField::FSnapResult> FSequencerSnapField::Snap(const TArray<FFrameNumber>& InTimes, int32 Threshold) const
{
	TOptional<FSnapResult> ProspectiveSnap;
	FFrameNumber SnapDelta(0);

	for (FFrameNumber Time : InTimes)
	{
		TOptional<FFrameNumber> ThisSnap = Snap(Time, Threshold);
		if (!ThisSnap.IsSet())
		{
			continue;
		}

		FFrameNumber ThisSnapDelta = ThisSnap.GetValue() - Time;
		if (!ProspectiveSnap.IsSet() || FMath::Abs(ThisSnapDelta) < FMath::Abs(SnapDelta))
		{
			ProspectiveSnap = FSnapResult{ Time, ThisSnap.GetValue() };
			SnapDelta = ThisSnapDelta;
		}
	}

	return ProspectiveSnap;
}
