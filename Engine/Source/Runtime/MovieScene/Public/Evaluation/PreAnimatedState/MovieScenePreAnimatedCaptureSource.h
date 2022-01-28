// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/TVariant.h"
#include "MovieSceneSequenceID.h"
#include "Evaluation/MovieSceneEvaluationKey.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateTypes.h"
#include "EntitySystem/MovieSceneSequenceInstanceHandle.h"
#include "EntitySystem/TrackInstance/MovieSceneTrackInstance.h"

struct FMovieSceneSequenceID;
struct FMovieSceneEvaluationKey;

class UObject;
class UMovieSceneTrackInstance;
class UMovieSceneEntitySystemLinker;
class FMovieScenePreAnimatedState;

namespace UE
{
namespace MovieScene
{

struct FPreAnimatedStateMetaData;

}
}


/**
 * Scoped structure that can be used to wrap a call to SavePreAnimatedState to associate the capture with a specific capture source.
 */
struct FScopedPreAnimatedCaptureSource
{
	/**
	 * Construct this capture source from a template (FMovieSceneEvalTemplate) evaluation key, and whether this should restore state when the template is finished
	 */
	MOVIESCENE_API explicit FScopedPreAnimatedCaptureSource(FMovieScenePreAnimatedState* InPreAnimatedState, const FMovieSceneEvaluationKey& InEvalKey, bool bInWantsRestoreState);

	/**
	 * Construct this capture source from an evaluation hook (UMovieSceneEvaluationHookSection), its SequenceID, and whether this should restore state when the template is finished
	 */
	MOVIESCENE_API explicit FScopedPreAnimatedCaptureSource(FMovieScenePreAnimatedState* InPreAnimatedState, const UObject* InEvalHook, FMovieSceneSequenceID SequenceID, bool bInWantsRestoreState);

	/**
	 * Construct this capture source from a track instance (UMovieSceneTrackInstance) and whether this should restore state when the template is finished
	 */
	MOVIESCENE_API explicit FScopedPreAnimatedCaptureSource(UMovieSceneEntitySystemLinker* InLinker, UMovieSceneTrackInstance* InTrackInstance, bool bInWantsRestoreState);

	/**
	 * Construct this capture source from a track instance input (UMovieSceneTrackInstance + FMovieSceneTrackInstanceInput) and whether this should restore state when the template is finished
	 */
	MOVIESCENE_API explicit FScopedPreAnimatedCaptureSource(UMovieSceneEntitySystemLinker* InLinker, const FMovieSceneTrackInstanceInput& TrackInstanceInput);

	FScopedPreAnimatedCaptureSource(const FScopedPreAnimatedCaptureSource&) = delete;
	void operator=(const FScopedPreAnimatedCaptureSource&) = delete;

	FScopedPreAnimatedCaptureSource(FScopedPreAnimatedCaptureSource&&) = delete;
	void operator=(FScopedPreAnimatedCaptureSource&&) = delete;

	MOVIESCENE_API ~FScopedPreAnimatedCaptureSource();

	bool WantsRestoreState() const
	{
		return bWantsRestoreState;
	}

private:

	static FScopedPreAnimatedCaptureSource*& GetCaptureSourcePtr();

	void BeginTracking(const UE::MovieScene::FPreAnimatedStateMetaData& MetaData, UMovieSceneEntitySystemLinker* Linker);
	UE::MovieScene::FInstanceHandle GetRootInstanceHandle(UMovieSceneEntitySystemLinker* Linker) const;

	friend UE::MovieScene::FPreAnimatedStateExtension;
	struct FEvalHookType
	{
		const UObject* EvalHook;
		FMovieSceneSequenceID SequenceID;
	};
	using CaptureSourceType = TVariant<FMovieSceneEvaluationKey, FEvalHookType, UMovieSceneTrackInstance*, FMovieSceneTrackInstanceInput>;

	CaptureSourceType Variant;
	FMovieScenePreAnimatedState* OptionalSequencePreAnimatedState;
	FScopedPreAnimatedCaptureSource* PrevCaptureSource;
	bool bWantsRestoreState;
};