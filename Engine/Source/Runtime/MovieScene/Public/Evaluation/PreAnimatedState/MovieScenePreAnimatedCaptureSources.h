// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include "UObject/ObjectKey.h"

#include "Evaluation/IMovieSceneEvaluationHook.h"
#include "Evaluation/MovieSceneEvaluationKey.h"

#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateTypes.h"
#include "Evaluation/PreAnimatedState/IMovieScenePreAnimatedCaptureSource.h"

class UMovieSceneTrackInstance;

namespace UE
{
namespace MovieScene
{

struct FRestoreStateParams;
struct FPreAnimatedStateExtension;
struct FPreAnimatedEvalHookKeyType;

enum class EPreAnimatedCaptureSourceState
{
	// This is a new capture source
	New,
	// This capture source needed to be updated due to its capture requirements changing
	Updated,
	// The capture source has already been encountered and does not need updating
	UpToDate,
};

struct FPreAnimatedEvalHookKeyType
{
	FObjectKey Hook;
	FMovieSceneSequenceID SequenceID;

	friend uint32 GetTypeHash(const FPreAnimatedEvalHookKeyType& In) { return GetTypeHash(In.Hook) ^ GetTypeHash(In.SequenceID); }
	friend bool operator==(const FPreAnimatedEvalHookKeyType& A, const FPreAnimatedEvalHookKeyType& B) { return A.Hook == B.Hook && A.SequenceID == B.SequenceID; }
};

/**
 * Structure responsible for tracking contributions to pre-eanimated state entries that originate from track templates (ie, from an IMovieSceneExecutionToken::Execute)
 */
template<typename KeyType>
struct TPreAnimatedCaptureSources : IPreAnimatedCaptureSource
{
	TPreAnimatedCaptureSources(FPreAnimatedStateExtension* InOwner);

	/**
	 * Make an association for the specifieid evaluation key to the value specified by Entry,
	 * along with an indication of whether it should be restored on completion
	 */
	EPreAnimatedCaptureSourceState BeginTrackingCaptureSource(const KeyType& InKey, const FPreAnimatedStateMetaData& MetaData);

	/**
	 * Stop tracking the evaluation key in question, restoring the value if it was captured with bWantsRestoreState
	 */
	void StopTrackingCaptureSource(const KeyType& InKey);

	void Reset() override;
	bool ContainsInstanceHandle(FInstanceHandle RootInstanceHandle) const override;
	void GatherAndRemoveExpiredMetaData(const FRestoreStateParams& Params, TArray<FPreAnimatedStateMetaData>& OutExpiredMetaData) override;
	void GatherAndRemoveMetaDataForGroup(FPreAnimatedStorageGroupHandle Group, TArray<FPreAnimatedStateMetaData>& OutExpiredMetaData) override;

private:

	TMap<KeyType, FPreAnimatedStateMetaDataArray> KeyToMetaData;
	FPreAnimatedStateExtension* Owner;
};


/**
 * Structure responsible for tracking contributions to pre-eanimated state entries that originate from track templates (ie, from an IMovieSceneExecutionToken::Execute)
 */
struct FPreAnimatedTemplateCaptureSources : TPreAnimatedCaptureSources<FMovieSceneEvaluationKey>
{
	MOVIESCENE_API FPreAnimatedTemplateCaptureSources(FPreAnimatedStateExtension* InOwner);
};

/**
 * Structure responsible for tracking contributions to pre-eanimated state entries that originate from track EvaluationHooks (ie, from an IMovieSceneExecutionToken::Execute)
 */
struct FPreAnimatedEvaluationHookCaptureSources : TPreAnimatedCaptureSources<FPreAnimatedEvalHookKeyType>
{
	MOVIESCENE_API FPreAnimatedEvaluationHookCaptureSources(FPreAnimatedStateExtension* InOwner);

	/**
	 * Make an association for the specifieid evaluation key to the value specified by Entry,
	 * along with an indication of whether it should be restored on completion
	 */
	MOVIESCENE_API EPreAnimatedCaptureSourceState BeginTrackingCaptureSource(const UObject* Hook, FMovieSceneSequenceID SequenceID, const FPreAnimatedStateMetaData& MetaData);

	/**
	 * Stop tracking the evaluation key in question, restoring the value if it was captured with bWantsRestoreState
	 */
	MOVIESCENE_API void StopTrackingCaptureSource(const UObject* Hook, FMovieSceneSequenceID SequenceID);
};


/**
 * Structure responsible for tracking contributions to pre-eanimated state entries that originate from track templates (ie, from an IMovieSceneExecutionToken::Execute)
 */
struct FPreAnimatedTrackInstanceCaptureSources : TPreAnimatedCaptureSources<FObjectKey>
{
	MOVIESCENE_API FPreAnimatedTrackInstanceCaptureSources(FPreAnimatedStateExtension* InOwner);

	/**
	 * Make an association for the specifieid evaluation key to the value specified by Entry,
	 * along with an indication of whether it should be restored on completion
	 */
	MOVIESCENE_API EPreAnimatedCaptureSourceState BeginTrackingCaptureSource(UMovieSceneTrackInstance* TrackInstance, const FPreAnimatedStateMetaData& MetaData);

	/**
	 * Stop tracking the evaluation key in question, restoring the value if it was captured with bWantsRestoreState
	 */
	MOVIESCENE_API void StopTrackingCaptureSource(UMovieSceneTrackInstance* TrackInstance);
};


} // namespace MovieScene
} // namespace UE
