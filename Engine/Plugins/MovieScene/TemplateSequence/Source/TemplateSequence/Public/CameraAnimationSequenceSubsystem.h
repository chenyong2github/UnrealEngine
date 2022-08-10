// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EntitySystem/MovieSceneEntitySystemRunner.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectMacros.h"
#include "CameraAnimationSequenceSubsystem.generated.h"

/**
 * World subsystem that holds global objects for handling camera animation sequences.
 */
UCLASS()
class UCameraAnimationSequenceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Get the camera animation sequence subsystem for the given world */
	static UCameraAnimationSequenceSubsystem* GetCameraAnimationSequenceSubsystem(const UWorld* InWorld);

	/** Construct a new camera animation sequence subsystem */
	UCameraAnimationSequenceSubsystem();
	/** Destroy the camera animation sequence subsystem */
	virtual ~UCameraAnimationSequenceSubsystem();

	/** Gets the Sequencer linker owned by this sybsystem */
	UMovieSceneEntitySystemLinker* GetLinker(bool bAutoCreate = true);

private:
	// Begin USubsystem
	virtual void Deinitialize() override;
	// End USubsystem
	
private:
	/** The global Sequencer linker that contains all the shakes and camera animations */
	UPROPERTY(transient)
	TObjectPtr<UMovieSceneEntitySystemLinker> Linker;

	/** The global Sequencer runner that runs all the shakes and camera animations */
	FMovieSceneEntitySystemRunner Runner;
};

