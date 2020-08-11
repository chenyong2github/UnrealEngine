// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerTrailHierarchy.h"

#include "MovieSceneSequence.h"
#include "ISequencer.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "MovieSceneSection.h"

#include "ControlRig.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Sequencer/ControlRigSortedControls.h"
//#include "ControlRigTransformTrail.h"

#include "MovieSceneTransformTrail.h"

void FSequencerTrailHierarchy::Initialize()
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	if (!Sequencer)
	{
		return;
	}
	
	OnActorAddedToSequencerHandle = Sequencer->OnActorAddedToSequencer().AddLambda([this](AActor* InActor, const FGuid InGuid) {
		UMovieScene3DTransformTrack* TransformTrack = WeakSequencer.Pin()->GetFocusedMovieSceneSequence()->GetMovieScene()->FindTrack<UMovieScene3DTransformTrack>(InGuid);
		if (TransformTrack)
		{
			AddComponentToHierarchy(InActor->GetRootComponent(), TransformTrack);
		}
	});

	OnLevelActorAttachedHandle = GEngine ? GEngine->OnLevelActorAttached().AddLambda([this](AActor* InActor, const AActor* InParent) {
		USceneComponent* RootComponent = InActor->GetRootComponent();
		if (!ObjectsTracked.Contains(RootComponent))
		{
			return;
		}

		for (const FGuid& ParentGuid : Hierarchy[ObjectsTracked[RootComponent]].Parents)
		{
			Hierarchy[ParentGuid].Children.Remove(ObjectsTracked[RootComponent]);
		}
		Hierarchy[ObjectsTracked[RootComponent]].Parents.Reset();

		ResolveComponentToRoot(RootComponent);
		
		AllTrails[ObjectsTracked[RootComponent]]->ForceEvaluateNextTick();
	}) : FDelegateHandle();

	OnLevelActorDetachedHandle = GEngine ? GEngine->OnLevelActorDetached().AddLambda([this](AActor* InActor, const AActor* InParent) {
		USceneComponent* RootComponent = InActor->GetRootComponent();
		USceneComponent* ParentRootComponent = InParent->GetRootComponent();
		if (!ObjectsTracked.Contains(RootComponent))
		{
			return;
		}

		if (ObjectsTracked.Contains(ParentRootComponent))
		{
			Hierarchy[ObjectsTracked[ParentRootComponent]].Children.Remove(ObjectsTracked[RootComponent]);
		}

		Hierarchy[RootTrailGuid].Children.Add(ObjectsTracked[RootComponent]);
		Hierarchy[ObjectsTracked[RootComponent]].Parents.Reset();
		Hierarchy[ObjectsTracked[RootComponent]].Parents.Add(RootTrailGuid);

		AllTrails[ObjectsTracked[RootComponent]]->ForceEvaluateNextTick();
	}) : FDelegateHandle();

	OnSelectionChangedHandle = Sequencer->GetSelectionChangedObjectGuids().AddLambda([this](TArray<FGuid> NewSelection) {
		TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
		check(Sequencer);

		for (TPair<FGuid, TUniquePtr<FTrail>>& GuidTrailPair : AllTrails)
		{
			if (GuidTrailPair.Value->GetDrawInfo()) GuidTrailPair.Value->GetDrawInfo()->SetIsVisible(false);
		}
		
		auto SetVisibleFunc = [this](FTrail* TrailPtr) {
			FTrajectoryDrawInfo* DrawInfo = TrailPtr->GetDrawInfo();
			if (DrawInfo && !DrawInfo->IsVisible())
			{
				DrawInfo->SetIsVisible(true);
			}
		};

		UpdateSequencerBindings(NewSelection, SetVisibleFunc);
	});

	OnViewOptionsChangedHandle = WeakEditorMode->GetTrailOptions()->OnDisplayPropertyChanged.AddLambda([this](FName) {
		AllTrails[RootTrailGuid]->ForceEvaluateNextTick();
	});

	UpdateViewRange();

	RootTrailGuid = FGuid::NewGuid();
	TUniquePtr<FTrail> RootTrail = MakeUnique<FRootTrail>();
	AllTrails.Add(RootTrailGuid, MoveTemp(RootTrail));
	Hierarchy.Add(RootTrailGuid, FTrailHierarchyNode());

	TArray<FGuid> SequencerSelectedObjects;
	Sequencer->GetSelectedObjects(SequencerSelectedObjects);
	UpdateSequencerBindings(SequencerSelectedObjects, 
		[this](FTrail* TrailPtr) {
			FTrajectoryDrawInfo* DrawInfo = TrailPtr->GetDrawInfo();
			if (DrawInfo && !DrawInfo->IsVisible())
			{
				DrawInfo->SetIsVisible(true);
			}
		}
	);
}

void FSequencerTrailHierarchy::Destroy()
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	if (Sequencer)
	{
		Sequencer->OnActorAddedToSequencer().Remove(OnActorAddedToSequencerHandle);
		Sequencer->GetSelectionChangedObjectGuids().Remove(OnSelectionChangedHandle);
		Sequencer->OnGlobalTimeChanged().Remove(OnGlobalTimeChangedHandle);
		WeakEditorMode->GetTrailOptions()->OnDisplayPropertyChanged.Remove(OnViewOptionsChangedHandle);
	}

	if (GEngine)
	{
		GEngine->OnLevelActorAttached().Remove(OnLevelActorAttachedHandle);
		GEngine->OnLevelActorDetached().Remove(OnLevelActorDetachedHandle);
	}

	for (TPair<FGuid, TUniquePtr<FTrail>>& GuidTrailPair : AllTrails)
	{
		const TMap<FString, FInteractiveTrailTool*>& ToolsForTrail = GuidTrailPair.Value->GetTools();
		for (const TPair<FString, FInteractiveTrailTool*>& NameToolPair : ToolsForTrail)
		{
			WeakEditorMode->RemoveTrailTool(NameToolPair.Key, NameToolPair.Value);
		}
	}

	Hierarchy.Reset();
	ObjectsTracked.Reset();
	AllTrails.Reset();
	RootTrailGuid = FGuid();
}

void FSequencerTrailHierarchy::RemoveTrail(const FGuid& Key)
{
	FTrailHierarchy::RemoveTrail(Key);
	if (UObject* const* FoundObject = ObjectsTracked.FindKey(Key))
	{
		ObjectsTracked.Remove(*FoundObject);
	}
	else if (const FName* FoundControl = ControlsTracked.FindKey(Key))
	{
		ControlsTracked.Remove(*FoundControl);
	}
}

void FSequencerTrailHierarchy::UpdateSequencerBindings(const TArray<FGuid>& SequencerBindings, TFunctionRef<void(FTrail*)> OnUpdated)
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	check(Sequencer);

	for (FGuid BindingGuid : SequencerBindings)
	{
		if (UMovieScene3DTransformTrack* TransformTrack = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->FindTrack<UMovieScene3DTransformTrack>(BindingGuid))
		{
			for (TWeakObjectPtr<> BoundObject : Sequencer->FindBoundObjects(BindingGuid, Sequencer->GetFocusedTemplateID()))
			{
				USceneComponent* BoundComponent = Cast<USceneComponent>(BoundObject.Get());
				if (AActor* BoundActor = Cast<AActor>(BoundObject.Get()))
				{
					BoundComponent = BoundActor->GetRootComponent();
				}

				if (!ObjectsTracked.Contains(BoundComponent))
				{
					AddComponentToHierarchy(BoundComponent, TransformTrack);
				}

				if (!ObjectsTracked.Contains(BoundComponent))
				{
					continue;
				}

				OnUpdated(AllTrails[ObjectsTracked[BoundComponent]].Get());
				
			}
		} // if TransformTrack
		else if (UMovieSceneControlRigParameterTrack* CRParameterTrack = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->FindTrack<UMovieSceneControlRigParameterTrack>(BindingGuid))
		{

		} // if ControlRigParameterTrack
	}
}

void FSequencerTrailHierarchy::UpdateObjectsTracked()
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	if (!Sequencer)
	{
		return;
	}

	// Clear hierarchy
	Hierarchy.Reset();
	ObjectsTracked.Reset();

	// Then re-build
	RootTrailGuid = FGuid::NewGuid();
	TUniquePtr<FTrail> RootTrail = MakeUnique<FRootTrail>();
	AllTrails.Add(RootTrailGuid, MoveTemp(RootTrail));
	Hierarchy.Add(RootTrailGuid, FTrailHierarchyNode());

	TArray<FGuid> SequencerBoundGuids;
	for (const FMovieSceneBinding& Binding : Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->GetBindings())
	{
		SequencerBoundGuids.Add(Binding.GetObjectGuid());
	}

	UpdateSequencerBindings(SequencerBoundGuids, [](FTrail*) {});
}

void FSequencerTrailHierarchy::UpdateViewRange()
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	check(Sequencer);

	FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
	FFrameRate DisplayRate = Sequencer->GetFocusedDisplayRate();

	TRange<FFrameNumber> TickViewRange;
	if (!WeakEditorMode->GetTrailOptions()->bShowFullTrail)
	{
		FFrameTime SequenceTime = Sequencer->GetLocalTime().Time;
		const FFrameNumber TicksBefore = FFrameRate::TransformTime(FFrameNumber(WeakEditorMode->GetTrailOptions()->FramesBefore), DisplayRate, TickResolution).FloorToFrame();
		const FFrameNumber TicksAfter = FFrameRate::TransformTime(FFrameNumber(WeakEditorMode->GetTrailOptions()->FramesAfter), DisplayRate, TickResolution).FloorToFrame();
		TickViewRange = TRange<FFrameNumber>(SequenceTime.GetFrame() - TicksBefore, SequenceTime.GetFrame() + TicksAfter);
	}
	else
	{
		TickViewRange = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->GetPlaybackRange();
	}

	const double StartSeconds = TickResolution.AsSeconds(FFrameTime(TickViewRange.GetLowerBoundValue()));
	const double EndSeconds = TickResolution.AsSeconds(FFrameTime(TickViewRange.GetUpperBoundValue()));

	// snap view range to ticks per segment
	const double TicksBetween = FMath::Fmod(StartSeconds, WeakEditorMode->GetTrailOptions()->SecondsPerSegment);
	ViewRange = TRange<double>(StartSeconds - TicksBetween, EndSeconds - TicksBetween);
}

void FSequencerTrailHierarchy::ResolveComponentToRoot(USceneComponent* Component)
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	check(Sequencer);

	const FGuid CurTrailGuid = ObjectsTracked.FindOrAdd(Component, FGuid::NewGuid());
	FTrailHierarchyNode& CurTrailNode = Hierarchy.FindOrAdd(CurTrailGuid);

	if (!ObjectsTracked.Contains(Component->GetAttachParent()) || !CurTrailNode.Parents.Contains(ObjectsTracked[Component->GetAttachParent()]))
	{
		USceneComponent* ChildItr = Component;
		FTrailHierarchyNode* ChildNode = &CurTrailNode;
		while (ChildItr->GetAttachParent() != nullptr)
		{
			FGuid ChildGuid = ObjectsTracked[ChildItr];
			FGuid ParentGuid = ObjectsTracked.FindOrAdd(ChildItr->GetAttachParent(), FGuid::NewGuid());
			FTrailHierarchyNode& ParentNode = Hierarchy.FindOrAdd(ParentGuid);

			if (!AllTrails.Contains(ParentGuid))
			{
				AllTrails.Add(ParentGuid, MakeUnique<FConstantComponentTrail>(ChildItr->GetAttachParent()));
			}

			if (!ParentNode.Children.Contains(ChildGuid)) ParentNode.Children.Add(ChildGuid);
			if (!ChildNode->Parents.Contains(ParentGuid)) ChildNode->Parents.Add(ParentGuid);

			ChildItr = ChildItr->GetAttachParent();
			ChildNode = &ParentNode;
		}

		if (!Hierarchy[RootTrailGuid].Children.Contains(ObjectsTracked[ChildItr])) Hierarchy[RootTrailGuid].Children.Add(ObjectsTracked[ChildItr]);
		if (!ChildNode->Parents.Contains(RootTrailGuid)) ChildNode->Parents.Add(RootTrailGuid);
	}
}

void FSequencerTrailHierarchy::AddComponentToHierarchy(USceneComponent* CompToAdd, UMovieScene3DTransformTrack* TransformTrack)
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	check(Sequencer);
	
	ResolveComponentToRoot(CompToAdd);

	TUniquePtr<FTrail> CurTrail = MakeUnique<FMovieSceneTransformTrail>(FLinearColor::White, false, TransformTrack, Sequencer);
	if (AllTrails.Contains(ObjectsTracked[CompToAdd])) AllTrails.Remove(ObjectsTracked[CompToAdd]);
	CurTrail->ForceEvaluateNextTick();

	AddTrail(ObjectsTracked[CompToAdd], Hierarchy[ObjectsTracked[CompToAdd]], MoveTemp(CurTrail));
}
