// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotionTrailTrackEditor.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "GameFramework/Actor.h"
#include "MotionTrailEditorMode.h"
#include "AnimationBoneTrail.h"
#include "SequencerTrailHierarchy.h"
#include "Editor.h"
#include "EditorModeManager.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Colors/SColorPicker.h"

#define LOCTEXT_NAMESPACE "MotionTrailTrackEditor"

namespace UE
{
namespace MotionTrailEditor
{
	
USkeletalMeshComponent* AcquireSkeletalMeshFromObjectGuid(const FGuid& Guid, TSharedPtr<ISequencer> SequencerPtr)
{
	UObject* BoundObject = SequencerPtr.IsValid() ? SequencerPtr->FindSpawnedObjectOrTemplate(Guid) : nullptr;

	if (AActor* Actor = Cast<AActor>(BoundObject))
	{
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(Component))
			{
				return SkeletalMeshComp;
			}
		}
	}
	else if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(BoundObject))
	{
		if (SkeletalMeshComponent->SkeletalMesh)
		{
			return SkeletalMeshComponent;
		}
	}

	return nullptr;
}

TSharedRef<ISequencerTrackEditor> FMotionTrailTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShareable(new FMotionTrailTrackEditor(InSequencer));
}

void FMotionTrailTrackEditor::OnInitialize()
{
	
}

void FMotionTrailTrackEditor::OnRelease()
{
	
}

bool FMotionTrailTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return false; // There is no track or section type associated with motion trails at the moment. This editor is only for setting up the context menu for motion trails.
}

void FMotionTrailTrackEditor::BuildObjectBindingContextMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	UMotionTrailEditorMode* EditorMode = Cast<UMotionTrailEditorMode>(GLevelEditorModeTools().GetActiveScriptableMode(UMotionTrailEditorMode::ModeName));
	if (!EditorMode)
	{
		return;
	}

	FSequencerTrailHierarchy* Hierarchy = static_cast<FSequencerTrailHierarchy*>(EditorMode->GetHierarchyForSequencer(GetSequencer().Get()));

	if (ObjectClass->IsChildOf(USkeletalMeshComponent::StaticClass()) || ObjectClass->IsChildOf(AActor::StaticClass()) || ObjectClass->IsChildOf(UChildActorComponent::StaticClass()))
	{
		if (ObjectBindings.Num() > 0)
		{
			USkeletalMeshComponent* SkelMeshComp = UE::MotionTrailEditor::AcquireSkeletalMeshFromObjectGuid(ObjectBindings[0], GetSequencer());
			UObject* BoundObject = GetSequencer()->FindSpawnedObjectOrTemplate(ObjectBindings[0]);
			if (AActor* BoundActor = Cast<AActor>(BoundObject))
			{
				BoundObject = BoundActor->GetRootComponent();
			}

			if (Hierarchy->GetBonesTracked().Contains(SkelMeshComp) || Hierarchy->GetControlsTracked().Contains(SkelMeshComp) || Hierarchy->GetObjectsTracked().Contains(BoundObject))
			{
				MenuBuilder.BeginSection("Motion Trail Options", LOCTEXT("MotionTrailOptions", "Motion Trail Options"));
				if(Hierarchy->GetObjectsTracked().Contains(BoundObject))
				{
					const FGuid SelectedCompGuid = Hierarchy->GetObjectsTracked().FindChecked(BoundObject);

					TArray<FGuid> SelectedTrails;
					SelectedTrails.Add(SelectedCompGuid);
					for (const TPair<FName, FGuid>& NameGuidPair : Hierarchy->GetBonesTracked().FindRef(SkelMeshComp))
					{
						SelectedTrails.Add(NameGuidPair.Value);
					}
					
					for (const TPair<FName, FGuid>& NameGuidPair : Hierarchy->GetControlsTracked().FindRef(SkelMeshComp))
					{
						SelectedTrails.Add(NameGuidPair.Value);
					}

					VisibilityStates.FindOrAdd(BoundObject, EBindingVisibilityState::VisibleWhenSelected);

					MenuBuilder.AddMenuEntry(LOCTEXT("VisibleWhenSelected", "Visible When Selected"), LOCTEXT("VisibleWhenSelectedTooltip", "Makes the trails for this object visible when it is selected"), FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([this, BoundObject, Hierarchy]() { 
								VisibilityStates.FindChecked(BoundObject) = EBindingVisibilityState::VisibleWhenSelected;
								Hierarchy->OnBindingVisibilityStateChanged(BoundObject, EBindingVisibilityState::VisibleWhenSelected); 
							}), 
							FCanExecuteAction::CreateLambda([]() { return true; }),
							FIsActionChecked::CreateLambda([this, BoundObject]() { return VisibilityStates.FindChecked(BoundObject) == EBindingVisibilityState::VisibleWhenSelected; })
						), NAME_None, EUserInterfaceActionType::RadioButton
					);

					MenuBuilder.AddMenuEntry(LOCTEXT("AlwaysVisible", "Always Visible"), LOCTEXT("AlwaysVisibleTooltip", "Makes the trails for this object always visible"), FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([this, BoundObject, Hierarchy]() {
							VisibilityStates.FindChecked(BoundObject) = EBindingVisibilityState::AlwaysVisible;
							Hierarchy->OnBindingVisibilityStateChanged(BoundObject, EBindingVisibilityState::AlwaysVisible);
						}),
						FCanExecuteAction::CreateLambda([]() { return true; }),
						FIsActionChecked::CreateLambda([this, BoundObject]() { return VisibilityStates.FindChecked(BoundObject) == EBindingVisibilityState::AlwaysVisible; })
						), NAME_None, EUserInterfaceActionType::RadioButton
					);
				}

				if (SkelMeshComp && SkelMeshComp->SkeletalMesh && SkelMeshComp->SkeletalMesh->GetSkeleton() && Hierarchy->GetBonesTracked().Contains(SkelMeshComp))
				{
					USkeleton* Skeleton = SkelMeshComp->SkeletalMesh->GetSkeleton();
					const FName RootBoneName = Skeleton->GetReferenceSkeleton().GetBoneName(0);
					const FGuid RootBoneTrailGuid = Hierarchy->GetBonesTracked().FindChecked(SkelMeshComp).FindChecked(RootBoneName);
					const FGuid ComponentGuid = Hierarchy->GetObjectsTracked().FindChecked(SkelMeshComp);
					FAnimationBoneTrail* RootTrail = static_cast<FAnimationBoneTrail*>(Hierarchy->GetAllTrails().FindChecked(RootBoneTrailGuid).Get());
					TWeakPtr<FAnimTrajectoryCache> AnimCache = static_cast<FAnimBoneTrajectoryCache*>(RootTrail->GetTrajectoryTransforms())->GetAnimCache();
					FTrajectoryCache* ParentCache = Hierarchy->GetAllTrails().FindChecked(ComponentGuid)->GetTrajectoryTransforms();

					MenuBuilder.AddMenuEntry(LOCTEXT("GenerateBoneTrails", "Generate Bone Trails"), LOCTEXT("GenerateBoneTrailsTooltip", "Evaluates trails for every bone in the animation, can be expensive"), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([AnimCache, ParentCache, SkelMeshComp]() { AnimCache.Pin()->Evaluate(ParentCache); })));
					MenuBuilder.AddSubMenu(LOCTEXT("VisibleBones", "Visible Bones"), LOCTEXT("VisibleBonesTooltip", "Set which bone trails should be visible"), FNewMenuDelegate::CreateRaw(this, &FMotionTrailTrackEditor::CreateBoneVisibilityMenu, Skeleton, Hierarchy),
						FUIAction(FExecuteAction::CreateLambda([] {}), FCanExecuteAction::CreateLambda([AnimCache] {
						return AnimCache.IsValid() && !AnimCache.Pin()->IsDirty();
					}), EUIActionRepeatMode::RepeatDisabled), NAME_None, EUserInterfaceActionType::Button, false, FSlateIcon(), false);
				}

				MenuBuilder.EndSection();
			}
		}
	}
}

void FMotionTrailTrackEditor::CreateBoneVisibilityMenu(FMenuBuilder& MenuBuilder, USkeleton* Skeleton, FSequencerTrailHierarchy* Hierarchy)
{
	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	BoneVisibilities.FindOrAdd(Skeleton, TBitArray<>(false, Skeleton->GetReferenceSkeleton().GetNum()));

	for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); BoneIndex++)
	{
		const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
		MenuBuilder.AddMenuEntry(
			FText::FromName(BoneName),
			LOCTEXT("SelectBoneTooltip", "Select bone"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, BoneIndex, BoneName, Skeleton, Hierarchy]() {
					TBitArray<>& Visibilities = BoneVisibilities[Skeleton];
					Visibilities[BoneIndex] = !Visibilities[BoneIndex];
					Hierarchy->OnBoneVisibilityChanged(Skeleton, BoneName, Visibilities[BoneIndex]);
				}),
				FCanExecuteAction::CreateLambda([]() { return true; }),
				FIsActionChecked::CreateLambda([this, BoneIndex, Skeleton]() { return BoneVisibilities[Skeleton][BoneIndex]; })
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
}

} // namespace MovieScene
} // namespace UE

#undef LOCTEXT_NAMESPACE
