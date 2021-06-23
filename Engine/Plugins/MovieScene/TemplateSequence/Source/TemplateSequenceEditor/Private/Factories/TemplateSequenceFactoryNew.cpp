// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/TemplateSequenceFactoryNew.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/Actor.h"
#include "ILevelSequenceModule.h"
#include "Kismet2/SClassPickerDialog.h"
#include "Misc/TemplateSequenceEditorUtil.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "MovieSceneToolsProjectSettings.h"
#include "Sections/MovieSceneBoolSection.h"
#include "TemplateSequence.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "MovieSceneFactory"

UTemplateSequenceFactoryNew::UTemplateSequenceFactoryNew(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UTemplateSequence::StaticClass();
}

UObject* UTemplateSequenceFactoryNew::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto NewTemplateSequence = NewObject<UTemplateSequence>(InParent, Name, Flags | RF_Transactional);
	NewTemplateSequence->Initialize();

	// Set up some sensible defaults
	const UMovieSceneToolsProjectSettings* ProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();

	FFrameRate TickResolution = NewTemplateSequence->GetMovieScene()->GetTickResolution();
	NewTemplateSequence->GetMovieScene()->SetPlaybackRange((ProjectSettings->DefaultStartTime*TickResolution).FloorToFrame(), (ProjectSettings->DefaultDuration*TickResolution).FloorToFrame().Value);

	if (BoundActorClass)
	{
		NewTemplateSequence->BoundActorClass = BoundActorClass;

		InitializeSpawnable(NewTemplateSequence, BoundActorClass);
	}

	return NewTemplateSequence;
}

bool UTemplateSequenceFactoryNew::ConfigureProperties()
{
	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.DisplayMode = EClassViewerDisplayMode::TreeView;
	Options.bShowObjectRootClass = true;
	Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::Dynamic;

	const FText TitleText = LOCTEXT("CreateTemplateSequenceOptions", "Pick Root Object Binding Class");
	UClass* ChosenClass = nullptr;
	const bool bPressedOk = SClassPickerDialog::PickClass(TitleText, Options, ChosenClass, AActor::StaticClass());
	if (bPressedOk)
	{
		BoundActorClass = ChosenClass;
	}

	return bPressedOk;
}

bool UTemplateSequenceFactoryNew::ShouldShowInNewMenu() const
{
	return true;
}

void UTemplateSequenceFactoryNew::InitializeSpawnable(UMovieSceneSequence* InTemplateSequence, TSubclassOf<UObject> InObjectTemplateClass)
{
	// Grab the object spawners, so we can create a new actor of the desired class.
	TArray<TSharedRef<IMovieSceneObjectSpawner>> ObjectSpawners;
	ILevelSequenceModule& LevelSequenceModule = FModuleManager::LoadModuleChecked<ILevelSequenceModule>("LevelSequence");
	LevelSequenceModule.GenerateObjectSpawners(ObjectSpawners);

	// Run the object spawners until we find one that works.
	UMovieScene* MovieScene = InTemplateSequence->GetMovieScene();
	check(MovieScene);

	TValueOrError<FNewSpawnable, FText> Result = MakeError(LOCTEXT("NoSpawnerFound", "No spawner found to create new spawnable type"));

	for (TSharedRef<IMovieSceneObjectSpawner> Spawner : ObjectSpawners)
	{
		TValueOrError<FNewSpawnable, FText> CurResult = Spawner->CreateNewSpawnableType(*InObjectTemplateClass.Get(), *MovieScene, nullptr);
		if (CurResult.IsValid())
		{
			Result = CurResult;
			break;
		}
	}
	
	// Bail out if we haven't found any good spawner.
	if (!Result.IsValid())
	{
		FNotificationInfo Info(Result.GetError());
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	// Create the spawnable.
	FNewSpawnable& NewSpawnable = Result.GetValue();

	NewSpawnable.Name = MovieSceneHelpers::MakeUniqueSpawnableName(MovieScene, NewSpawnable.Name);			

	const FGuid NewGuid = MovieScene->AddSpawnable(NewSpawnable.Name, *NewSpawnable.ObjectTemplate);
	
	// Create a new spawn section.
	if (ensure(NewGuid.IsValid()))
	{
		UMovieSceneSpawnTrack* NewSpawnTrack = Cast<UMovieSceneSpawnTrack>(MovieScene->AddTrack(UMovieSceneSpawnTrack::StaticClass(), NewGuid));
		UMovieSceneBoolSection* NewSpawnSection = Cast<UMovieSceneBoolSection>(NewSpawnTrack->CreateNewSection());
		NewSpawnSection->GetChannel().SetDefault(true);
		NewSpawnSection->SetRange(TRange<FFrameNumber>::All());
		NewSpawnTrack->AddSection(*NewSpawnSection);
		NewSpawnTrack->SetObjectId(NewGuid);
	}
}

#undef LOCTEXT_NAMESPACE
