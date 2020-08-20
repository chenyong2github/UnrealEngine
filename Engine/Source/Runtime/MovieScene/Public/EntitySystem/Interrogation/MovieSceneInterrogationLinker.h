// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"
#include "UObject/ObjectKey.h"
#include "IMovieScenePlayer.h"

#include "MovieSceneInterrogationLinker.generated.h"

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
class MOVIESCENE_API UMovieSceneInterrogationLinker : public UMovieSceneEntitySystemLinker
{
public:
	GENERATED_BODY()

	UMovieSceneInterrogationLinker(const FObjectInitializer& ObjInit);

	/**
	 * Import a track into this linker. This will add the track to the linker's evaluation field and
	 * cause entities to be created for it at each interrogation channel (if it is relevant at such times)
	 * Must be called before AddInterrogation() and Update()
	 */
	void ImportTrack(UMovieSceneTrack* Track);


	/**
	 * Import multiple tracks into this linker. See ImporTrack above.
	 */
	void ImportTracks(TArrayView<UMovieSceneTrack* const> Tracks)
	{
		for (UMovieSceneTrack* Track : Tracks)
		{
			ImportTrack(Track);
		}
	}


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


	/**
	 * Find an entity given the entity's owner.
	 */
	UE::MovieScene::FMovieSceneEntityID FindEntityFromOwner(UE::MovieScene::FInterrogationChannel InterrogationChannel, UObject* Owner, uint32 EntityID) const;


	/**
	 * Find an entity given the entity's owner.
	 */
	UE::MovieScene::FMovieSceneEntityID FindEntityFromOwner(FFrameTime InterrogationTime, UObject* Owner, uint32 EntityID) const;

private:
	struct FImportedEntityKey
	{
		UE::MovieScene::FInterrogationChannel InterrogationChannel;
		FMovieSceneEvaluationFieldEntityKey Entity;

		friend bool operator==(FImportedEntityKey A, FImportedEntityKey B)
		{
			return A.InterrogationChannel == B.InterrogationChannel && A.Entity == B.Entity;
		}
		friend bool operator!=(FImportedEntityKey A, FImportedEntityKey B)
		{
			return !(A == B);
		}
		friend uint32 GetTypeHash(FImportedEntityKey In)
		{
			return HashCombine(In.InterrogationChannel.AsIndex(), GetTypeHash(In.Entity));
		}
	};

	void InterrogateEntity(const UE::MovieScene::FEntityImportSequenceParams& ImportParams, UE::MovieScene::FInterrogationChannel InterrogationChannel, const FMovieSceneEvaluationFieldEntityQuery& Query);

private:

	/** Scratch buffer used for generating entities for interrogation times */
	FMovieSceneEvaluationFieldEntitySet EntitiesScratch;

	/** Entity component field containing all the entity owners relevant at specific times */
	FMovieSceneEntityComponentField EntityComponentField;

	/** Ledger for all imported and manufactured entities */
	TMap<FImportedEntityKey, FMovieSceneEntityID> ImportedEntities;

	/** A map from interrogation channel to its time */
	TMap<UE::MovieScene::FInterrogationChannel, FFrameTime> ChannelToTime;

	/** The next valid interrogation channel, or invalid if we've reached capacity */
	UE::MovieScene::FInterrogationChannel NextChannel;
};

