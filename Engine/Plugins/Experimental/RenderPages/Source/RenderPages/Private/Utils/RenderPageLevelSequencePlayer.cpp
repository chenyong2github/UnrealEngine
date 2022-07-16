// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils/RenderPageLevelSequencePlayer.h"
#include "MovieSceneCommonHelpers.h"
#include "Camera/CameraComponent.h"


ULevelSequencePlayer* URenderPageLevelSequencePlayer::CreateLevelSequencePlayer(UObject* WorldContextObject, ULevelSequence* InLevelSequence, FMovieSceneSequencePlaybackSettings Settings, ALevelSequenceActor*& OutActor)
{
	// entire function is copied from ULevelSequencePlayer::CreateLevelSequencePlayer (LevelSequencePlayer.cpp, lines 42-75)

	if (InLevelSequence == nullptr)
	{
		return nullptr;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (World == nullptr || World->bIsTearingDown)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bAllowDuringConstructionScript = true;

	// Defer construction for autoplay so that BeginPlay() is called
	SpawnParams.bDeferConstruction = true;

	ALevelSequenceActor* Actor = World->SpawnActor<ARenderPageLevelLevelSequenceActor>(SpawnParams); // line contains changes

	Actor->PlaybackSettings = Settings;
	Actor->SetSequence(InLevelSequence);

	Actor->InitializePlayer();
	OutActor = Actor;

	FTransform DefaultTransform;
	Actor->FinishSpawning(DefaultTransform);

	return Actor->SequencePlayer;
}

void URenderPageLevelSequencePlayer::UpdateCameraCut(UObject* CameraObject, const EMovieSceneCameraCutParams& CameraCutParams)
{
	CachedCameraComponent = MovieSceneHelpers::CameraComponentFromRuntimeObject(CameraObject);

	ULevelSequencePlayer::UpdateCameraCut(CameraObject, CameraCutParams);
}


ARenderPageLevelLevelSequenceActor::ARenderPageLevelLevelSequenceActor(const FObjectInitializer& Init)
	: Super(Init)
{
	// copied from ALevelSequenceActor::EndPlay (LevelSequenceActor.cpp, lines 184-196)
	if (SequencePlayer)
	{
		SequencePlayer->Stop();
		SequencePlayer->OnPlay.RemoveAll(this);
		SequencePlayer->OnPlayReverse.RemoveAll(this);
		SequencePlayer->OnStop.RemoveAll(this);
		SequencePlayer->TearDown();
	}

	// copied from ALevelSequenceActor constructor (LevelSequenceActor.cpp, lines 71-75)
	SequencePlayer = Init.CreateDefaultSubobject<URenderPageLevelSequencePlayer>(this, "RenderPageAnimationPlayer"); // line contains changes
	SequencePlayer->OnPlay.AddDynamic(this, &ALevelSequenceActor::ShowBurnin);
	SequencePlayer->OnPlayReverse.AddDynamic(this, &ALevelSequenceActor::ShowBurnin);
	SequencePlayer->OnStop.AddDynamic(this, &ALevelSequenceActor::HideBurnin);
}
