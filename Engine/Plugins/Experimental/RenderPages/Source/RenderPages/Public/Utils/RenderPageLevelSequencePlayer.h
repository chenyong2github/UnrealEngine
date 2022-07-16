// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "RenderPageLevelSequencePlayer.generated.h"


/**
 * This class overrides a function in ULevelSequencePlayer to fix an issue with obtaining the camera while running the level sequence player in the editor.
 */
UCLASS()
class RENDERPAGES_API URenderPageLevelSequencePlayer : public ULevelSequencePlayer
{
public:
	GENERATED_BODY()

public:
	static ULevelSequencePlayer* CreateLevelSequencePlayer(UObject* WorldContextObject, ULevelSequence* LevelSequence, FMovieSceneSequencePlaybackSettings Settings, ALevelSequenceActor*& OutActor);

protected:
	virtual void UpdateCameraCut(UObject* CameraObject, const EMovieSceneCameraCutParams& CameraCutParams) override;
};


/**
 * This class overrides the constructor in ALevelSequenceActor so it will use a URenderPageLevelSequencePlayer instance instead of a ULevelSequencePlayer instance.
 */
UCLASS()
class RENDERPAGES_API ARenderPageLevelLevelSequenceActor : public ALevelSequenceActor
{
public:
	GENERATED_BODY()

	ARenderPageLevelLevelSequenceActor(const FObjectInitializer& Init);
};
