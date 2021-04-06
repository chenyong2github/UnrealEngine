// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigSequencerEditorLibrary.h"
#include "LevelSequence.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "ISequencer.h"
#include "ControlRigEditorModule.h"
#include "Channels/FloatChannelCurveModel.h"
#include "TransformNoScale.h"
#include "ControlRigComponent.h"
#include "MovieSceneToolHelpers.h"
#include "Rigs/FKControlRig.h"
#include "ControlRig/Private/Units/Execution/RigUnit_InverseExecution.h"
#include "ControlRig.h"
#include "ControlRigEditMode.h"
#include "EditorModeManager.h"
#include "Engine/Selection.h"
#include "ControlRigObjectBinding.h"
#include "Engine/SCS_Node.h"
#include "ILevelSequenceEditorToolkit.h"
#include "ISequencer.h"
#include "Tools/ControlRigTweener.h"
#include "LevelSequencePlayer.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "Logging/LogMacros.h"
#include "ControlRig/Private/Units/Execution/RigUnit_InverseExecution.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Exporters/AnimSeqExportOption.h"
#include "MovieSceneTimeHelpers.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "ControlrigSequencerEditorLibrary"

TArray<UControlRig*> UControlRigSequencerEditorLibrary::GetVisibleControlRigs()
{
	TArray<UControlRig*> ControlRigs;
	FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
	if (ControlRigEditMode && ControlRigEditMode->GetControlRig(true) && ControlRigEditMode->GetControlRig(true)->GetObjectBinding() &&\
		ControlRigEditMode->GetControlRig(true)->GetObjectBinding()->GetBoundObject())
	{
		ControlRigs.Add(ControlRigEditMode->GetControlRig(true));
	}
	return ControlRigs;
}

TArray<FControlRigSequencerBindingProxy> UControlRigSequencerEditorLibrary::GetControlRigs(ULevelSequence* LevelSequence)
{
	TArray<FControlRigSequencerBindingProxy> ControlRigBindingProxies;
	if (LevelSequence)
	{
		UMovieScene* MovieScene = LevelSequence->GetMovieScene();
		if (MovieScene)
		{
			const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
			for (const FMovieSceneBinding& Binding : Bindings)
			{
				TArray<UMovieSceneTrack*> Tracks = MovieScene->FindTracks(UMovieSceneControlRigParameterTrack::StaticClass(), Binding.GetObjectGuid(), NAME_None);
				for (UMovieSceneTrack* AnyOleTrack : Tracks)
				{
					UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(AnyOleTrack);
					if (Track && Track->GetControlRig())
					{
						FControlRigSequencerBindingProxy BindingProxy;
						BindingProxy.ControlRig = Track->GetControlRig();
						BindingProxy.Proxy.BindingID = Binding.GetObjectGuid();
						BindingProxy.Proxy.Sequence = LevelSequence;
						ControlRigBindingProxies.Add(BindingProxy);
					}
				}
			}
		}
	}
	return ControlRigBindingProxies;
}

static void AcquireSkeletonAndSkelMeshCompFromObject(UObject* BoundObject, USkeleton** OutSkeleton, USkeletalMeshComponent** OutSkeletalMeshComponent)
{
	*OutSkeletalMeshComponent = nullptr;
	*OutSkeleton = nullptr;
	if (AActor* Actor = Cast<AActor>(BoundObject))
	{
		for (UActorComponent* Component : Actor->GetComponents())
		{
			USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(Component);
			if (SkeletalMeshComp)
			{
				*OutSkeletalMeshComponent = SkeletalMeshComp;
				if (SkeletalMeshComp->SkeletalMesh && SkeletalMeshComp->SkeletalMesh->GetSkeleton())
				{
					*OutSkeleton = SkeletalMeshComp->SkeletalMesh->GetSkeleton();
				}
				return;
			}
		}

		AActor* ActorCDO = Cast<AActor>(Actor->GetClass()->GetDefaultObject());
		if (ActorCDO)
		{
			for (UActorComponent* Component : ActorCDO->GetComponents())
			{
				USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(Component);
				if (SkeletalMeshComp)
				{
					*OutSkeletalMeshComponent = SkeletalMeshComp;
					if (SkeletalMeshComp->SkeletalMesh && SkeletalMeshComp->SkeletalMesh->GetSkeleton())
					{
						*OutSkeleton = SkeletalMeshComp->SkeletalMesh->GetSkeleton();
					}
					return;
				}
			}
		}

		UBlueprintGeneratedClass* ActorBlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(Actor->GetClass());
		if (ActorBlueprintGeneratedClass)
		{
			const TArray<USCS_Node*>& ActorBlueprintNodes = ActorBlueprintGeneratedClass->SimpleConstructionScript->GetAllNodes();

			for (USCS_Node* Node : ActorBlueprintNodes)
			{
				if (Node->ComponentClass->IsChildOf(USkeletalMeshComponent::StaticClass()))
				{
					USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(Node->GetActualComponentTemplate(ActorBlueprintGeneratedClass));
					if (SkeletalMeshComp)
					{
						*OutSkeletalMeshComponent = SkeletalMeshComp;
						if (SkeletalMeshComp->SkeletalMesh && SkeletalMeshComp->SkeletalMesh->GetSkeleton())
						{
							*OutSkeleton = SkeletalMeshComp->SkeletalMesh->GetSkeleton();
						}
					}
				}
			}
		}
	}
	else if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(BoundObject))
	{
		*OutSkeletalMeshComponent = SkeletalMeshComponent;
		if (SkeletalMeshComponent->SkeletalMesh && SkeletalMeshComponent->SkeletalMesh->GetSkeleton())
		{
			*OutSkeleton = SkeletalMeshComponent->SkeletalMesh->GetSkeleton();
		}
	}
}

static TSharedPtr<ISequencer> GetSequencerFromAsset(ULevelSequence* LevelSequence)
{
	IAssetEditorInstance* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(LevelSequence, false);
	ILevelSequenceEditorToolkit* LevelSequenceEditor = static_cast<ILevelSequenceEditorToolkit*>(AssetEditor);
	TSharedPtr<ISequencer> Sequencer = LevelSequenceEditor ? LevelSequenceEditor->GetSequencer() : nullptr;
	if (Sequencer.IsValid() == false)
	{
		UE_LOG(LogControlRig, Error, TEXT("Can not open Sequencer for the LevelSequence %s"), *(LevelSequence->GetPathName()));
	}
	return Sequencer;
}

static UMovieSceneControlRigParameterTrack* AddControlRig(ULevelSequence* LevelSequence,const UClass* InClass, UObject* BoundActor, FGuid ObjectBinding, UControlRig* InExistingControlRig)
{
	FSlateApplication::Get().DismissAllMenus();

	if (InClass && InClass->IsChildOf(UControlRig::StaticClass()) && LevelSequence && LevelSequence->GetMovieScene())
	{
		UMovieScene* OwnerMovieScene = LevelSequence->GetMovieScene();
		TSharedPtr<ISequencer> SharedSequencer = GetSequencerFromAsset(LevelSequence);
		ISequencer* Sequencer = nullptr; // will be valid  if we have a ISequencer AND it's focused.
		if (SharedSequencer.IsValid() && SharedSequencer->GetFocusedMovieSceneSequence() == LevelSequence)
		{
			Sequencer = SharedSequencer.Get();
		}
		LevelSequence->Modify();
		OwnerMovieScene->Modify();
		UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(OwnerMovieScene->AddTrack(UMovieSceneControlRigParameterTrack::StaticClass(), ObjectBinding));
		if (Track)
		{
			FString ObjectName = InClass->GetName(); //GetDisplayNameText().ToString();
			ObjectName.RemoveFromEnd(TEXT("_C"));

			bool bSequencerOwnsControlRig = false;
			UControlRig* ControlRig = InExistingControlRig;
			if (ControlRig == nullptr)
			{
				ControlRig = NewObject<UControlRig>(Track, InClass, FName(*ObjectName), RF_Transactional);
				bSequencerOwnsControlRig = true;
			}

			ControlRig->Modify();
			ControlRig->SetObjectBinding(MakeShared<FControlRigObjectBinding>());
			ControlRig->GetObjectBinding()->BindToObject(BoundActor);
			ControlRig->GetDataSourceRegistry()->RegisterDataSource(UControlRig::OwnerComponent, ControlRig->GetObjectBinding()->GetBoundObject());
			ControlRig->Initialize();
			ControlRig->Evaluate_AnyThread();


			Track->Modify();
			UMovieSceneSection* NewSection = Track->CreateControlRigSection(0, ControlRig, bSequencerOwnsControlRig);
			NewSection->Modify();

			//mz todo need to have multiple rigs with same class
			Track->SetTrackName(FName(*ObjectName));
			Track->SetDisplayName(FText::FromString(ObjectName));

			if (Sequencer)
			{
				Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
				Sequencer->EmptySelection();
				Sequencer->SelectSection(NewSection);
				Sequencer->ThrobSectionSelection();
				Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
				Sequencer->ObjectImplicitlyAdded(ControlRig);
			}

			FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
			if (!ControlRigEditMode)
			{
				GLevelEditorModeTools().ActivateMode(FControlRigEditMode::ModeName);
				ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));

			}
			if (ControlRigEditMode)
			{
				ControlRigEditMode->SetObjects(ControlRig, nullptr, SharedSequencer);
			}
			return Track;
		}
	}
	return nullptr;
}


UMovieSceneTrack* UControlRigSequencerEditorLibrary::FindOrCreateControlRigTrack(ULevelSequence* LevelSequence, const UClass* ControlRigClass, const FSequencerBindingProxy& InBinding)
{
	UMovieScene* MovieScene = InBinding.Sequence ? InBinding.Sequence->GetMovieScene() : nullptr;
	UMovieSceneTrack* BaseTrack = nullptr;
	if (LevelSequence && MovieScene && InBinding.BindingID.IsValid())
	{
		const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
		for (const FMovieSceneBinding& Binding : Bindings)
		{
			if (Binding.GetObjectGuid() == InBinding.BindingID)
			{
				TArray<UMovieSceneTrack*> Tracks = MovieScene->FindTracks(UMovieSceneControlRigParameterTrack::StaticClass(), Binding.GetObjectGuid(), NAME_None);
				for (UMovieSceneTrack* AnyOleTrack : Tracks)
				{
					UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(AnyOleTrack);
					if (Track && Track->GetControlRig() && Track->GetControlRig()->GetClass() == ControlRigClass)
					{
						return Track;
					}
				}

				TArray<UObject*, TInlineAllocator<1>> Result;
				UObject* Context = nullptr;
				LevelSequence->LocateBoundObjects(InBinding.BindingID, Context, Result);
				if (Result.Num() > 0 && Result[0])
				{
					UObject* BoundObject = Result[0];
					USkeleton* Skeleton = nullptr;
					USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
					AcquireSkeletonAndSkelMeshCompFromObject(BoundObject, &Skeleton, &SkeletalMeshComponent);

					UControlRig* ControlRig = nullptr;
						

					if (Skeleton && SkeletalMeshComponent)
					{
						UMovieSceneControlRigParameterTrack* Track = AddControlRig(LevelSequence, ControlRigClass, SkeletalMeshComponent, InBinding.BindingID, nullptr);

						if (Track)
						{
							BaseTrack = Track;								
						}
					}
				}
			}
		}
	}
	return BaseTrack;
}

TArray<UMovieSceneTrack*> UControlRigSequencerEditorLibrary::FindOrCreateControlRigComponentTrack(ULevelSequence* LevelSequence, const FSequencerBindingProxy& InBinding)
{
	TArray< UMovieSceneTrack*> Tracks;
	TArray<UObject*, TInlineAllocator<1>> Result;

	UObject* Context = nullptr;
	LevelSequence->LocateBoundObjects(InBinding.BindingID, Context, Result);
	if (Result.Num() > 0 && Result[0])
	{
		UObject* BoundObject = Result[0];
		
		if (AActor* BoundActor = Cast<AActor>(BoundObject))
		{
			TArray<UControlRigComponent*> ControlRigComponents;
			BoundActor->GetComponents<UControlRigComponent>(ControlRigComponents);
			for (UControlRigComponent* ControlRigComponent : ControlRigComponents)
			{
				if (UControlRig* CR = ControlRigComponent->GetControlRig())
				{
					UMovieSceneControlRigParameterTrack* Track = AddControlRig(LevelSequence,CR->GetClass(), BoundActor, InBinding.BindingID, CR);
					Tracks.Add(Track);
				}
			}
		}
	}
	return Tracks;
}


bool UControlRigSequencerEditorLibrary::TweenControlRig(ULevelSequence* LevelSequence, UControlRig* ControlRig, float TweenValue)
{
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);
	if (WeakSequencer.IsValid() && WeakSequencer.Pin()->GetFocusedMovieSceneSequence() == LevelSequence
		&& ControlRig && LevelSequence->GetMovieScene())
	{
		FControlsToTween ControlsToTween;
		LevelSequence->GetMovieScene()->Modify();
		TArray<UControlRig*> SelectedControlRigs;
		SelectedControlRigs.Add(ControlRig);
		ControlsToTween.Setup(SelectedControlRigs, WeakSequencer);
		ControlsToTween.Blend(WeakSequencer, TweenValue);
		return true;
	}
	return false;
}


bool UControlRigSequencerEditorLibrary::SnapControlRig(FFrameNumber StartFrame, FFrameNumber EndFrame, const FControlRigSnapperSelection& ChildrenToSnap,
	const FControlRigSnapperSelection& ParentToSnap, const UControlRigSnapSettings* SnapSettings)
{
	FControlRigSnapper Snapper;
	return Snapper.SnapIt(StartFrame, EndFrame, ChildrenToSnap, ParentToSnap,SnapSettings);
}

FTransform UControlRigSequencerEditorLibrary::GetActorWorldTransform(ULevelSequence* LevelSequence, AActor* Actor, FFrameNumber Frame)
{
	TArray<FFrameNumber> Frames;
	Frames.Add(Frame);
	TArray<FTransform> Transforms = GetActorWorldTransforms(LevelSequence, Actor, Frames);
	if (Transforms.Num() == 1)
	{
		return Transforms[0];
	}
	return FTransform::Identity;
}

TArray<FTransform> UControlRigSequencerEditorLibrary::GetActorWorldTransforms(ULevelSequence* LevelSequence,AActor* Actor, const TArray<FFrameNumber>& Frames)
{
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);
	TArray<FTransform> OutWorldTransforms;
	if (WeakSequencer.IsValid() && Actor)
	{
		FActorForWorldTransforms Actors;
		Actors.Actor = Actor;
		MovieSceneToolHelpers::GetActorWorldTransforms(WeakSequencer.Pin().Get(), Actors, Frames, OutWorldTransforms); 
	}
	return OutWorldTransforms;

}

FTransform UControlRigSequencerEditorLibrary::GetSkeletalMeshComponentWorldTransform(ULevelSequence* LevelSequence,USkeletalMeshComponent* SkeletalMeshComponent, FFrameNumber Frame, FName SocketName)
{
	TArray<FFrameNumber> Frames;
	Frames.Add(Frame);
	TArray<FTransform> Transforms = GetSkeletalMeshComponentWorldTransforms(LevelSequence, SkeletalMeshComponent, Frames);
	if (Transforms.Num() == 1)
	{
		return Transforms[0];
	}
	return FTransform::Identity;
}

TArray<FTransform> UControlRigSequencerEditorLibrary::GetSkeletalMeshComponentWorldTransforms(ULevelSequence* LevelSequence,USkeletalMeshComponent* SkeletalMeshComponent, const TArray<FFrameNumber>& Frames, FName SocketName)
{
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);
	TArray<FTransform> OutWorldTransforms;
	if (WeakSequencer.IsValid() && SkeletalMeshComponent)
	{
		FActorForWorldTransforms Actors;
		AActor* Actor = SkeletalMeshComponent->GetTypedOuter<AActor>();
		if (Actor)
		{
			Actors.Actor = Actor;
			Actors.Component = SkeletalMeshComponent;
			Actors.SocketName = SocketName;
			MovieSceneToolHelpers::GetActorWorldTransforms(WeakSequencer.Pin().Get(), Actors, Frames, OutWorldTransforms);
		}
	}
	return OutWorldTransforms;
}

FTransform UControlRigSequencerEditorLibrary::GetControlRigWorldTransform(ULevelSequence* LevelSequence,UControlRig* ControlRig, FName ControlName, FFrameNumber Frame)
{
	TArray<FFrameNumber> Frames;
	Frames.Add(Frame);
	TArray<FTransform> Transforms = GetControlRigWorldTransforms(LevelSequence, ControlRig, ControlName, Frames);
	if (Transforms.Num() == 1)
	{
		return Transforms[0];
	}
	return FTransform::Identity;
}

TArray<FTransform> UControlRigSequencerEditorLibrary::GetControlRigWorldTransforms(ULevelSequence* LevelSequence,UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames)
{
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);
	TArray<FTransform> OutWorldTransforms;
	if (WeakSequencer.IsValid() && ControlRig)
	{
		if (TSharedPtr<IControlRigObjectBinding> ObjectBinding = ControlRig->GetObjectBinding())
		{
			USceneComponent* Component = Cast<USceneComponent>(ObjectBinding->GetBoundObject());
			if (Component)
			{
				AActor* Actor = Component->GetTypedOuter< AActor >();
				if (Actor)
				{
					ControlRig->Modify();
					TArray<FTransform> ControlRigParentWorldTransforms;
					FActorForWorldTransforms ControlRigActorSelection;
					ControlRigActorSelection.Actor = Actor;
					MovieSceneToolHelpers::GetActorWorldTransforms(WeakSequencer.Pin().Get(), ControlRigActorSelection, Frames, ControlRigParentWorldTransforms);
					FControlRigSnapper Snapper;
					Snapper.GetControlRigControlTransforms(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frames, ControlRigParentWorldTransforms, OutWorldTransforms);	
				}
			}
		}
	}
	return OutWorldTransforms;
}


static void LocalSetControlRigWorldTransforms(ULevelSequence* LevelSequence,UControlRig* ControlRig, FName ControlName, EControlRigSetKey SetKey, const TArray<FFrameNumber>& Frames, const TArray<FTransform>& WorldTransforms)
{
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);
	if (WeakSequencer.IsValid() && ControlRig)
	{
		if (TSharedPtr<IControlRigObjectBinding> ObjectBinding = ControlRig->GetObjectBinding())
		{
			USceneComponent* Component = Cast<USceneComponent>(ObjectBinding->GetBoundObject());
			if (Component)
			{
				AActor* Actor = Component->GetTypedOuter< AActor >();
				if (Actor)
				{
					UMovieScene* MovieScene = WeakSequencer.Pin().Get()->GetFocusedMovieSceneSequence()->GetMovieScene();
					MovieScene->Modify();
					FFrameRate TickResolution = MovieScene->GetTickResolution();
					FRigControlModifiedContext Context;
					Context.SetKey = SetKey;

					ControlRig->Modify();
					TArray<FTransform> ControlRigParentWorldTransforms;
					FActorForWorldTransforms ControlRigActorSelection;
					ControlRigActorSelection.Actor = Actor;
					MovieSceneToolHelpers::GetActorWorldTransforms(WeakSequencer.Pin().Get(), ControlRigActorSelection, Frames, ControlRigParentWorldTransforms);
					FControlRigSnapper Snapper;
					
					TArray<FFrameNumber> OneFrame;
					OneFrame.SetNum(1);
					TArray<FTransform> CurrentControlRigTransform, CurrentParentWorldTransform;
					CurrentControlRigTransform.SetNum(1);
					CurrentParentWorldTransform.SetNum(1);
					
					for (int32 Index = 0; Index < WorldTransforms.Num(); ++Index)
					{
						OneFrame[0] = Frames[Index];
						CurrentParentWorldTransform[0] = ControlRigParentWorldTransforms[Index];
						//this will evaluate at the current frame which we want
						Snapper.GetControlRigControlTransforms(WeakSequencer.Pin().Get(), ControlRig, ControlName, OneFrame, CurrentParentWorldTransform, CurrentControlRigTransform);
							
						const FFrameNumber& FrameNumber = Frames[Index];
						Context.LocalTime = TickResolution.AsSeconds(FFrameTime(FrameNumber));
						FTransform GlobalTransform = WorldTransforms[Index].GetRelativeTransform(ControlRigParentWorldTransforms[Index]);
						ControlRig->SetControlGlobalTransform(ControlName, GlobalTransform, true, Context);
					}
				}
			}
		}
	}
}

void UControlRigSequencerEditorLibrary::SetControlRigWorldTransform(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame, const FTransform& WorldTransform, bool bSetKey)
{
	EControlRigSetKey SetKey = bSetKey ? EControlRigSetKey::Always : EControlRigSetKey::DoNotCare;
	TArray<FFrameNumber> Frames;
	Frames.Add(Frame);
	TArray<FTransform> WorldTransforms;
	WorldTransforms.Add(WorldTransform);

	LocalSetControlRigWorldTransforms(LevelSequence, ControlRig, ControlName, EControlRigSetKey::Always, Frames, WorldTransforms);

}

void UControlRigSequencerEditorLibrary::SetControlRigWorldTransforms(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames, const TArray<FTransform>& WorldTransforms)
{
	LocalSetControlRigWorldTransforms(LevelSequence, ControlRig, ControlName, EControlRigSetKey::Always, Frames, WorldTransforms);
}

bool UControlRigSequencerEditorLibrary::BakeToControlRig(UWorld* World, ULevelSequence* LevelSequence, UClass* InClass, UAnimSeqExportOption* ExportOptions, bool bReduceKeys, float Tolerance,
	const FSequencerBindingProxy& Binding)
{
	UMovieScene* MovieScene = LevelSequence->GetMovieScene();
	if (Binding.Sequence != LevelSequence)
	{
		UE_LOG(LogControlRig, Error, TEXT("Baking: Binding.Sequence different"));
		return false;
	}
	//get level sequence if one exists...
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	ALevelSequenceActor* OutActor;
	FMovieSceneSequencePlaybackSettings Settings;
	FLevelSequenceCameraSettings CameraSettings;
	FMovieSceneSequenceIDRef Template = MovieSceneSequenceID::Root;
	FMovieSceneSequenceTransform RootToLocalTransform;
	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, LevelSequence, Settings, OutActor);
	Player->Initialize(LevelSequence, World->PersistentLevel, Settings, CameraSettings);
	Player->State.AssignSequence(MovieSceneSequenceID::Root, *LevelSequence, *Player);

	bool bResult = false;
	const FScopedTransaction Transaction(LOCTEXT("BakeToControlRig_Transaction", "Bake To Control Rig"));
	{
		FSpawnableRestoreState SpawnableRestoreState(MovieScene);

		if (SpawnableRestoreState.bWasChanged)
		{
			// Evaluate at the beginning of the subscene time to ensure that spawnables are created before export
			Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(UE::MovieScene::DiscreteInclusiveLower(MovieScene->GetPlaybackRange()).Value, EUpdatePositionMethod::Play));
		}
		TArrayView<TWeakObjectPtr<>>  Result = Player->FindBoundObjects(Binding.BindingID, Template);
	
		if (Result.Num() > 0 && Result[0].IsValid())
		{

			UObject* BoundObject = Result[0].Get();
			USkeleton* Skeleton = nullptr;
			USkeletalMeshComponent* SkeletalMeshComp = nullptr;
			AcquireSkeletonAndSkelMeshCompFromObject(BoundObject, &Skeleton, &SkeletalMeshComp);
			if (SkeletalMeshComp && SkeletalMeshComp->SkeletalMesh && SkeletalMeshComp->SkeletalMesh->GetSkeleton())
			{
				UAnimSequence* TempAnimSequence = NewObject<UAnimSequence>(GetTransientPackage(), NAME_None);
				TempAnimSequence->SetSkeleton(Skeleton);
				bResult = MovieSceneToolHelpers::ExportToAnimSequence(TempAnimSequence, ExportOptions, MovieScene, Player, SkeletalMeshComp, Template, RootToLocalTransform);
				if (bResult == false)
				{
					TempAnimSequence->MarkPendingKill();
					World->DestroyActor(OutActor);
					return false;
				}

				MovieScene->Modify();
				TArray<UMovieSceneTrack*> Tracks = MovieScene->FindTracks(UMovieSceneControlRigParameterTrack::StaticClass(), Binding.BindingID, NAME_None);
				UMovieSceneControlRigParameterTrack* Track = nullptr;
				for (UMovieSceneTrack* AnyOleTrack : Tracks)
				{
					UMovieSceneControlRigParameterTrack* ValidTrack = Cast<UMovieSceneControlRigParameterTrack>(AnyOleTrack);
					if (ValidTrack)
					{
						Track = ValidTrack;
						Track->Modify();
						for (UMovieSceneSection* Section : Track->GetAllSections())
						{
							Section->SetIsActive(false);
						}
					}
				}
				if(Track == nullptr)
				{
					Track = Cast<UMovieSceneControlRigParameterTrack>(MovieScene->AddTrack(UMovieSceneControlRigParameterTrack::StaticClass(), Binding.BindingID));
					{
						Track->Modify();
					}
				}

				if (Track)
				{
					FString ObjectName = InClass->GetName();
					ObjectName.RemoveFromEnd(TEXT("_C"));
					UControlRig* ControlRig = NewObject<UControlRig>(Track, InClass, FName(*ObjectName), RF_Transactional);
					if (InClass != UFKControlRig::StaticClass() && !ControlRig->SupportsEvent(FRigUnit_InverseExecution::EventName))
					{
						TempAnimSequence->MarkPendingKill();
						MovieScene->RemoveTrack(*Track);
						World->DestroyActor(OutActor);
						return false;
					}
					FControlRigEditMode* ControlRigEditMode = nullptr;
					if (WeakSequencer.IsValid())
					{
						ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
						if (!ControlRigEditMode)
						{
							GLevelEditorModeTools().ActivateMode(FControlRigEditMode::ModeName);
							ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));

						}
						else
						{
							UControlRig* OldControlRig = ControlRigEditMode->GetControlRig(false);
							if (OldControlRig)
							{
								WeakSequencer.Pin()->ObjectImplicitlyRemoved(OldControlRig);
							}
						}
					}

					ControlRig->Modify();
					ControlRig->SetObjectBinding(MakeShared<FControlRigObjectBinding>());
					ControlRig->GetObjectBinding()->BindToObject(SkeletalMeshComp);
					ControlRig->GetDataSourceRegistry()->RegisterDataSource(UControlRig::OwnerComponent, ControlRig->GetObjectBinding()->GetBoundObject());
					ControlRig->Initialize();
					ControlRig->Evaluate_AnyThread();

					bool bSequencerOwnsControlRig = true;
					UMovieSceneSection* NewSection = Track->CreateControlRigSection(0, ControlRig, bSequencerOwnsControlRig);
					UMovieSceneControlRigParameterSection* ParamSection = Cast<UMovieSceneControlRigParameterSection>(NewSection);

					//mz todo need to have multiple rigs with same class
					Track->SetTrackName(FName(*ObjectName));
					Track->SetDisplayName(FText::FromString(ObjectName));

					if (WeakSequencer.IsValid())
					{
						WeakSequencer.Pin()->EmptySelection();
						WeakSequencer.Pin()->SelectSection(NewSection);
						WeakSequencer.Pin()->ThrobSectionSelection();
						WeakSequencer.Pin()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
					}
					ParamSection->LoadAnimSequenceIntoThisSection(TempAnimSequence, MovieScene, Skeleton,
						bReduceKeys, Tolerance);

					//Turn Off Any Skeletal Animation Tracks
					UMovieSceneSkeletalAnimationTrack* SkelTrack = Cast<UMovieSceneSkeletalAnimationTrack>(MovieScene->FindTrack(UMovieSceneSkeletalAnimationTrack::StaticClass(), Binding.BindingID, NAME_None));
					if (SkelTrack)
					{
						SkelTrack->Modify();
						//can't just turn off the track so need to mute the sections
						const TArray<UMovieSceneSection*>& Sections = SkelTrack->GetAllSections();
						for (UMovieSceneSection* Section : Sections)
						{
							if (Section)
							{
								Section->TryModify();
								Section->SetIsActive(false);
							}
						}
					}
					//Finish Setup
					if (ControlRigEditMode)
					{
						ControlRigEditMode->SetObjects(ControlRig, nullptr, WeakSequencer.Pin());
					}

					TempAnimSequence->MarkPendingKill();
					if (WeakSequencer.IsValid())
					{
						WeakSequencer.Pin()->ObjectImplicitlyAdded(ControlRig);
						WeakSequencer.Pin()->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
					}
					bResult = true;
				}
			}
		}
	}

	Player->Stop();
	World->DestroyActor(OutActor);

	return bResult;
}

bool UControlRigSequencerEditorLibrary::LoadAnimSequenceIntoControlRigSection(UMovieSceneSection* MovieSceneSection, UAnimSequence* AnimSequence,  USkeleton* Skeleton,
	FFrameNumber InStartFrame,bool bKeyReduce, float Tolerance)
{
	if (MovieSceneSection == nullptr || AnimSequence == nullptr || Skeleton == nullptr)
	{
		return false;;
	}
	UMovieScene* MovieScene = MovieSceneSection->GetTypedOuter<UMovieScene>();
	if (MovieScene == nullptr)
	{
		return false;
	}
	if (UMovieSceneControlRigParameterSection* Section = Cast<UMovieSceneControlRigParameterSection>(MovieSceneSection))
	{
		return Section->LoadAnimSequenceIntoThisSection(AnimSequence, MovieScene, Skeleton, bKeyReduce, Tolerance, InStartFrame);
	}
	return false;
}

static bool LocalGetControlRigControlValues(IMovieScenePlayer* Player, UMovieSceneSequence* MovieSceneSequence, FMovieSceneSequenceIDRef Template, FMovieSceneSequenceTransform& RootToLocalTransform,
	UControlRig* ControlRig, const FName& ControlName,
	const TArray<FFrameNumber>& Frames, TArray<FRigControlValue>& OutValues)
{
	if (Player == nullptr || MovieSceneSequence == nullptr || ControlRig == nullptr)
	{
		return false;
	}
	if (ControlRig->FindControl(ControlName) == nullptr)
	{
		UE_LOG(LogControlRig, Error, TEXT("Can not find Control %s"), *(ControlName.ToString()));
		return false;
	}
	if (UMovieScene* MovieScene = MovieSceneSequence->GetMovieScene())
	{

		FFrameRate TickResolution = MovieScene->GetTickResolution();

		OutValues.SetNum(Frames.Num());
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			const FFrameNumber& FrameNumber = Frames[Index];
			FFrameTime GlobalTime(FrameNumber);

			FMovieSceneContext Context = FMovieSceneContext(FMovieSceneEvaluationRange(GlobalTime, TickResolution), Player->GetPlaybackStatus()).SetHasJumped(true);

			Player->GetEvaluationTemplate().Evaluate(Context, *Player);
			ControlRig->Evaluate_AnyThread();
			OutValues[Index] = ControlRig->GetControlValue(ControlName);
		}
	}
	return true;
}

static bool GetControlRigValues(ISequencer* Sequencer, UControlRig* ControlRig, const FName& ControlName,
	const TArray<FFrameNumber>& Frames,  TArray<FRigControlValue>& OutValues)
{
	if (Sequencer->GetFocusedMovieSceneSequence())
	{
		FMovieSceneSequenceIDRef Template = Sequencer->GetFocusedTemplateID();
		FMovieSceneSequenceTransform RootToLocalTransform;
		return LocalGetControlRigControlValues(Sequencer, Sequencer->GetFocusedMovieSceneSequence(), Template, RootToLocalTransform,
			ControlRig, ControlName, Frames, OutValues);

	}
	return false;
}

static bool GetControlRigValue(ISequencer* Sequencer, UControlRig* ControlRig, const FName& ControlName,
	const FFrameNumber Frame, FRigControlValue& OutValue)
{
	if (Sequencer->GetFocusedMovieSceneSequence())
	{
		TArray<FFrameNumber> Frames;
		Frames.Add(Frame);
		TArray<FRigControlValue> OutValues;
		FMovieSceneSequenceIDRef Template = Sequencer->GetFocusedTemplateID();
		FMovieSceneSequenceTransform RootToLocalTransform;
		bool bVal = LocalGetControlRigControlValues(Sequencer, Sequencer->GetFocusedMovieSceneSequence(), Template, RootToLocalTransform,
			ControlRig, ControlName, Frames, OutValues);
		if (bVal)
		{
			OutValue = OutValues[0];
		}
		return bVal;
	}
	return false;
}

static bool GetControlRigValues(UWorld* World, ULevelSequence* LevelSequence, UControlRig* ControlRig, const FName& ControlName,
	const TArray<FFrameNumber>& Frames, TArray<FRigControlValue>& OutValues)
{
	if (LevelSequence)
	{
		ALevelSequenceActor* OutActor;
		FMovieSceneSequencePlaybackSettings Settings;
		FLevelSequenceCameraSettings CameraSettings;
		FMovieSceneSequenceIDRef Template = MovieSceneSequenceID::Root;
		FMovieSceneSequenceTransform RootToLocalTransform;
		ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, LevelSequence, Settings, OutActor);
		Player->Initialize(LevelSequence, World->PersistentLevel, Settings, CameraSettings);
		Player->State.AssignSequence(MovieSceneSequenceID::Root, *LevelSequence, *Player);
		return LocalGetControlRigControlValues(Player, LevelSequence, Template, RootToLocalTransform,
			ControlRig, ControlName, Frames, OutValues);

	}
	return false;
}


float UControlRigSequencerEditorLibrary::GetLocalControlRigFloat(ULevelSequence* LevelSequence,UControlRig* ControlRig, FName ControlName, FFrameNumber Frame)
{
	float Value = 0.0f;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		FRigControlValue RigValue;
		if (GetControlRigValue(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frame, RigValue))
		{
			Value = RigValue.Get<float>();
		}
	}
	return Value;
}

TArray<float> UControlRigSequencerEditorLibrary::GetLocalControlRigFloats(ULevelSequence* LevelSequence,UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames)
{
	TArray<float> Values;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		TArray<FRigControlValue> RigValues;
		if (GetControlRigValues(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frames, RigValues))
		{
			Values.Reserve(RigValues.Num());
			for (const FRigControlValue& RigValue : RigValues)
			{
				float Value = RigValue.Get<float>();
				Values.Add(Value);
			}
		}
	}
	return Values;
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigFloat(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame, float Value, bool bSetKey)
{
	if (LevelSequence == nullptr || ControlRig == nullptr)
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = bSetKey ? EControlRigSetKey::Always : EControlRigSetKey::DoNotCare;
		Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
		ControlRig->SetControlValue<float>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
	}
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigFloats(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames, const TArray<float> Values)
{
	if (LevelSequence == nullptr || ControlRig == nullptr || (Frames.Num() != Values.Num()))
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = EControlRigSetKey::Always;
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			FFrameNumber Frame = Frames[Index];
			float  Value = Values[Index];
			Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
			ControlRig->SetControlValue<float>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
		}
	}
}


bool UControlRigSequencerEditorLibrary::GetLocalControlRigBool(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame)
{
	bool Value = true;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		FRigControlValue RigValue;
		if (GetControlRigValue(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frame, RigValue))
		{
			Value = RigValue.Get<bool>();
		}
	}
	return Value;
}

TArray<bool> UControlRigSequencerEditorLibrary::GetLocalControlRigBools(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames)
{
	TArray<bool> Values;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		TArray<FRigControlValue> RigValues;
		if (GetControlRigValues(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frames, RigValues))
		{
			Values.Reserve(RigValues.Num());
			for (const FRigControlValue& RigValue : RigValues)
			{
				bool Value = RigValue.Get<bool>();
				Values.Add(Value);
			}
		}
	}
	return Values;
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigBool(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame, bool Value, bool bSetKey)
{
	if (LevelSequence == nullptr || ControlRig == nullptr)
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = bSetKey ? EControlRigSetKey::Always : EControlRigSetKey::DoNotCare;
		Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
		ControlRig->SetControlValue<bool>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
	}
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigBools(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames, const TArray<bool> Values)
{
	if (LevelSequence == nullptr || ControlRig == nullptr || (Frames.Num() != Values.Num()))
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = EControlRigSetKey::Always;
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			FFrameNumber Frame = Frames[Index];
			bool Value = Values[Index];
			Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
			ControlRig->SetControlValue<bool>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
		}
	}
}

int32 UControlRigSequencerEditorLibrary::GetLocalControlRigInt(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame)
{
	int32 Value = 0;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		FRigControlValue RigValue;
		if (GetControlRigValue(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frame, RigValue))
		{
			Value = RigValue.Get<int32>();
		}
	}
	return Value;
}

TArray<int32> UControlRigSequencerEditorLibrary::GetLocalControlRigInts(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames)
{
	TArray<int32> Values;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		TArray<FRigControlValue> RigValues;
		if (GetControlRigValues(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frames, RigValues))
		{
			Values.Reserve(RigValues.Num());
			for (const FRigControlValue& RigValue : RigValues)
			{
				int32 Value = RigValue.Get<int32>();
				Values.Add(Value);
			}
		}
	}
	return Values;
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigInt(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame, int32 Value, bool bSetKey)
{
	if (LevelSequence == nullptr || ControlRig == nullptr)
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = bSetKey ? EControlRigSetKey::Always : EControlRigSetKey::DoNotCare;
		Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
		ControlRig->SetControlValue<int32>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
	}
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigInts(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames, const TArray<int32> Values)
{
	if (LevelSequence == nullptr || ControlRig == nullptr || (Frames.Num() != Values.Num()))
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = EControlRigSetKey::Always;
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			FFrameNumber Frame = Frames[Index];
			int32 Value = Values[Index];
			Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
			ControlRig->SetControlValue<int32>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
		}
	}
}


FVector2D UControlRigSequencerEditorLibrary::GetLocalControlRigVector2D(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame)
{
	FVector2D Value;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		FRigControlValue RigValue;
		if (GetControlRigValue(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frame, RigValue))
		{
			Value = RigValue.Get<FVector2D>();
		}
	}
	return Value;
}

TArray<FVector2D> UControlRigSequencerEditorLibrary::GetLocalControlRigVector2Ds(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames)
{
	TArray<FVector2D> Values;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		TArray<FRigControlValue> RigValues;
		if (GetControlRigValues(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frames, RigValues))
		{
			Values.Reserve(RigValues.Num());
			for (const FRigControlValue& RigValue : RigValues)
			{
				FVector2D Value = RigValue.Get<FVector2D>();
				Values.Add(Value);
			}
		}
	}
	return Values;

}

void UControlRigSequencerEditorLibrary::SetLocalControlRigVector2D(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame, FVector2D Value, bool bSetKey)
{
	if (LevelSequence == nullptr || ControlRig == nullptr)
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = bSetKey ? EControlRigSetKey::Always : EControlRigSetKey::DoNotCare;
		Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
		ControlRig->SetControlValue<FVector2D>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
	}
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigVector2Ds(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames, const TArray<FVector2D> Values)
{
	if (LevelSequence == nullptr || ControlRig == nullptr || (Frames.Num() != Values.Num()))
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = EControlRigSetKey::Always;
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			FFrameNumber Frame = Frames[Index];
			FVector2D Value = Values[Index];
			Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
			ControlRig->SetControlValue<FVector2D>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
		}
	}
}


FVector UControlRigSequencerEditorLibrary::GetLocalControlRigPosition(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame)
{
	FVector Value;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		FRigControlValue RigValue;
		if (GetControlRigValue(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frame, RigValue))
		{
			Value = RigValue.Get<FVector>();
		}
	}
	return Value;
}

TArray<FVector> UControlRigSequencerEditorLibrary::GetLocalControlRigPositions(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames)
{
	TArray<FVector> Values;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		TArray<FRigControlValue> RigValues;
		if (GetControlRigValues(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frames, RigValues))
		{
			Values.Reserve(RigValues.Num());
			for (const FRigControlValue& RigValue : RigValues)
			{
				FVector Value = RigValue.Get<FVector>();
				Values.Add(Value);
			}
		}
	}
	return Values;
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigPosition(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame, FVector Value, bool bSetKey)
{
	if (LevelSequence == nullptr || ControlRig == nullptr)
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = bSetKey ? EControlRigSetKey::Always : EControlRigSetKey::DoNotCare;
		Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
		ControlRig->SetControlValue<FVector>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
	}
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigPositions(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames, const TArray<FVector> Values)
{
	if (LevelSequence == nullptr || ControlRig == nullptr || (Frames.Num() != Values.Num()))
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = EControlRigSetKey::Always;
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			FFrameNumber Frame = Frames[Index];
			FVector Value = Values[Index];
			Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
			ControlRig->SetControlValue<FVector>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
		}
	}
}


FRotator UControlRigSequencerEditorLibrary::GetLocalControlRigRotator(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame)
{
	FRotator Value;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		FRigControlValue RigValue;
		if (GetControlRigValue(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frame, RigValue))
		{
			Value = RigValue.Get<FRotator>();
		}
	}
	return Value;
}

TArray<FRotator> UControlRigSequencerEditorLibrary::GetLocalControlRigRotators(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames)
{
	TArray<FRotator> Values;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		TArray<FRigControlValue> RigValues;
		if (GetControlRigValues(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frames, RigValues))
		{
			Values.Reserve(RigValues.Num());
			for (const FRigControlValue& RigValue : RigValues)
			{
				FRotator Value = RigValue.Get<FRotator>();
				Values.Add(Value);
			}
		}
	}
	return Values;
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigRotator(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame, FRotator Value, bool bSetKey)
{
	if (LevelSequence == nullptr || ControlRig == nullptr)
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = bSetKey ? EControlRigSetKey::Always : EControlRigSetKey::DoNotCare;
		Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
		ControlRig->SetControlValue<FRotator>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
	}
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigRotators(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames, const TArray<FRotator> Values)
{
	if (LevelSequence == nullptr || ControlRig == nullptr || (Frames.Num() != Values.Num()))
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = EControlRigSetKey::Always;
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			FFrameNumber Frame = Frames[Index];
			FRotator Value = Values[Index];
			Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
			ControlRig->SetControlValue<FRotator>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
		}
	}
}


FVector UControlRigSequencerEditorLibrary::GetLocalControlRigScale(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame)
{
	FVector Value;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		FRigControlValue RigValue;
		if (GetControlRigValue(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frame, RigValue))
		{
			Value = RigValue.Get<FVector>();
		}
	}
	return Value;
}

TArray<FVector>UControlRigSequencerEditorLibrary::GetLocalControlRigScales(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames)
{
	TArray<FVector> Values;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		TArray<FRigControlValue> RigValues;
		if (GetControlRigValues(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frames, RigValues))
		{
			Values.Reserve(RigValues.Num());
			for (const FRigControlValue& RigValue : RigValues)
			{
				FVector Value = RigValue.Get<FVector>();
				Values.Add(Value);
			}
		}
	}
	return Values;
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigScale(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame, FVector Value, bool bSetKey)
{
	if (LevelSequence == nullptr || ControlRig == nullptr)
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = bSetKey ? EControlRigSetKey::Always : EControlRigSetKey::DoNotCare;
		Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
		ControlRig->SetControlValue<FVector>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
	}
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigScales(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames, const TArray<FVector> Values)
{
	if (LevelSequence == nullptr || ControlRig == nullptr || (Frames.Num() != Values.Num()))
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = EControlRigSetKey::Always;
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			FFrameNumber Frame = Frames[Index];
			FVector Value = Values[Index];
			Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
			ControlRig->SetControlValue<FVector>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
		}
	}
}


FEulerTransform UControlRigSequencerEditorLibrary::GetLocalControlRigEulerTransform(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame)
{
	FEulerTransform Value = FEulerTransform::Identity;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		FRigControlValue RigValue;
		if (GetControlRigValue(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frame, RigValue))
		{
			Value = RigValue.Get<FEulerTransform>();
		}
	}
	return Value;
}

TArray<FEulerTransform> UControlRigSequencerEditorLibrary::GetLocalControlRigEulerTransforms(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames)
{
	TArray<FEulerTransform> Values;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		TArray<FRigControlValue> RigValues;
		if (GetControlRigValues(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frames, RigValues))
		{
			Values.Reserve(RigValues.Num());
			for (const FRigControlValue& RigValue : RigValues)
			{
				FEulerTransform Value = RigValue.Get<FEulerTransform>();
				Values.Add(Value);
			}
		}
	}
	return Values;
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigEulerTransform(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame, FEulerTransform Value, bool bSetKey)
{
	if (LevelSequence == nullptr || ControlRig == nullptr)
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = bSetKey ? EControlRigSetKey::Always : EControlRigSetKey::DoNotCare;
		Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
		ControlRig->SetControlValue<FEulerTransform>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
	}
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigEulerTransforms(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames, const TArray<FEulerTransform> Values)
{
	if (LevelSequence == nullptr || ControlRig == nullptr || (Frames.Num() != Values.Num()))
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = EControlRigSetKey::Always;
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			FFrameNumber Frame = Frames[Index];
			FEulerTransform Value = Values[Index];
			Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
			ControlRig->SetControlValue<FEulerTransform>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
		}
	}
}


FTransformNoScale UControlRigSequencerEditorLibrary::GetLocalControlRigTransformNoScale(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame)
{
	FTransformNoScale Value = FTransformNoScale::Identity;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		FRigControlValue RigValue;
		if (GetControlRigValue(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frame, RigValue))
		{
			Value = RigValue.Get<FTransformNoScale>();
		}
	}
	return Value;
}

TArray<FTransformNoScale> UControlRigSequencerEditorLibrary::GetLocalControlRigTransformNoScales(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames)
{
	TArray<FTransformNoScale> Values;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		TArray<FRigControlValue> RigValues;
		if (GetControlRigValues(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frames, RigValues))
		{
			Values.Reserve(RigValues.Num());
			for (const FRigControlValue& RigValue : RigValues)
			{
				FTransformNoScale Value = RigValue.Get<FTransformNoScale>();
				Values.Add(Value);
			}
		}
	}
	return Values;
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigTransformNoScale(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame, FTransformNoScale Value, bool bSetKey)
{
	if (LevelSequence == nullptr || ControlRig == nullptr)
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = bSetKey ? EControlRigSetKey::Always : EControlRigSetKey::DoNotCare;
		Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
		ControlRig->SetControlValue<FTransformNoScale>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
	}
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigTransformNoScales(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames, const TArray<FTransformNoScale> Values)
{
	if (LevelSequence == nullptr || ControlRig == nullptr || (Frames.Num() != Values.Num()))
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = EControlRigSetKey::Always;
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			FFrameNumber Frame = Frames[Index];
			FTransformNoScale Value = Values[Index];
			Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
			ControlRig->SetControlValue<FTransformNoScale>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
		}
	}
}


FTransform UControlRigSequencerEditorLibrary::GetLocalControlRigTransform(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame)
{
	FTransform Value = FTransform::Identity;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);
	if (WeakSequencer.IsValid() && ControlRig)
	{
		FRigControlValue RigValue;
		if (GetControlRigValue(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frame, RigValue))
		{
			Value = RigValue.Get<FTransform>();
		}
	}
	return Value;
}

TArray<FTransform> UControlRigSequencerEditorLibrary::GetLocalControlRigTransforms(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames)
{
	TArray<FTransform> Values;
	TWeakPtr<ISequencer> WeakSequencer = GetSequencerFromAsset(LevelSequence);

	if (WeakSequencer.IsValid() && ControlRig)
	{
		TArray<FRigControlValue> RigValues;
		if (GetControlRigValues(WeakSequencer.Pin().Get(), ControlRig, ControlName, Frames, RigValues))
		{
			Values.Reserve(RigValues.Num());
			for (const FRigControlValue& RigValue : RigValues)
			{
				FTransform Value = RigValue.Get<FTransform>();
				Values.Add(Value);
			}
		}
	}
	return Values;
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigTransform(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, FFrameNumber Frame, FTransform Value, bool bSetKey)
{
	if (LevelSequence == nullptr || ControlRig == nullptr)
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = bSetKey ? EControlRigSetKey::Always : EControlRigSetKey::DoNotCare;
		Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
		ControlRig->SetControlValue<FTransform>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
	}
}

void UControlRigSequencerEditorLibrary::SetLocalControlRigTransforms(ULevelSequence* LevelSequence, UControlRig* ControlRig, FName ControlName, const TArray<FFrameNumber>& Frames, const TArray<FTransform> Values)
{
	if (LevelSequence == nullptr || ControlRig == nullptr || (Frames.Num() != Values.Num()))
	{
		return;
	}
	if (UMovieScene* MovieScene = LevelSequence->GetMovieScene())
	{
		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FRigControlModifiedContext Context;
		Context.SetKey = EControlRigSetKey::Always;
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			FFrameNumber Frame = Frames[Index];
			FTransform Value = Values[Index];
			Context.LocalTime = TickResolution.AsSeconds(FFrameTime(Frame));
			ControlRig->SetControlValue<FTransform>(ControlName, Value, true, FRigControlModifiedContext(EControlRigSetKey::Never, false));
		}
	}
}


#undef LOCTEXT_NAMESPACE
