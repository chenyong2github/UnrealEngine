// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/TemplateSequenceFactoryNew.h"
#include "TemplateSequence.h"
#include "MovieScene.h"
#include "MovieSceneToolsProjectSettings.h"

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

	return NewTemplateSequence;
}

bool UTemplateSequenceFactoryNew::ShouldShowInNewMenu() const
{
	return true;
}

#undef LOCTEXT_NAMESPACE
