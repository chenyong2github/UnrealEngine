// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "Templates/SubclassOf.h"
#include "Widgets/SWidget.h"
#include "ISequencer.h"
#include "MovieSceneTrack.h"
#include "ISequencerSection.h"
#include "ISequencerTrackEditor.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "KeyframeTrackEditor.h"
#include "Sections/MovieScene3DTransformSection.h"

struct FAssetData;
class FMenuBuilder;
class USkeleton;

/**
 * Tools for animation tracks
 */
class FControlRigParameterTrackEditor : public FKeyframeTrackEditor<UMovieSceneControlRigParameterTrack>
{
public:
	/**
	 * Constructor
	 *
	 * @param InSequencer The sequencer instance to be used by this tool
	 */
	FControlRigParameterTrackEditor(TSharedRef<ISequencer> InSequencer);

	/** Virtual destructor. */
	virtual ~FControlRigParameterTrackEditor();

	/**
	 * Creates an instance of this class.  Called by a sequencer
	 *
	 * @param OwningSequencer The sequencer instance to be used by this tool
	 * @return The new instance of this class
	 */
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> OwningSequencer);

public:

	// ISequencerTrackEditor interface
	virtual void OnRelease() override;
	virtual void BindCommands(TSharedRef<FUICommandList> SequencerCommandBindings) override;
	virtual void BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass) override;
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;

private:

	void AddControlRigSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings, UMovieSceneTrack* Track);

	/** Control Rig Picked */
	void AddControlRig(UClass* InClass, UObject* BoundObject, FGuid ObjectBinding, USkeleton* Skeleton);

	/** Delegate for Selection Changed Event */
	void OnSelectionChanged(TArray<UMovieSceneTrack*> InTracks);

	/** Delegate for MovieScene Changing so we can see if our track got deleted*/
	void OnSequencerDataChanged(EMovieSceneDataChangeType DataChangeType);

	/** Control Rig Delegates*/
	void HandleControlModified(IControlRigManipulatable* Subject, const FRigControl& Control);
	void HandleControlSelected(IControlRigManipulatable* Subject, const FRigControl& Control, bool bSelected);

private:
	//MZ TODO Store map of control rig to parameter values to see if they changed?

	/** Command Bindings added by the Transform Track Editor to Sequencer and curve editor. */
	TSharedPtr<FUICommandList> CommandBindings;
public:

	void AddControlKeys(UObject* InObject, IControlRigManipulatable* Manip, FName PropertyName, FName ParameterName, EMovieSceneTransformChannel ChannelsToKey, ESequencerKeyMode KeyMode);
	void GetControlRigKeys(IControlRigManipulatable* Manip, FName ParameterName, EMovieSceneTransformChannel ChannelsToKey, FGeneratedTrackKeys& OutGeneratedKeys);
	FKeyPropertyResult AddKeysToControlRig(
		UObject* Object, IControlRigManipulatable* Manip, FFrameNumber KeyTime, FGeneratedTrackKeys& GeneratedKeys,
		ESequencerKeyMode KeyMode, TSubclassOf<UMovieSceneTrack> TrackClass, FName PropertyName);
	FKeyPropertyResult AddKeysToControlRigHandle(UObject *Object, IControlRigManipulatable* Manip,
		FGuid ObjectHandle, FFrameNumber KeyTime, FGeneratedTrackKeys& GeneratedKeys,
		ESequencerKeyMode KeyMode, TSubclassOf<UMovieSceneTrack> TrackClass, FName PropertyName);
	/**
	 * Modify the passed in Generated Keys by the current tracks values and weight at the passed in time.

	 * @param Object The handle to the object modify
	 * @param Track The track we are modifying
	 * @param SectionToKey The Sections Channels we will be modifiying
	 * @param Time The Time at which to evaluate
	 * @param InOutGeneratedTrackKeys The Keys we need to modify. We change these values.
	 * @param Weight The weight we need to modify the values by.
	 */
	bool ModifyOurGeneratedKeysByCurrentAndWeight(UObject* Object, IControlRigManipulatable* Manip, UMovieSceneTrack *Track, UMovieSceneSection* SectionToKey, FFrameNumber Time, FGeneratedTrackKeys& InOutGeneratedTotalKeys, float Weight) const;

	/*
	bool ShouldFilterAsset(const FAssetData& AssetData);

	void OnControlRigAssetSelected(const FAssetData& AssetData, TArray<FGuid> ObjectBindings, USkeleton* Skeleton);

	void OnControlRigAssetEnterPressed(const TArray<FAssetData>& AssetData, TArray<FGuid> ObjectBindings,  USkeleton* Skeleton);
	*/
private:
	bool CanAddTransformKeysForSelectedObjects();
	void OnAddTransformKeysForSelectedObjects(EMovieSceneTransformChannel Channel);
	FDelegateHandle SelectionChangedHandle;
	FDelegateHandle SequencerChangedHandle;

private:

	/** Guard to stop infinite loops when handling control selections*/
	bool bIsDoingASelection;

};


/** Class for control rig sections */
class FControlRigParameterSection : public FSequencerSection
{
public:

	/**
	* Creates a new control rig property section.
	*
	* @param InSection The section object which is being displayed and edited.
	* @param InSequencer The sequencer which is controlling this parameter section.
	*/
	FControlRigParameterSection(UMovieSceneSection& InSection, TWeakPtr<ISequencer> InSequencer)
		: FSequencerSection(InSection), WeakSequencer(InSequencer)
	{
	}

public:

	virtual void BuildSectionContextMenu(FMenuBuilder& MenuBuilder, const FGuid& InObjectBinding) override;

	//~ ISequencerSection interface
	virtual bool RequestDeleteCategory(const TArray<FName>& CategoryNamePath) override;
	virtual bool RequestDeleteKeyArea(const TArray<FName>& KeyAreaNamePath) override;

protected:

	/** The sequencer which is controlling this section. */
	TWeakPtr<ISequencer> WeakSequencer;

};