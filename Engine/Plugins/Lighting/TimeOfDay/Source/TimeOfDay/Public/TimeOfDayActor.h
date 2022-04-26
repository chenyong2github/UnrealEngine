// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LevelSequenceActor.h"
#include "TimeOfDayActor.generated.h"

class USkyAtmosphereComponent;
class USkyLightComponent;
class UDirectionalLightComponent;

UCLASS(prioritizeCategories=(TimeOfDay, RuntimeDayCycle), hideCategories=(Rendering, Physics, HLOD, Activation, Input, Collision, Actor, Lod, Cooking, DataLayers, Replication, WorldPartition))
class TIMEOFDAY_API ATimeOfDayActor : public ALevelSequenceActor
{
	GENERATED_BODY()

public:
	ATimeOfDayActor(const FObjectInitializer& Init);

protected:
	//~ Begin AActor interface
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void RewindForReplay() override;
	//~ End AActor interface

	/** Customized TimeOfDay initialization of the SequencePlayer */
	void InitializeSequencePlayer();

	/** Compute PlaybackSettings from day cycle properties */
	FMovieSceneSequencePlaybackSettings GetPlaybackSettings(const ULevelSequence* Sequence) const;

protected:
	UPROPERTY()
	TObjectPtr<ULevelSequence> MasterSequence;

#if WITH_EDITORONLY_DATA
	/** Sets the time of day to preview in the editor. Does not affect the start time at runtime */
	UPROPERTY(EditAnywhere, Category = TimeOfDay)
	FTimecode TimeOfDayPreview;
#endif

	/** Whether or not to run a day cycle. If this is unchecked the day cycle will remain fixed at the time specified by the Initial Time setting */
	UPROPERTY(EditAnywhere, Category = RuntimeDayCycle)
	bool bRunDayCycle;

	/** How long a single day cycle is */
	UPROPERTY(EditAnywhere, Category = RuntimeDayCycle)
	FTimecode DayLength;

	/** How long does it take for a day cycle to complete in world time. If this is the same value as day duration that means real world time is used */
	UPROPERTY(EditAnywhere, Category = RuntimeDayCycle)
	FTimecode TimePerCycle;

	/** The initial time that the day cycle will start at */
	UPROPERTY(EditAnywhere, Category = RuntimeDayCycle)
	FTimecode InitialTimeOfDay;
};

