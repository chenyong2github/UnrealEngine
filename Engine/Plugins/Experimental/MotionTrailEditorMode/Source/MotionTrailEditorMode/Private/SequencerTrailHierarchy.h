// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TrailHierarchy.h"

#include "ISequencer.h"

class ISequencer;

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
		, OnSelectionChangedHandle()
		, OnGlobalTimeChangedHandle()
		, OnViewOptionsChangedHandle()
	{}

	// FTrailHierarchy interface
	virtual void Initialize() override;
	virtual void Destroy() override;
	virtual ITrailHierarchyRenderer* GetRenderer() const override { return HierarchyRenderer.Get(); }
	virtual double GetSecondsPerFrame() const override { return 1.0 / WeakSequencer.Pin()->GetFocusedDisplayRate().AsDecimal(); }

	virtual void RemoveTrail(const FGuid& Key);

	virtual void Update() override
	{
		UpdateViewRange();
		FTrailHierarchy::Update();
	}

private:
	void UpdateSequencerBindings(const TArray<FGuid>& SequencerBindings, TFunctionRef<void(FTrail*)> OnUpdated);
	void UpdateObjectsTracked();
	void UpdateViewRange();

	void ResolveComponentToRoot(class USceneComponent* Component);
	void AddComponentToHierarchy(class USceneComponent* CompToAdd, class UMovieScene3DTransformTrack* TransformTrack);

	TWeakPtr<ISequencer> WeakSequencer;
	TMap<UObject*, FGuid> ObjectsTracked;
	TMap<FName, FGuid> ControlsTracked;

	TUniquePtr<FTrailHierarchyRenderer> HierarchyRenderer;

	FDelegateHandle OnActorAddedToSequencerHandle;
	FDelegateHandle OnLevelActorAttachedHandle;
	FDelegateHandle OnLevelActorDetachedHandle;
	FDelegateHandle OnSelectionChangedHandle;
	FDelegateHandle OnGlobalTimeChangedHandle;
	FDelegateHandle OnViewOptionsChangedHandle;
};
