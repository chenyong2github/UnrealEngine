// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerTrailHierarchy.h"
#include "MotionTrailEditorMode.h"
#include "MotionTrailTrackEditor.h"

#include "MovieSceneSequence.h"
#include "ISequencer.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "MovieSceneSection.h"

#include "ControlRig.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Sequencer/ControlRigSortedControls.h"
#include "AnimationBoneTrail.h"
//#include "ControlRigTransformTrail.h"

#include "MovieSceneTransformTrail.h"

namespace UE
{
namespace MotionTrailEditor
{

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

	OnViewOptionsChangedHandle = WeakEditorMode->GetTrailOptions()->OnDisplayPropertyChanged.AddLambda([this](FName PropertyName) {
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UMotionTrailOptions, SecondsPerSegment))
		{
			AllTrails[RootTrailGuid]->ForceEvaluateNextTick();
		}
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
		WeakEditorMode->GetTrailOptions()->OnDisplayPropertyChanged.Remove(OnViewOptionsChangedHandle);
	}

	if (GEngine)
	{
		GEngine->OnLevelActorAttached().Remove(OnLevelActorAttachedHandle);
		GEngine->OnLevelActorDetached().Remove(OnLevelActorDetachedHandle);
	}

	for (const TPair<UMovieSceneSection*, FControlRigDelegateHandles>& SectionHandlesPair : ControlRigDelegateHandles)
	{
		UMovieSceneControlRigParameterSection* Section = Cast<UMovieSceneControlRigParameterSection>(SectionHandlesPair.Key);
		FRigHierarchyContainer* RigHierarchy = Section->ControlRig->GetHierarchy();
		RigHierarchy->OnElementAdded.Remove(SectionHandlesPair.Value.OnControlAddedHandle);
		RigHierarchy->OnElementRemoved.Remove(SectionHandlesPair.Value.OnControlRemovedHandle);
		RigHierarchy->OnElementReparented.Remove(SectionHandlesPair.Value.OnControlReparentedHandle);
		RigHierarchy->OnElementRenamed.Remove(SectionHandlesPair.Value.OnControlRenamedHandle);
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
	BonesTracked.Reset();
	ControlsTracked.Reset();
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
	else
	{
		for (TPair<USkeletalMeshComponent*, TMap<FName, FGuid>>& CompMapPair : BonesTracked)
		{
			if (const FName* FoundBone = CompMapPair.Value.FindKey(Key))
			{
				CompMapPair.Value.Remove(*FoundBone);
				return;
			}
		}
		for (TPair<USkeletalMeshComponent*, TMap<FName, FGuid>>& CompMapPair : ControlsTracked)
		{
			if (const FName* FoundControl = CompMapPair.Value.FindKey(Key))
			{
				CompMapPair.Value.Remove(*FoundControl);
				return;
			}
		}
	}
}

void FSequencerTrailHierarchy::Update()
{
	const FDateTime UpdateStartTime = FDateTime::Now();

	UpdateViewRange();
	FTrailHierarchy::Update();

	const FTimespan UpdateTimespan = FDateTime::Now() - UpdateStartTime;
	TimingStats.Add("FSequencerTrailHierarchy::Update", UpdateTimespan);
}

void FSequencerTrailHierarchy::OnBoneVisibilityChanged(class USkeleton* Skeleton, const FName& BoneName)
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	check(Sequencer);

	TArray<FGuid> SelectedSequencerGuids;
	Sequencer->GetSelectedObjects(SelectedSequencerGuids);

	// TODO: potentially expensive
	for (const FGuid& SelectedGuid : SelectedSequencerGuids)
	{
		UMovieSceneSkeletalAnimationTrack* SkelAnimTrack = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->FindTrack<UMovieSceneSkeletalAnimationTrack>(SelectedGuid);
		for (TWeakObjectPtr<> BoundObject : Sequencer->FindObjectsInCurrentSequence(SelectedGuid))
		{
			USkeletalMeshComponent* BoundComponent = Cast<USkeletalMeshComponent>(BoundObject.Get());
			if (AActor* BoundActor = Cast<AActor>(BoundObject.Get()))
			{
				BoundComponent = BoundActor->FindComponentByClass<USkeletalMeshComponent>();
			}

			if (!BoundComponent || !BoundComponent->SkeletalMesh || !BoundComponent->SkeletalMesh->Skeleton || !(BoundComponent->SkeletalMesh->Skeleton == Skeleton) || !BonesTracked.Contains(BoundComponent))
			{
				continue;
			}

			const FGuid BoneTrailGuid = BonesTracked[BoundComponent][BoneName];
			const int32 BoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);
			const bool bIsVisible = !AllTrails[BoneTrailGuid]->GetDrawInfo()->IsVisible();

			if (!SelectionMask.Contains(BoneTrailGuid))
			{
				AllTrails[BoneTrailGuid]->GetDrawInfo()->SetIsVisible(bIsVisible);
			}
		}
	}
}

void FSequencerTrailHierarchy::UpdateSequencerBindings(const TArray<FGuid>& SequencerBindings, TFunctionRef<void(FTrail*)> OnUpdated)
{
	const FDateTime StartTime = FDateTime::Now();

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

				if (!SelectionMask.Contains(ObjectsTracked[BoundComponent]))
				{
					OnUpdated(AllTrails[ObjectsTracked[BoundComponent]].Get());
				}
				
			}
		} // if TransformTrack
		if (UMovieSceneSkeletalAnimationTrack* AnimTrack = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->FindTrack<UMovieSceneSkeletalAnimationTrack>(BindingGuid))
		{
			for (TWeakObjectPtr<> BoundObject : Sequencer->FindBoundObjects(BindingGuid, Sequencer->GetFocusedTemplateID()))
			{
				USkeletalMeshComponent* BoundComponent = Cast<USkeletalMeshComponent>(BoundObject.Get());
				if (AActor* BoundActor = Cast<AActor>(BoundObject.Get()))
				{
					BoundComponent = BoundActor->FindComponentByClass<USkeletalMeshComponent>();
				}

				if (!BoundComponent || !BoundComponent->SkeletalMesh || !BoundComponent->SkeletalMesh->Skeleton)
				{
					continue;
				}

				if (!ObjectsTracked.Contains(BoundComponent))
				{
					if (UMovieScene3DTransformTrack* TransformTrack = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->FindTrack<UMovieScene3DTransformTrack>(BindingGuid))
					{
						AddComponentToHierarchy(BoundComponent, TransformTrack);
					}
				}

				if (!ObjectsTracked.Contains(BoundComponent))
				{
					continue;
				}

				if (!BonesTracked.Contains(BoundComponent))
				{
					AddSkeletonToHierarchy(BoundComponent);
				}

				if (!BonesTracked.Contains(BoundComponent))
				{
					continue;
				}

				for (const TPair<FName, FGuid>& BoneNameGuidPair : BonesTracked[BoundComponent])
				{
					// TODO: hack for now
					const int32 BoneIndex = BoundComponent->SkeletalMesh->Skeleton->GetReferenceSkeleton().FindBoneIndex(BoneNameGuidPair.Key);
					if (false /* is visible */)
					{
						OnUpdated(AllTrails[BoneNameGuidPair.Value].Get());
					}
				}
			}
		}
		if (UMovieSceneControlRigParameterTrack* CRParameterTrack = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->FindTrack<UMovieSceneControlRigParameterTrack>(BindingGuid))
		{
			for (TWeakObjectPtr<> BoundObject : Sequencer->FindBoundObjects(BindingGuid, Sequencer->GetFocusedTemplateID()))
			{
				USkeletalMeshComponent* BoundComponent = Cast<USkeletalMeshComponent>(BoundObject.Get());
				if (AActor* BoundActor = Cast<AActor>(BoundObject.Get()))
				{
					BoundComponent = BoundActor->FindComponentByClass<USkeletalMeshComponent>();
				}

				if (!BoundComponent || !BoundComponent->SkeletalMesh || !BoundComponent->SkeletalMesh->Skeleton)
				{
					continue;
				}

				if (!ObjectsTracked.Contains(BoundComponent))
				{
					if (UMovieScene3DTransformTrack* TransformTrack = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->FindTrack<UMovieScene3DTransformTrack>(BindingGuid))
					{
						AddComponentToHierarchy(BoundComponent, TransformTrack);
					}
				}

				if (!ObjectsTracked.Contains(BoundComponent))
				{
					continue;
				}

				if (!ControlsTracked.Contains(BoundComponent))
				{
					AddControlsToHierarchy(BoundComponent, CRParameterTrack);
				}

				if (!ControlsTracked.Contains(BoundComponent))
				{
					continue;
				}

				for (const TPair<FName, FGuid>& ControlNameGuidPair : ControlsTracked[BoundComponent])
				{
					if (!SelectionMask.Contains(ControlNameGuidPair.Value))
					{
						OnUpdated(AllTrails[ControlNameGuidPair.Value].Get());
					}
				}
			}
		} // if ControlRigParameterTrack
	}

	const FTimespan Timespan = FDateTime::Now() - StartTime;
	TimingStats.Add("FSequencerTrailHierarchy::UpdateSequencerBindings", Timespan);
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

	UMovieScene3DTransformSection* TransformSection = FMovieSceneComponentTransformTrail::GetAbsoluteTransformSection(TransformTrack);
	TUniquePtr<FTrail> CurTrail = MakeUnique<FMovieSceneComponentTransformTrail>(FLinearColor::White, false, TransformSection, Sequencer);
	if (AllTrails.Contains(ObjectsTracked[CompToAdd])) AllTrails.Remove(ObjectsTracked[CompToAdd]);
	CurTrail->ForceEvaluateNextTick();

	AddTrail(ObjectsTracked[CompToAdd], Hierarchy[ObjectsTracked[CompToAdd]], MoveTemp(CurTrail));
}

void FSequencerTrailHierarchy::AddSkeletonToHierarchy(class USkeletalMeshComponent* CompToAdd)
{
	const FDateTime StartTime = FDateTime::Now();

	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	check(Sequencer);

	TSharedPtr<FAnimTrajectoryCache> AnimTrajectoryCache = MakeShared<FAnimTrajectoryCache>(CompToAdd, Sequencer);
	TMap<FName, FGuid>& BoneMap = BonesTracked.Add(CompToAdd, TMap<FName, FGuid>());

	USkeleton* MySkeleton = CompToAdd->SkeletalMesh->Skeleton;
	const int32 NumBones = MySkeleton->GetReferenceSkeleton().GetNum();
	for (int32 BoneIdx = 0; BoneIdx < NumBones; BoneIdx++)
	{
		int32 ParentBoneIndex = MySkeleton->GetReferenceSkeleton().GetParentIndex(BoneIdx);
		const FName BoneName = MySkeleton->GetReferenceSkeleton().GetBoneName(BoneIdx);

		const FGuid BoneGuid = FGuid::NewGuid();
		BoneMap.Add(BoneName, BoneGuid);
		FTrailHierarchyNode& BoneNode = Hierarchy.Add(BoneGuid, FTrailHierarchyNode());

		FGuid ParentGuid;
		FTrailHierarchyNode* ParentNode;
		if (ParentBoneIndex != INDEX_NONE)
		{
			AllTrails.Add(BoneGuid, MakeUnique<FAnimationBoneTrail>(FLinearColor::White, false, AnimTrajectoryCache, BoneName, false));

			const FName ParentName = MySkeleton->GetReferenceSkeleton().GetBoneName(ParentBoneIndex);
			ParentGuid = BoneMap[ParentName];
			ParentNode = &Hierarchy[ParentGuid];
		}
		else // root bone
		{
			AllTrails.Add(BoneGuid, MakeUnique<FAnimationBoneTrail>(FLinearColor::White, false, AnimTrajectoryCache, BoneName, true));

			ParentGuid = ObjectsTracked[CompToAdd];
			ParentNode = &Hierarchy[ParentGuid];
		}

		ParentNode->Children.Add(BoneGuid);
		BoneNode.Parents.Add(ParentGuid);
	}

	const FTimespan Timespan = FDateTime::Now() - StartTime;
	TimingStats.Add("FSequencerTrailHierarchy::AddSkeletonToHierarchy", Timespan);
}

void FSequencerTrailHierarchy::ResolveRigElementToRootComponent(FRigHierarchyContainer* RigHierarchy, FRigElementKey InElementKey, class USkeletalMeshComponent* Component)
{
	TMap<FName, FGuid>& ControlMap = ControlsTracked[Component];
	
	int32 ElementIndex = RigHierarchy->ControlHierarchy.GetIndex(InElementKey.Name);
	FRigElementKey ParentKey = RigHierarchy->ControlHierarchy[ElementIndex].SpaceName != NAME_None ? RigHierarchy->ControlHierarchy[ElementIndex].GetSpaceElementKey() : RigHierarchy->ControlHierarchy[ElementIndex].GetParentElementKey();

	FRigElementKey ChildItr = InElementKey;
	while (ParentKey.IsValid() && ParentKey.Type != ERigElementType::Bone)
	{
		if (ParentKey.Type == ERigElementType::Space) // TODO: add empty space trail with inital transform
		{
			ChildItr = ParentKey;
			ElementIndex = RigHierarchy->ControlHierarchy.GetIndex(ChildItr.Name);
			ParentKey = RigHierarchy->SpaceHierarchy[ElementIndex].GetParentElementKey();
			continue;
		}

		FGuid ChildGuid = ControlMap[ChildItr.Name];
		FTrailHierarchyNode& ChildNode = Hierarchy[ChildGuid];
		FGuid ParentGuid = ControlMap.FindOrAdd(ParentKey.Name, FGuid::NewGuid());
		FTrailHierarchyNode& ParentNode = Hierarchy.FindOrAdd(ParentGuid);

		if (!ParentNode.Children.Contains(ChildGuid)) ParentNode.Children.Add(ChildGuid);
		if (!ChildNode.Parents.Contains(ParentGuid)) ChildNode.Parents.Add(ParentGuid);

		ChildItr = ParentKey;
		ElementIndex = RigHierarchy->ControlHierarchy.GetIndex(ChildItr.Name);
		ParentKey = RigHierarchy->ControlHierarchy[ElementIndex].GetParentElementKey();
	}

	if (!ParentKey.IsValid()) // Add to root component trail
	{
		FGuid ChildGuid = ControlMap[ChildItr.Name];
		FTrailHierarchyNode& ChildNode = Hierarchy[ChildGuid];
		const FGuid CompRootGuid = ObjectsTracked[Component];
		if (!Hierarchy[CompRootGuid].Children.Contains(ControlMap[ChildItr.Name])) Hierarchy[CompRootGuid].Children.Add(ControlMap[ChildItr.Name]);
		if (!ChildNode.Parents.Contains(CompRootGuid)) ChildNode.Parents.Add(CompRootGuid);
	}
	else // if (ParentKey.Type == ERigElementType::Bone)
	{
		if (!BonesTracked.Contains(Component))
		{
			AddSkeletonToHierarchy(Component);
		}
		FGuid ChildGuid = ControlMap[ChildItr.Name];
		FTrailHierarchyNode& ChildNode = Hierarchy[ChildGuid];
		const FGuid SkelParentGuid = BonesTracked[Component][ParentKey.Name];
		if (!Hierarchy[SkelParentGuid].Children.Contains(ControlMap[ChildItr.Name])) Hierarchy[SkelParentGuid].Children.Add(ControlMap[ChildItr.Name]);
		if (!ChildNode.Parents.Contains(SkelParentGuid)) ChildNode.Parents.Add(SkelParentGuid);
	}
}

void FSequencerTrailHierarchy::AddControlsToHierarchy(class USkeletalMeshComponent* CompToAdd, UMovieSceneControlRigParameterTrack* CRParamTrack)
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	const TArray<UMovieSceneSection*>& Sections = CRParamTrack->GetAllSections();
	if (Sections.Num() == 0 || !Sequencer)
	{
		return;
	}

	TMap<FName, FGuid>& ControlMap = ControlsTracked.Add(CompToAdd, TMap<FName, FGuid>());

	// TODO: only handle one section for now
	// TODO: handle trails for multiple control value types, not just transforms
	UMovieSceneControlRigParameterSection* CRParamSection = Cast<UMovieSceneControlRigParameterSection>(Sections[0]);
	check(CRParamSection);

	FRigHierarchyContainer* RigHierarchy = CRParamSection->ControlRig->GetHierarchy();
	if (!ControlRigDelegateHandles.Contains(CRParamSection))
	{
		RegisterControlRigDelegates(CompToAdd, CRParamSection);
	}

	CRParamSection->ReconstructChannelProxy(true);

	TArray<FRigControl> SortedControls;
	FControlRigSortedControls::GetControlsInOrder(CRParamSection->ControlRig, SortedControls);
	for (const TPair<FName, FChannelMapInfo>& NameInfoPair : CRParamSection->ControlChannelMap)
	{
		const FRigControl& Control = SortedControls[NameInfoPair.Value.ControlIndex];
		const FRigElementKey RigKey = Control.GetElementKey();

		if (RigKey.Type != ERigElementType::Control)
		{
			continue;
		}
		
		const FGuid ControlGuid = ControlMap.FindOrAdd(RigKey.Name, FGuid::NewGuid());
		FTrailHierarchyNode& ControlNode = Hierarchy.FindOrAdd(ControlGuid, FTrailHierarchyNode());

		ResolveRigElementToRootComponent(RigHierarchy, RigKey, CompToAdd);

		TUniquePtr<FTrail> CurTrail = MakeUnique<FMovieSceneControlTransformTrail>(FLinearColor::White, false, CRParamSection, Sequencer, NameInfoPair.Value.ChannelIndex, RigKey.Name);
		if (AllTrails.Contains(ControlMap[RigKey.Name])) AllTrails.Remove(ControlMap[RigKey.Name]);

		AddTrail(ControlMap[RigKey.Name], Hierarchy[ControlMap[RigKey.Name]], MoveTemp(CurTrail));
	}
}

void FSequencerTrailHierarchy::RegisterControlRigDelegates(USkeletalMeshComponent* Component, class UMovieSceneControlRigParameterSection* CRParamSection)
{
	FRigHierarchyContainer* RigHierarchy = CRParamSection->ControlRig->GetHierarchy();
	FControlRigDelegateHandles& DelegateHandles = ControlRigDelegateHandles.Add(CRParamSection);
	DelegateHandles.OnControlAddedHandle = RigHierarchy->OnElementAdded.AddLambda(
		[this, RigHierarchy, Component, CRParamSection](FRigHierarchyContainer*, const FRigElementKey& NewElemKey) {
			TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
			check(Sequencer);
			if (NewElemKey.Type != ERigElementType::Control) // We only care about controls
			{
				return;
			}

			const FRigControl& Control = RigHierarchy->ControlHierarchy[NewElemKey.Name];
			if (Control.ParentName != NAME_None)
			{
				TMap<FName, FGuid>& ControlMap = ControlsTracked[Component];

				const FGuid ControlGuid = ControlMap.FindOrAdd(NewElemKey.Name, FGuid::NewGuid());
				FTrailHierarchyNode& ControlNode = Hierarchy.FindOrAdd(ControlGuid, FTrailHierarchyNode());

				ResolveRigElementToRootComponent(RigHierarchy, NewElemKey, Component);

				const int32 ChannelIndex = CRParamSection->ControlChannelMap[NewElemKey.Name].ChannelIndex;
				TUniquePtr<FTrail> CurTrail = MakeUnique<FMovieSceneControlTransformTrail>(FLinearColor::White, false, CRParamSection, Sequencer, ChannelIndex, NewElemKey.Name);
				if (AllTrails.Contains(ControlMap[NewElemKey.Name])) AllTrails.Remove(ControlMap[NewElemKey.Name]);

				AddTrail(ControlMap[NewElemKey.Name], Hierarchy[ControlMap[NewElemKey.Name]], MoveTemp(CurTrail));
			}
			else
			{
				// TODO: handle spaces
			}
		}
	);

	DelegateHandles.OnControlRemovedHandle = RigHierarchy->OnElementRemoved.AddLambda(
		[this, Component](FRigHierarchyContainer*, const FRigElementKey& ElemKey) {
			TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
			check(Sequencer);
			if (ElemKey.Type != ERigElementType::Control || !ControlsTracked.Contains(Component) || !ControlsTracked[Component].Contains(ElemKey.Name)) // We only care about controls
			{
				return;
			}
			
			const FGuid TrailGuid = ControlsTracked[Component][ElemKey.Name];
			RemoveTrail(TrailGuid);
		}
	);

	DelegateHandles.OnControlReparentedHandle = RigHierarchy->OnElementReparented.AddLambda(
		[this, Component](FRigHierarchyContainer*, const FRigElementKey& ElemKey, const FName& OldParent, const FName& NewParent) {
			TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
			check(Sequencer);
			if (ElemKey.Type != ERigElementType::Control || !ControlsTracked.Contains(Component) || 
				!ControlsTracked[Component].Contains(ElemKey.Name)) // We only care about controls
			{
				return;
			}

			const FGuid ElemGuid = ControlsTracked[Component][ElemKey.Name];
			const FGuid OldParentGuid = ControlsTracked[Component].Contains(OldParent) ? ControlsTracked[Component][OldParent] : 
				BonesTracked[Component].Contains(OldParent) ? BonesTracked[Component][OldParent] : FGuid();
			if (OldParentGuid.IsValid())
			{
				Hierarchy[OldParentGuid].Children.Remove(ElemGuid);
				Hierarchy[ElemGuid].Parents.Remove(OldParentGuid);
			}

			// Assuming parent is already in hierarchy
			const FGuid NewParentGuid = ControlsTracked[Component].Contains(NewParent) ? ControlsTracked[Component][NewParent] :
				BonesTracked[Component].Contains(NewParent) ? BonesTracked[Component][NewParent] : FGuid();
			check(NewParentGuid.IsValid());
			if (!Hierarchy[NewParentGuid].Children.Contains(ElemGuid))
			{
				Hierarchy[NewParentGuid].Children.Add(ElemGuid);
			}
			if (!Hierarchy[ElemGuid].Parents.Contains(NewParentGuid))
			{
				Hierarchy[ElemGuid].Parents.Add(NewParentGuid);
			}
		}
	);

	DelegateHandles.OnControlRenamedHandle = RigHierarchy->OnElementRenamed.AddLambda(
		[this, Component](FRigHierarchyContainer*, ERigElementType ElemType , const FName& OldName, const FName& NewName) {
			if (ElemType != ERigElementType::Control || !ControlsTracked.Contains(Component) || !ControlsTracked[Component].Contains(OldName))
			{
				return;
			}

			const FGuid TempTrailGuid = ControlsTracked[Component][OldName];
			ControlsTracked[Component].Remove(OldName);
			ControlsTracked[Component].Add(NewName, TempTrailGuid);
		}
	);
}

} // namespace MovieScene
} // namespace UE
