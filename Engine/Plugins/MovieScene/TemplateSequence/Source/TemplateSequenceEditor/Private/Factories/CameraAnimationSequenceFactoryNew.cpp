// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Factories/CameraAnimationSequenceFactoryNew.h"
#include "CameraAnimationSequence.h"
#include "MovieScene.h"
#include "MovieSceneToolsProjectSettings.h"

#define LOCTEXT_NAMESPACE "MovieSceneFactory"

UCameraAnimationSequenceFactoryNew::UCameraAnimationSequenceFactoryNew(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UCameraAnimationSequence::StaticClass();
}

UObject* UCameraAnimationSequenceFactoryNew::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto NewCameraAnimationSequence = NewObject<UCameraAnimationSequence>(InParent, Name, Flags | RF_Transactional);
	NewCameraAnimationSequence->Initialize();

	// Set up some sensible defaults
	const UMovieSceneToolsProjectSettings* ProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();

	FFrameRate TickResolution = NewCameraAnimationSequence->GetMovieScene()->GetTickResolution();
	NewCameraAnimationSequence->GetMovieScene()->SetPlaybackRange((ProjectSettings->DefaultStartTime*TickResolution).FloorToFrame(), (ProjectSettings->DefaultDuration*TickResolution).FloorToFrame().Value);

	return NewCameraAnimationSequence;
}

bool UCameraAnimationSequenceFactoryNew::ShouldShowInNewMenu() const
{
	return true;
}

#undef LOCTEXT_NAMESPACE

