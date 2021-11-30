// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSequenceEditorSubsystem.h"
#include "EngineUtils.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "SequencerUtilities.h"
#include "MovieScene.h"
#include "MovieSceneSequence.h"
#include "LevelSequenceEditorCommands.h"
#include "ActorTreeItem.h"
#include "LevelEditor.h"
#include "ScopedTransaction.h"
#include "Compilation/MovieSceneCompiledDataManager.h"
#include "Modules/ModuleManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

DEFINE_LOG_CATEGORY(LogLevelSequenceEditor);

void ULevelSequenceEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	UE_LOG(LogLevelSequenceEditor, Log, TEXT("LevelSequenceEditor subsystem initialized."));

	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	OnSequencerCreatedHandle = SequencerModule.RegisterOnSequencerCreated(FOnSequencerCreated::FDelegate::CreateUObject(this, &ULevelSequenceEditorSubsystem::OnSequencerCreated));

	/* Commands for this subsystem */
	CommandList = MakeShareable(new FUICommandList);
	CommandList->MapAction(FLevelSequenceEditorCommands::Get().FixActorReferences,
		FExecuteAction::CreateUObject(this, &ULevelSequenceEditorSubsystem::FixActorReferences)
	);

	/* Createand register "Fix Actor References" extension */
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

	FScopedTransaction FixActorReferencesTransaction(NSLOCTEXT("LevelSequenceEditor", "FixActorReferences", "Fix Actor References"));

	UMovieScene* FocusedMovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!FocusedMovieScene)
	{
		return;
	}

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

