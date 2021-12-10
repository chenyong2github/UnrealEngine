// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSequenceEditorSubsystem.h"

#include "Compilation/MovieSceneCompiledDataManager.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "LevelSequenceEditorCommands.h"
#include "MovieScene.h"
#include "MovieSceneCommonHelpers.h"
#include "MovieSceneSequence.h"
#include "MovieSceneTimeHelpers.h"
#include "SequencerSettings.h"
#include "SequencerUtilities.h"
#include "SequencerBindingProxy.h"
#include "SequenceTimeUnit.h"
#include "Sections/MovieScene3DConstraintSection.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieScene3DConstraintTrack.h"
#include "Tracks/MovieSceneCameraAnimTrack.h"
#include "Tracks/MovieSceneCameraShakeTrack.h"

#include "ActorTreeItem.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"

DEFINE_LOG_CATEGORY(LogLevelSequenceEditor);

#define LOCTEXT_NAMESPACE "LevelSequenceEditor"

void ULevelSequenceEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	UE_LOG(LogLevelSequenceEditor, Log, TEXT("LevelSequenceEditor subsystem initialized."));

	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	OnSequencerCreatedHandle = SequencerModule.RegisterOnSequencerCreated(FOnSequencerCreated::FDelegate::CreateUObject(this, &ULevelSequenceEditorSubsystem::OnSequencerCreated));

	/* Commands for this subsystem */
	CommandList = MakeShareable(new FUICommandList);

	CommandList->MapAction(FLevelSequenceEditorCommands::Get().BakeTransform,
		FExecuteAction::CreateUObject(this, &ULevelSequenceEditorSubsystem::BakeTransformInternal)
	);

	CommandList->MapAction(FLevelSequenceEditorCommands::Get().FixActorReferences,
		FExecuteAction::CreateUObject(this, &ULevelSequenceEditorSubsystem::FixActorReferences)
	);

	BakeTransformMenuExtender = MakeShareable(new FExtender);
	BakeTransformMenuExtender->AddMenuExtension("Transform", EExtensionHook::After, CommandList, FMenuExtensionDelegate::CreateStatic([](FMenuBuilder& MenuBuilder) {
		MenuBuilder.AddMenuEntry(FLevelSequenceEditorCommands::Get().BakeTransform);
		}));

	SequencerModule.GetActionsMenuExtensibilityManager()->AddExtender(BakeTransformMenuExtender);

	FixActorReferencesMenuExtender = MakeShareable(new FExtender);
	FixActorReferencesMenuExtender->AddMenuExtension("Bindings", EExtensionHook::First, CommandList, FMenuExtensionDelegate::CreateStatic([](FMenuBuilder& MenuBuilder) {
		MenuBuilder.AddMenuEntry(FLevelSequenceEditorCommands::Get().FixActorReferences);
		}));

	SequencerModule.GetActionsMenuExtensibilityManager()->AddExtender(FixActorReferencesMenuExtender);
}

void ULevelSequenceEditorSubsystem::Deinitialize()
{
	UE_LOG(LogLevelSequenceEditor, Log, TEXT("LevelSequenceEditor subsystem deinitialized."));

	ISequencerModule* SequencerModulePtr = FModuleManager::Get().GetModulePtr<ISequencerModule>("Sequencer");
	if (SequencerModulePtr)
	{
		SequencerModulePtr->UnregisterOnSequencerCreated(OnSequencerCreatedHandle);
	}

}

void ULevelSequenceEditorSubsystem::OnSequencerCreated(TSharedRef<ISequencer> InSequencer)
{
	UE_LOG(LogLevelSequenceEditor, VeryVerbose, TEXT("ULevelSequenceEditorSubsystem::OnSequencerCreated"));

	Sequencers.Add(TWeakPtr<ISequencer>(InSequencer));
}

TSharedPtr<ISequencer> ULevelSequenceEditorSubsystem::GetActiveSequencer()
{
	for (TWeakPtr<ISequencer> Ptr : Sequencers)
	{
		if (Ptr.IsValid())
		{
			return Ptr.Pin();
		}
	}

	return nullptr;
}

void ULevelSequenceEditorSubsystem::BakeTransformInternal()
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UMovieScene* FocusedMovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!FocusedMovieScene)
	{
		return;
	}

	FFrameRate TickResolution = FocusedMovieScene->GetTickResolution();
	FFrameRate DisplayRate = FocusedMovieScene->GetDisplayRate();

	FFrameNumber InFrame = UE::MovieScene::DiscreteInclusiveLower(FocusedMovieScene->GetPlaybackRange());
	FFrameNumber OutFrame = UE::MovieScene::DiscreteExclusiveUpper(FocusedMovieScene->GetPlaybackRange());

	TArray<FGuid> ObjectBindings;
	Sequencer->GetSelectedObjects(ObjectBindings);

	TArray<FSequencerBindingProxy> BindingProxies;
	for (FGuid Guid : ObjectBindings)
	{
		BindingProxies.Add(FSequencerBindingProxy(Guid, Sequencer->GetFocusedMovieSceneSequence()));
	}

	FMovieSceneScriptingParams Params;
	Params.TimeUnit = ESequenceTimeUnit::TickResolution;
	BakeTransform(BindingProxies, FFrameTime(InFrame), FFrameTime(OutFrame), ConvertFrameTime(1, DisplayRate, TickResolution), Params);
}

void ULevelSequenceEditorSubsystem::BakeTransform(const TArray<FSequencerBindingProxy>& ObjectBindings, const FFrameTime& BakeInTime, const FFrameTime& BakeOutTime, const FFrameTime& BakeInterval, const FMovieSceneScriptingParams& Params)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UMovieScene* FocusedMovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!FocusedMovieScene)
	{
		return;
	}

	if (FocusedMovieScene->IsReadOnly())
	{
		FSequencerUtilities::ShowReadOnlyError();
		return;
	}

	if (ObjectBindings.Num() == 0)
	{
		return;
	}

	FScopedTransaction BakeTransform(LOCTEXT("BakeTransform", "Bake Transform"));

	FocusedMovieScene->Modify();

	FQualifiedFrameTime ResetTime = Sequencer->GetLocalTime();

	FFrameRate TickResolution = FocusedMovieScene->GetTickResolution();
	FFrameRate DisplayRate = FocusedMovieScene->GetDisplayRate();

	FFrameTime InFrame = BakeInTime;
	FFrameTime OutFrame = BakeOutTime;
	FFrameTime Interval = BakeInterval;

	if (Params.TimeUnit == ESequenceTimeUnit::DisplayRate)
	{
		InFrame = ConvertFrameTime(BakeInTime, DisplayRate, TickResolution);
		OutFrame = ConvertFrameTime(BakeOutTime, DisplayRate, TickResolution);
		Interval = ConvertFrameTime(BakeInterval, DisplayRate, TickResolution);
	}

	struct FBakeData
	{
		TArray<FVector> Locations;
		TArray<FRotator> Rotations;
		TArray<FVector> Scales;
		TArray<FFrameNumber> KeyTimes;
	};

	TMap<FGuid, FBakeData> BakeDataMap;
	for (const FSequencerBindingProxy& ObjectBinding : ObjectBindings)
	{
		BakeDataMap.Add(ObjectBinding.BindingID);
	}

	FMovieSceneSequenceTransform RootToLocalTransform = Sequencer->GetFocusedMovieSceneSequenceTransform();
	
	for (FFrameTime EvalTime = InFrame; EvalTime <= OutFrame; EvalTime += Interval)
	{
		FFrameNumber KeyTime = FFrameRate::Snap(EvalTime, TickResolution, DisplayRate).FloorToFrame();
		FMovieSceneEvaluationRange Range(KeyTime * RootToLocalTransform.InverseLinearOnly(), TickResolution);

		Sequencer->SetLocalTimeDirectly(Range.GetTime());

		for (const FSequencerBindingProxy& ObjectBinding : ObjectBindings)
		{
			FGuid Guid = ObjectBinding.BindingID;

			for (TWeakObjectPtr<> RuntimeObject : Sequencer->FindObjectsInCurrentSequence(Guid) )
			{
				AActor* Actor = Cast<AActor>(RuntimeObject.Get());
				if (!Actor)
				{
					UActorComponent* ActorComponent = Cast<UActorComponent>(RuntimeObject.Get());
					if (ActorComponent)
					{
						Actor = ActorComponent->GetOwner();
					}
				}

				if (!Actor)
				{
					continue;
				}

				UCameraComponent* CameraComponent = MovieSceneHelpers::CameraComponentFromRuntimeObject(RuntimeObject.Get());

				// Cache transforms
				USceneComponent* Parent = nullptr;
				if (CameraComponent)
				{
					Parent = CameraComponent->GetAttachParent();
				} 
				else if (Actor->GetRootComponent())
				{
					Parent = Actor->GetRootComponent()->GetAttachParent();
				}
				
				// The CameraRig_rail updates the spline position tick, so it needs to be ticked manually while baking the frames
				while (Parent && Parent->GetOwner())
				{
					Parent->GetOwner()->Tick(0.03f);
					if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(Parent))
					{
						SkeletalMeshComponent->TickAnimation(0.f, false);

						SkeletalMeshComponent->RefreshBoneTransforms();
						SkeletalMeshComponent->RefreshSlaveComponents();
						SkeletalMeshComponent->UpdateComponentToWorld();
						SkeletalMeshComponent->FinalizeBoneTransform();
						SkeletalMeshComponent->MarkRenderTransformDirty();
						SkeletalMeshComponent->MarkRenderDynamicDataDirty();
					}
					Parent = Parent->GetAttachParent();
				}

				if (CameraComponent)
				{
					FTransform AdditiveOffset;
					float AdditiveFOVOffset;
					CameraComponent->GetAdditiveOffset(AdditiveOffset, AdditiveFOVOffset);

					FTransform Transform(Actor->GetActorRotation(), Actor->GetActorLocation());
					FTransform TransformWithAdditiveOffset = AdditiveOffset * Transform;
					FVector LocalTranslation = TransformWithAdditiveOffset.GetTranslation();
					FRotator LocalRotation = TransformWithAdditiveOffset.GetRotation().Rotator();

					BakeDataMap[Guid].Locations.Add(LocalTranslation);
					BakeDataMap[Guid].Rotations.Add(LocalRotation);
					BakeDataMap[Guid].Scales.Add(FVector::OneVector);
				}
				else
				{
					BakeDataMap[Guid].Locations.Add(Actor->GetActorLocation());
					BakeDataMap[Guid].Rotations.Add(Actor->GetActorRotation());
					BakeDataMap[Guid].Scales.Add(Actor->GetActorScale());
				}

				BakeDataMap[Guid].KeyTimes.Add(KeyTime);
			}
		}
	}

	const bool bDisableSectionsAfterBaking = Sequencer->GetSequencerSettings()->GetDisableSectionsAfterBaking();

	for (TPair<FGuid, FBakeData>& BakeData : BakeDataMap)
	{
		FGuid Guid = BakeData.Key;

		// Disable or delete any constraint (attach/path) tracks
		AActor* AttachParentActor = nullptr;
		for (UMovieSceneTrack* Track : FocusedMovieScene->FindTracks(UMovieScene3DConstraintTrack::StaticClass(), Guid))
		{
			if (UMovieScene3DConstraintTrack* ConstraintTrack = Cast<UMovieScene3DConstraintTrack>(Track))
			{
				for (UMovieSceneSection* ConstraintSection : ConstraintTrack->GetAllSections())
				{
					FMovieSceneObjectBindingID ConstraintBindingID = (Cast<UMovieScene3DConstraintSection>(ConstraintSection))->GetConstraintBindingID();
					for (TWeakObjectPtr<> ParentObject : ConstraintBindingID.ResolveBoundObjects(Sequencer->GetFocusedTemplateID(), *Sequencer))
					{
						AttachParentActor = Cast<AActor>(ParentObject.Get());
						break;
					}
				}

				if (bDisableSectionsAfterBaking)
				{
					for (UMovieSceneSection* ConstraintSection : ConstraintTrack->GetAllSections())
					{
						ConstraintSection->Modify();
						ConstraintSection->SetIsActive(false);
					}
				}
				else
				{
					FocusedMovieScene->RemoveTrack(*ConstraintTrack);
				}
			}
		}

		// Disable or delete any transform tracks
		for (UMovieSceneTrack* Track : FocusedMovieScene->FindTracks(UMovieScene3DTransformTrack::StaticClass(), Guid))
		{
			if (UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(Track))
			{
				if (bDisableSectionsAfterBaking)
				{
					for (UMovieSceneSection* TransformSection : TransformTrack->GetAllSections())
					{
						TransformSection->Modify();
						TransformSection->SetIsActive(false);
					}
				}
				else
				{
					FocusedMovieScene->RemoveTrack(*TransformTrack);
				}
			}
		}

		// Disable or delete any camera anim tracks
		for (UMovieSceneTrack* Track : FocusedMovieScene->FindTracks(UMovieSceneCameraAnimTrack::StaticClass(), Guid))
		{
			if (UMovieSceneCameraAnimTrack* CameraAnimTrack = Cast<UMovieSceneCameraAnimTrack>(Track))
			{
				if (bDisableSectionsAfterBaking)
				{
					for (UMovieSceneSection* CameraAnimSection : CameraAnimTrack->GetAllSections())
					{
						CameraAnimSection->Modify();
						CameraAnimSection->SetIsActive(false);
					}
				}
				else
				{
					FocusedMovieScene->RemoveTrack(*CameraAnimTrack);
				}
			}
		}

		// Disable or delete any camera shake tracks
		for (UMovieSceneTrack* Track : FocusedMovieScene->FindTracks(UMovieSceneCameraShakeTrack::StaticClass(), Guid))
		{
			if (UMovieSceneCameraShakeTrack* CameraShakeTrack = Cast<UMovieSceneCameraShakeTrack>(Track))
			{
				if (bDisableSectionsAfterBaking)
				{
					for (UMovieSceneSection* CameraShakeSection : CameraShakeTrack->GetAllSections())
					{
						CameraShakeSection->Modify();
						CameraShakeSection->SetIsActive(false);
					}
				}
				else
				{
					FocusedMovieScene->RemoveTrack(*CameraShakeTrack);
				}
			}
		}

		// Reset position
		Sequencer->SetLocalTimeDirectly(ResetTime.Time);

		FVector DefaultLocation = FVector::ZeroVector;
		FVector DefaultRotation = FVector::ZeroVector;
		FVector DefaultScale = FVector::OneVector;

		for (TWeakObjectPtr<> RuntimeObject : Sequencer->FindObjectsInCurrentSequence(Guid))
		{
			AActor* Actor = Cast<AActor>(RuntimeObject.Get());
			if (!Actor)
			{
				UActorComponent* ActorComponent = Cast<UActorComponent>(RuntimeObject.Get());
				if (ActorComponent)
				{
					Actor = ActorComponent->GetOwner();
				}
			}

			if (!Actor)
			{
				continue;
			}

			DefaultLocation = Actor->GetActorLocation();
			DefaultRotation = Actor->GetActorRotation().Euler();
			DefaultScale = Actor->GetActorScale();

			// Always detach from any existing parent
			Actor->DetachFromActor(FDetachmentTransformRules::KeepRelativeTransform);
		}
			
		// Create new transform track and section
		UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(FocusedMovieScene->AddTrack(UMovieScene3DTransformTrack::StaticClass(), Guid));

		if (TransformTrack)
		{
			UMovieScene3DTransformSection* TransformSection = CastChecked<UMovieScene3DTransformSection>(TransformTrack->CreateNewSection());
			TransformTrack->AddSection(*TransformSection);

			TransformSection->SetRange(TRange<FFrameNumber>::All());

			TArrayView<FMovieSceneDoubleChannel*> DoubleChannels = TransformSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
			DoubleChannels[0]->SetDefault(DefaultLocation.X);
			DoubleChannels[1]->SetDefault(DefaultLocation.Y);
			DoubleChannels[2]->SetDefault(DefaultLocation.Z);
			DoubleChannels[3]->SetDefault(DefaultRotation.X);
			DoubleChannels[4]->SetDefault(DefaultRotation.Y);
			DoubleChannels[5]->SetDefault(DefaultRotation.Z);
			DoubleChannels[6]->SetDefault(DefaultScale.X);
			DoubleChannels[7]->SetDefault(DefaultScale.Y);
			DoubleChannels[8]->SetDefault(DefaultScale.Z);

			TArray<FVector> LocalTranslations, LocalRotations, LocalScales;
			LocalTranslations.SetNum(BakeData.Value.KeyTimes.Num());
			LocalRotations.SetNum(BakeData.Value.KeyTimes.Num());
			LocalScales.SetNum(BakeData.Value.KeyTimes.Num());

			for (int32 Counter = 0; Counter < BakeData.Value.KeyTimes.Num(); ++Counter)
			{
				FTransform LocalTransform(BakeData.Value.Rotations[Counter], BakeData.Value.Locations[Counter], BakeData.Value.Scales[Counter]);
				LocalTranslations[Counter] = LocalTransform.GetTranslation();
				LocalRotations[Counter] = LocalTransform.GetRotation().Euler();
				LocalScales[Counter] = LocalTransform.GetScale3D();
			}

			// Euler filter
			for (int32 Counter = 0; Counter < LocalRotations.Num() - 1; ++Counter)
			{
				FMath::WindRelativeAnglesDegrees(LocalRotations[Counter].X, LocalRotations[Counter + 1].X);
				FMath::WindRelativeAnglesDegrees(LocalRotations[Counter].Y, LocalRotations[Counter + 1].Y);
				FMath::WindRelativeAnglesDegrees(LocalRotations[Counter].Z, LocalRotations[Counter + 1].Z);							
			}
				
			for (int32 Counter = 0; Counter < BakeData.Value.KeyTimes.Num(); ++Counter)
			{
				FFrameNumber KeyTime = BakeData.Value.KeyTimes[Counter];
				DoubleChannels[0]->AddLinearKey(KeyTime, LocalTranslations[Counter].X);
				DoubleChannels[1]->AddLinearKey(KeyTime, LocalTranslations[Counter].Y);
				DoubleChannels[2]->AddLinearKey(KeyTime, LocalTranslations[Counter].Z);
				DoubleChannels[3]->AddLinearKey(KeyTime, LocalRotations[Counter].X);
				DoubleChannels[4]->AddLinearKey(KeyTime, LocalRotations[Counter].Y);
				DoubleChannels[5]->AddLinearKey(KeyTime, LocalRotations[Counter].Z);
				DoubleChannels[6]->AddLinearKey(KeyTime, LocalScales[Counter].X);
				DoubleChannels[7]->AddLinearKey(KeyTime, LocalScales[Counter].Y);
				DoubleChannels[8]->AddLinearKey(KeyTime, LocalScales[Counter].Z);
			}
		}
	}
	
	Sequencer->NotifyMovieSceneDataChanged( EMovieSceneDataChangeType::MovieSceneStructureItemsChanged );
}

void ULevelSequenceEditorSubsystem::FixActorReferences()
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UWorld* PlaybackContext = Cast<UWorld>(Sequencer->GetPlaybackContext());

	if (!PlaybackContext)
	{
		return;
	}

	UMovieScene* FocusedMovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!FocusedMovieScene)
	{
		return;
	}

	if (FocusedMovieScene->IsReadOnly())
	{
		FSequencerUtilities::ShowReadOnlyError();
		return;
	}

	FScopedTransaction FixActorReferencesTransaction(LOCTEXT("FixActorReferences", "Fix Actor References"));

	TMap<FString, AActor*> ActorNameToActorMap;

	for (TActorIterator<AActor> ActorItr(PlaybackContext); ActorItr; ++ActorItr)
	{
		// Same as with the Object Iterator, access the subclass instance with the * or -> operators.
		AActor* Actor = *ActorItr;
		ActorNameToActorMap.Add(Actor->GetActorLabel(), Actor);
	}

	// Cache the possessables to fix up first since the bindings will change as the fix ups happen.
	TArray<FMovieScenePossessable> ActorsPossessablesToFix;
	for (int32 i = 0; i < FocusedMovieScene->GetPossessableCount(); i++)
	{
		FMovieScenePossessable& Possessable = FocusedMovieScene->GetPossessable(i);
		// Possessables with parents are components so ignore them.
		if (Possessable.GetParent().IsValid() == false)
		{
			if (Sequencer->FindBoundObjects(Possessable.GetGuid(), Sequencer->GetFocusedTemplateID()).Num() == 0)
			{
				ActorsPossessablesToFix.Add(Possessable);
			}
		}
	}

	// For the possessables to fix, look up the actors by name and reassign them if found.
	TMap<FGuid, FGuid> OldGuidToNewGuidMap;
	for (const FMovieScenePossessable& ActorPossessableToFix : ActorsPossessablesToFix)
	{
		AActor** ActorPtr = ActorNameToActorMap.Find(ActorPossessableToFix.GetName());
		if (ActorPtr != nullptr)
		{
			FGuid OldGuid = ActorPossessableToFix.GetGuid();

			// The actor might have an existing guid while the possessable with the same name might not. 
			// In that case, make sure we also replace the existing guid with the new guid 
			FGuid ExistingGuid = Sequencer->FindObjectId(**ActorPtr, Sequencer->GetFocusedTemplateID());

			FGuid NewGuid = FSequencerUtilities::DoAssignActor(Sequencer.Get(), ActorPtr, 1, ActorPossessableToFix.GetGuid());

			OldGuidToNewGuidMap.Add(OldGuid, NewGuid);

			if (ExistingGuid.IsValid())
			{
				OldGuidToNewGuidMap.Add(ExistingGuid, NewGuid);
			}
		}
	}

	UMovieSceneCompiledDataManager* CompiledDataManager = FindObject<UMovieSceneCompiledDataManager>(GetTransientPackage(), TEXT("SequencerCompiledDataManager"));
	if (!CompiledDataManager)
	{
		CompiledDataManager = NewObject<UMovieSceneCompiledDataManager>(GetTransientPackage(), "SequencerCompiledDataManager");
	}

	for (TPair<FGuid, FGuid> GuidPair : OldGuidToNewGuidMap)
	{
		FSequencerUtilities::UpdateBindingIDs(Sequencer.Get(), CompiledDataManager, GuidPair.Key, GuidPair.Value);
	}
}

#undef LOCTEXT_NAMESPACE
