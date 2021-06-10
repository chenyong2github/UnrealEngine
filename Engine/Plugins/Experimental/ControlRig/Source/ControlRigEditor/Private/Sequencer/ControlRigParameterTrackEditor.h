// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "AcquiredResources.h"
#include "MovieSceneToolHelpers.h"
#include "MovieSceneToolsModule.h"

struct FAssetData;
class FMenuBuilder;
class USkeleton;
class UMovieSceneControlRigParameterSection;
class UFKControlRig;

/**
 * Tools for animation tracks
 */
class FControlRigParameterTrackEditor : public FKeyframeTrackEditor<UMovieSceneControlRigParameterTrack>, public IMovieSceneToolsAnimationBakeHelper
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
	virtual void BuildObjectBindingContextMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass) override;
	virtual void BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass) override;
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;
	virtual bool HasTransformKeyBindings() const override { return true; }
	virtual bool CanAddTransformKeysForSelectedObjects() const override;
	virtual void OnAddTransformKeysForSelectedObjects(EMovieSceneTransformChannel Channel);
	virtual bool HasTransformKeyOverridePriority() const override;
	virtual void ObjectImplicitlyAdded(UObject* InObject)  override;
	virtual void BuildTrackContextMenu(FMenuBuilder& MenuBuilder, UMovieSceneTrack* InTrack) override;

	//IMovieSceneToolsAnimationBakeHelper
	virtual void PostEvaluation(UMovieScene* MovieScene, FFrameNumber Frame);

private:

	void HandleAddTrackSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings, UMovieSceneTrack* Track);
	void HandleAddControlRigSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings, UMovieSceneTrack* Track);

	void ToggleFilterAssetBySkeleton();
	bool IsToggleFilterAssetBySkeleton();

	void ToggleFilterAssetByAnimatableControls();
	bool IsToggleFilterAssetByAnimatableControls();
	void SelectSequencerNodeInSection(UMovieSceneControlRigParameterSection* ParamSection, const FName& ControlName, bool bSelected);

	/** Control Rig Picked */
	void AddControlRig(UClass* InClass, UObject* BoundActor, FGuid ObjectBinding);
	void AddControlRig(UClass* InClass, UObject* BoundActor, FGuid ObjectBinding, UControlRig* InExistingControlRig);
	void AddControlRigFromComponent(FGuid InGuid);
	void AddFKControlRig(TArray<FGuid> ObjectBindings);
	
	/** Delegate for Selection Changed Event */
	void OnSelectionChanged(TArray<UMovieSceneTrack*> InTracks);

	/** Delegate for MovieScene Changing so we can see if our track got deleted*/
	void OnSequencerDataChanged(EMovieSceneDataChangeType DataChangeType);

	/** Delegate for when track immediately changes, so we need to do an interaction edit for things like fk/ik*/
	void OnChannelChanged(const FMovieSceneChannelMetaData* MetaData, UMovieSceneSection* InSection);

	/** Delegate for Curve Selection Changed Event */
	void OnCurveDisplayChanged(FCurveModel* InCurveModel, bool bDisplayed);

	/** Delegate for difference focused movie scene sequence*/
	void OnActivateSequenceChanged(FMovieSceneSequenceIDRef ID);

	/** Actor Added Delegate*/
	void HandleActorAdded(AActor* Actor, FGuid TargetObjectGuid);
	/** Add Control Rig Tracks For Skelmesh Components*/
	void AddTrackForComponent(USceneComponent* Component);

	/** Control Rig Delegates*/
	void HandleControlModified(UControlRig* Subject, const FRigControl& Control, const FRigControlModifiedContext& Context);
	void HandleControlSelected(UControlRig* Subject, const FRigControl& Control, bool bSelected);
	void HandleOnInitialized(UControlRig* Subject, const EControlRigState InState, const FName& InEventName);
	/** Post Edit Delegates */
	void OnPropagateObjectChanges(UObject* InChangedObject);

	/** Handle Creattion for SkelMeshComp or Actor Owner, either may have a binding*/
	FMovieSceneTrackEditor::FFindOrCreateHandleResult FindOrCreateHandleToSceneCompOrOwner(USceneComponent* InComp);
	
	/** Import FBX*/
	void ImportFBX(UMovieSceneControlRigParameterTrack* InTrack, UMovieSceneControlRigParameterSection* InSection, 
		TArray<FFBXNodeAndChannels>* NodeAndChannels);

	/** Select Bones to Animate on FK Rig*/
	void SelectFKBonesToAnimate(UFKControlRig* FKControlRig, UMovieSceneControlRigParameterTrack* Track);

	/** Toggle FK Control Rig*/
	void ToggleFKControlRig(UMovieSceneControlRigParameterTrack* Track, UFKControlRig* FKControlRig);

	/** Convert to FK Control Rig*/
	void ConvertToFKControlRig(FGuid ObjectBinding, UObject* BoundObject, USkeletalMeshComponent* SkelMeshComp, USkeleton* Skeleton);

	/** Bake To Control Rig Sub Menu*/
	void BakeToControlRigSubMenu(FMenuBuilder& MenuBuilder, FGuid ObjectBinding, UObject* BoundObject, USkeletalMeshComponent* SkelMeshComp,USkeleton* Skeleton);
	
	/** Bake To Control Rig Sub Menu*/
	void BakeToControlRig(UClass* InClass, FGuid ObjectBinding,UObject* BoundObject, USkeletalMeshComponent* SkelMeshComp, USkeleton* Skeleton);

private:

	/** Command Bindings added by the Transform Track Editor to Sequencer and curve editor. */
	TSharedPtr<FUICommandList> CommandBindings;

	FAcquiredResources AcquiredResources;

public:

	void AddControlKeys(USceneComponent *InSceneComp, UControlRig* InControlRig, FName PropertyName, FName ParameterName, EMovieSceneTransformChannel ChannelsToKey, ESequencerKeyMode KeyMode, float InLocalTime);
	void GetControlRigKeys(UControlRig* InControlRig, FName ParameterName, EMovieSceneTransformChannel ChannelsToKey, UMovieSceneControlRigParameterSection* SectionToKey, FGeneratedTrackKeys& OutGeneratedKeys);
	FKeyPropertyResult AddKeysToControlRig(
		USceneComponent *InSceneComp, UControlRig* InControlRig, FFrameNumber KeyTime, FGeneratedTrackKeys& GeneratedKeys,
		ESequencerKeyMode KeyMode, TSubclassOf<UMovieSceneTrack> TrackClass, FName ControlRigName, FName RigControlName);
	FKeyPropertyResult AddKeysToControlRigHandle(USceneComponent *InSceneComp, UControlRig* InControlRig,
		FGuid ObjectHandle, FFrameNumber KeyTime, FGeneratedTrackKeys& GeneratedKeys,
		ESequencerKeyMode KeyMode, TSubclassOf<UMovieSceneTrack> TrackClass, FName ControlRigName, FName RigControlName);
	/**
	 * Modify the passed in Generated Keys by the current tracks values and weight at the passed in time.

	 * @param Object The handle to the object modify
	 * @param Track The track we are modifying
	 * @param SectionToKey The Sections Channels we will be modifiying
	 * @param Time The Time at which to evaluate
	 * @param InOutGeneratedTrackKeys The Keys we need to modify. We change these values.
	 * @param Weight The weight we need to modify the values by.
	 */
	bool ModifyOurGeneratedKeysByCurrentAndWeight(UObject* Object, UControlRig* InControlRig, FName RigControlName, UMovieSceneTrack *Track, UMovieSceneSection* SectionToKey, FFrameNumber Time, FGeneratedTrackKeys& InOutGeneratedTotalKeys, float Weight) const;


private:
	FDelegateHandle SelectionChangedHandle;
	FDelegateHandle SequencerChangedHandle;
	FDelegateHandle OnActivateSequenceChangedHandle;
	FDelegateHandle CurveChangedHandle;
	FDelegateHandle OnChannelChangedHandle;
	FDelegateHandle OnMovieSceneChannelChangedHandle;
	FDelegateHandle OnActorAddedToSequencerHandle;

	void BindControlRig(UControlRig* ControlRig);
	void UnbindControlRig(UControlRig* ControlRig);
	void UnbindAllControlRigs();
	TArray<TWeakObjectPtr<UControlRig>> BoundControlRigs;


	//used to sync curve editor selections/displays on next tick for performance reasons
	TSet<FName> DisplayedControls;
	TSet<FName> UnDisplayedControls;
	bool bCurveDisplayTickIsPending;

private:

	/** Guard to stop infinite loops when handling control selections*/
	bool bIsDoingSelection;

	/** Whether or not we should check Skeleton when filtering*/
	bool bFilterAssetBySkeleton;

	/** Whether or not we should check for Animatable Controls when filtering*/
	bool bFilterAssetByAnimatableControls;
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
	/** Add Sub Menu */
	void AddAnimationSubMenuForFK(FMenuBuilder& MenuBuilder, FGuid ObjectBinding, USkeleton* Skeleton, UMovieSceneControlRigParameterSection* Section);

	/** Animation sub menu filter function */
	bool ShouldFilterAssetForFK(const FAssetData& AssetData);

	/** Animation asset selected */
	void OnAnimationAssetSelectedForFK(const FAssetData& AssetData,FGuid ObjectBinding, UMovieSceneControlRigParameterSection* Section);

	/** Animation asset enter pressed */
	void OnAnimationAssetEnterPressedForFK(const TArray<FAssetData>& AssetData, FGuid ObjectBinding, UMovieSceneControlRigParameterSection* Section);


	/** The sequencer which is controlling this section. */
	TWeakPtr<ISequencer> WeakSequencer;

};