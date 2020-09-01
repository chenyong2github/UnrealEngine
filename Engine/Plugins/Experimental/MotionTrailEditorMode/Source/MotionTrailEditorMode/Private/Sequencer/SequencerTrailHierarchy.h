// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TrailHierarchy.h"

#include "ISequencer.h"
#include "Rigs/RigHierarchyDefines.h"

class ISequencer;
class USceneComponent;
class USkeletalMeshComponent;
class USkeleton;
class UMovieSceneSection;
class UMovieScene3DTransformTrack;
class UMovieSceneControlRigParameterTrack;
class UMovieSceneControlRigParameterSection;

struct FRigHierarchyContainer;

namespace UE
{
namespace MotionTrailEditor
{

class FSequencerTrailHierarchy : public FTrailHierarchy
{
public:
	FSequencerTrailHierarchy(TWeakObjectPtr<class UMotionTrailEditorMode> InWeakEditorMode, TWeakPtr<ISequencer> InWeakSequencer)
		: FTrailHierarchy(InWeakEditorMode)
		, WeakSequencer(InWeakSequencer)
		, ObjectsTracked()
		, ControlsTracked()
		, HierarchyRenderer(MakeUnique<FTrailHierarchyRenderer>(this))
		, OnActorAddedToSequencerHandle()
		, OnLevelActorAttachedHandle()
		, OnLevelActorDetachedHandle()
		, OnSelectionChangedHandle()
		, OnBoneVisibilityChangedHandle()
		, OnViewOptionsChangedHandle()
		, ControlRigDelegateHandles()
	{}

	// FTrailHierarchy interface
	virtual void Initialize() override;
	virtual void Destroy() override;
	virtual ITrailHierarchyRenderer* GetRenderer() const override { return HierarchyRenderer.Get(); }
	virtual double GetSecondsPerFrame() const override { return 1.0 / WeakSequencer.Pin()->GetFocusedDisplayRate().AsDecimal(); }
	virtual void RemoveTrail(const FGuid& Key) override;
	virtual void Update() override;

	const TMap<UObject*, FGuid>& GetObjectsTracked() const { return ObjectsTracked; }
	const TMap<USkeletalMeshComponent*, TMap<FName, FGuid>>& GetBonesTracked() const { return BonesTracked; }
	const TMap<USkeletalMeshComponent*, TMap<FName, FGuid>>& GetControlsTracked() const { return ControlsTracked; }

	void OnBoneVisibilityChanged(USkeleton* Skeelton, const FName& BoneName);

private:
	void UpdateSequencerBindings(const TArray<FGuid>& SequencerBindings, TFunctionRef<void(FTrail*)> OnUpdated);
	void UpdateObjectsTracked(); // TODO: will remove
	void UpdateViewRange();

	void ResolveComponentToRoot(USceneComponent* Component);
	void AddComponentToHierarchy(USceneComponent* CompToAdd, UMovieScene3DTransformTrack* TransformTrack);
	void AddSkeletonToHierarchy(USkeletalMeshComponent* CompToAdd);
	void ResolveRigElementToRootComponent(FRigHierarchyContainer* RigHierarchy, FRigElementKey InElementKey, USkeletalMeshComponent* Component);
	void AddControlsToHierarchy(USkeletalMeshComponent* CompToAdd, UMovieSceneControlRigParameterTrack* CRParamTrack);

	void RegisterControlRigDelegates(USkeletalMeshComponent* Component, UMovieSceneControlRigParameterSection* CRParamSection);

	TWeakPtr<ISequencer> WeakSequencer;
	TMap<UObject*, FGuid> ObjectsTracked;
	TMap<USkeletalMeshComponent*, TMap<FName, FGuid>> BonesTracked; // TODO: components can have multiple rigs so make this a map from sections to controls instead
	TMap<USkeletalMeshComponent*, TMap<FName, FGuid>> ControlsTracked;

	TUniquePtr<FTrailHierarchyRenderer> HierarchyRenderer;

	FDelegateHandle OnActorAddedToSequencerHandle;
	FDelegateHandle OnLevelActorAttachedHandle;
	FDelegateHandle OnLevelActorDetachedHandle;
	FDelegateHandle OnSelectionChangedHandle;
	FDelegateHandle OnBoneVisibilityChangedHandle;
	FDelegateHandle OnViewOptionsChangedHandle;

	struct FControlRigDelegateHandles
	{
		FDelegateHandle OnControlAddedHandle;
		FDelegateHandle OnControlRemovedHandle;
		FDelegateHandle OnControlReparentedHandle;
		FDelegateHandle OnControlRenamedHandle;
	};
	TMap<UMovieSceneSection*, FControlRigDelegateHandles> ControlRigDelegateHandles;
};

} // namespace MovieScene
} // namespace UE
