// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/TemplateSequenceFactoryNew.h"
#include "GameFramework/Actor.h"
#include "Kismet2/SClassPickerDialog.h"
#include "Misc/TemplateSequenceEditorUtil.h"
#include "MovieScene.h"
#include "MovieSceneToolsProjectSettings.h"
#include "Sections/MovieSceneBoolSection.h"
#include "TemplateSequence.h"
#include "Tracks/MovieSceneSpawnTrack.h"

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

		UObject* DefaultObject = BoundActorClass->GetDefaultObject();
		const FName DefaultObjectName = BoundActorClass->GetDefaultObjectName();

		UMovieScene* NewMovieScene = NewTemplateSequence->GetMovieScene();
		if (ensure(NewMovieScene && DefaultObject))
		{
			const FGuid NewSpawnableGuid = NewMovieScene->AddSpawnable(DefaultObjectName.ToString(), *DefaultObject);
			
			UMovieSceneSpawnTrack* NewSpawnTrack = Cast<UMovieSceneSpawnTrack>(NewMovieScene->AddTrack(UMovieSceneSpawnTrack::StaticClass(), NewSpawnableGuid));
			UMovieSceneBoolSection* NewSpawnSection = Cast<UMovieSceneBoolSection>(NewSpawnTrack->CreateNewSection());
			NewSpawnSection->GetChannel().SetDefault(true);
			NewSpawnSection->SetRange(TRange<FFrameNumber>::All());
			NewSpawnTrack->AddSection(*NewSpawnSection);
			NewSpawnTrack->SetObjectId(NewSpawnableGuid);
		}
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

#undef LOCTEXT_NAMESPACE
