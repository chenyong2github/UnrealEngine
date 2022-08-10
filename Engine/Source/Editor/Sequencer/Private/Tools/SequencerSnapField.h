// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Tools/SequencerEntityVisitor.h"
#include "MVVM/Extensions/ISnappableExtension.h"

class IKeyArea;
class FSequencer;

/** A snapping field that provides efficient snapping calculations on a range of values */
class FSequencerSnapField
{
public:

	/** A snap result denoting the time that was snapped, and the resulting snapped time */
	struct FSnapResult
	{
		/** The time before it was snapped */
		FFrameNumber Original;
		/** The time after it was snapped */
		FFrameNumber Snapped;
	};

	FSequencerSnapField(){}

	/** Construction from a sequencer and a snap canidate implementation. Optionally provide an entity mask to completely ignore some entity types */
	FSequencerSnapField(const FSequencer& InSequencer, UE::Sequencer::ISnapCandidate& Candidate, uint32 EntityMask = ESequencerEntity::Everything);

	/** Move construction / assignment */
	FSequencerSnapField(FSequencerSnapField&& In) : SortedSnaps(MoveTemp(In.SortedSnaps)) {}
	FSequencerSnapField& operator=(FSequencerSnapField&& In) { SortedSnaps = MoveTemp(In.SortedSnaps); return *this; }

	void Initialize(const FSequencer& InSequencer, UE::Sequencer::ISnapCandidate& Candidate, uint32 EntityMask = ESequencerEntity::Everything);

	void AddExplicitSnap(UE::Sequencer::FSnapPoint InSnap);

	void Finalize();

	/** Snap the specified time to this field with the given threshold */
	TOptional<FFrameNumber> Snap(FFrameNumber InTime, int32 Threshold) const;

	/** Snap the specified times to this field with the given threshold. Will return the closest snap value of the entire intersection. */
	TOptional<FSnapResult> Snap(const TArray<FFrameNumber>& InTimes, int32 Threshold) const;

private:
	/** Array of snap points, approximately grouped, and sorted in ascending order by time */
	TArray<UE::Sequencer::FSnapPoint> SortedSnaps;
};
