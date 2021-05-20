// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Map.h"
#include "Containers/Array.h"

#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieSceneSequenceInstanceHandle.h"

#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStorageID.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateTypes.h"

namespace UE
{
namespace MovieScene
{

struct FRestoreStateParams;
struct FPreAnimatedStateExtension;

/**
 * Structure responsible for tracking contributions to pre-eanimated state entries that originate from ECS data (ie, from FMovieSceneEntityIDs)
 */
struct FPreAnimatedEntityCaptureSource
{
	FPreAnimatedEntityCaptureSource(FPreAnimatedStateExtension* InOwner);

	void Reset();

	MOVIESCENE_API void BeginTrackingEntity(const FPreAnimatedStateEntry& Entry, FMovieSceneEntityID EntityID, FInstanceHandle RootInstanceHandle, bool bWantsRestoreState);
	MOVIESCENE_API void StopTrackingEntity(FMovieSceneEntityID EntityID, FPreAnimatedStorageID StorageID);

	bool ContainsInstanceHandle(FInstanceHandle RootInstanceHandle) const;
	void GatherAndRemoveExpiredMetaData(const FRestoreStateParams& Params, TArray<FPreAnimatedStateMetaData>& OutExpiredMetaData);
	void GatherAndRemoveMetaDataForGroup(FPreAnimatedStorageGroupHandle Group, TArray<FPreAnimatedStateMetaData>& OutExpiredMetaData);

private:

	TMap<FMovieSceneEntityID, FPreAnimatedStateMetaDataArray> KeyToMetaData;
	FPreAnimatedStateExtension* Owner;
};




} // namespace MovieScene
} // namespace UE
