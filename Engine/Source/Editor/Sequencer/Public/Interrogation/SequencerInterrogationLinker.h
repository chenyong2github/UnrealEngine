// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntityLedger.h"
#include "UObject/ObjectKey.h"
#include "IMovieScenePlayer.h"

#include "SequencerInterrogationLinker.generated.h"

class UMovieSceneTrack;

/**
 * A linker specialized for interrogating entity data without applying any state.
 * Currently only tracks within the same space and object are supported.
 * Will only link systems that are not excluded from EEntitySystemContext::Interrogation.
 * 
 * Example usage:
 *    Linker->ImportTrack(MyTrack);
 *
 *    for (int32 FrameNumber = 0; FrameNumber < 100; ++FrameNumber)
 *        Linker->AddInterrogation(FrameNumber);
 *
 *    Linker->Update();
 *
 *    TArray64<DataType> OutData;
 *    OutData.SetNum(100);
 *    Linker->FindSystem<UMyTrackSystem>()->Interrogate(OutData);
 */
UCLASS()
class SEQUENCER_API USequencerInterrogationLinker : public UMovieSceneEntitySystemLinker
{
public:
	GENERATED_BODY()

	USequencerInterrogationLinker(const FObjectInitializer& ObjInit);

	/**
	 * Import a track into this linker. This will add the track to the linker's evaluation field and
	 * cause entities to be created for it at each interrogation channel (if it is relevant at such times)
	 * Must be called before AddInterrogation() and Update()
	 */
	void ImportTrack(UMovieSceneTrack* Track);


	/**
	 * Add a new time to interrogate this linker at, in the time-base of the imported tracks
	 *
	 * @param Time     The desired time to interrogate at
	 * @return A unique channel identifier for the specified time, or an invalid channel if there are none left
	 */
	UE::MovieScene::FInterrogationChannel AddInterrogation(FFrameTime Time);


	/**
	 * Flush this linker by running all the systems relevant to the current data, and populating the interrogation outputs
	 */
	void Update();


	/**
	 * Reset this linker back to its original state
	 */
	void Reset();

private:

	/** Scratch buffer used for generating entities for interrogation times */
	TSet<FMovieSceneEvaluationFieldEntityPtr> EntitiesScratch;

	/** Entity component field containing all the entity owners relevant at specific times */
	FMovieSceneEntityComponentField EntityComponentField;

	/** A map from interrogation channel to its time */
	TMap<UE::MovieScene::FInterrogationChannel, FFrameTime> ChannelToTime;

	/** The next valid interrogation channel, or invalid if we've reached capacity */
	UE::MovieScene::FInterrogationChannel NextChannel;
};

