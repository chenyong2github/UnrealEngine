// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneTrackEditor.h"
#include "SequencerTrailHierarchy.h"
#include "Animation/Skeleton.h"

#include "Containers/BitArray.h"


namespace UE
{
namespace MotionTrailEditor
{

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnBoneVisibilityChanged, class USkeleton*, const FName&);

class FMotionTrailTrackEditor : public FMovieSceneTrackEditor
{
public:

	FMotionTrailTrackEditor(TSharedRef<ISequencer> InSequencer)
		: FMovieSceneTrackEditor(InSequencer)
		, BoneVisibilities()
	{}

	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> InSequencer);
	
	// Start FMovieSceneTrackEditor interface
	virtual void OnInitialize() override;
	virtual void OnRelease() override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;
	virtual void BuildObjectBindingContextMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass) override;
	// end FMovieSceneTrackEditor

	const TBitArray<>& GetVisibilities(USkeleton* Skeleton) { return BoneVisibilities.FindOrAdd(Skeleton, TBitArray<>(false, Skeleton->GetReferenceSkeleton().GetNum())); }

private:

	void CreateBoneVisibilityMenu(FMenuBuilder& MenuBuilder, USkeleton* Skeleton, FSequencerTrailHierarchy* Hierarchy);

	// TODO: make map of FName to bool, index lookup is O(N)
	TMap<USkeleton*, TBitArray<>> BoneVisibilities;
	TMap<UObject*, EBindingVisibilityState> VisibilityStates;
};

} // namespace MovieScene
} // namespace UE
