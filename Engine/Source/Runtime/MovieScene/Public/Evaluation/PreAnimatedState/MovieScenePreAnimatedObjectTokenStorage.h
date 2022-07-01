// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/ObjectKey.h"

#include "MovieSceneExecutionToken.h"
#include "EntitySystem/MovieSceneSequenceInstanceHandle.h"
#include "Evaluation/MovieSceneAnimTypeID.h"
#include "Evaluation/PreAnimatedState/MovieSceneRestoreStateParams.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateStorage.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStorageID.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateExtension.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedObjectGroupManager.h"


namespace UE
{
namespace MovieScene
{


struct FPreAnimatedObjectTokenTraits : FBoundObjectPreAnimatedStateTraits
{
	using KeyType     = TTuple<FObjectKey, FMovieSceneAnimTypeID>;
	using StorageType = IMovieScenePreAnimatedTokenPtr;

	static void RestorePreAnimatedValue(const KeyType& InKey, IMovieScenePreAnimatedTokenPtr& Token, const FRestoreStateParams& Params)
	{
		if (UObject* Object = InKey.Get<0>().ResolveObjectPtr())
		{
			Token->RestoreState(*Object, Params);
		}
	}
};


struct MOVIESCENE_API FAnimTypePreAnimatedStateObjectStorage : TPreAnimatedStateStorage<FPreAnimatedObjectTokenTraits>
{
	static TAutoRegisterPreAnimatedStorageID<FAnimTypePreAnimatedStateObjectStorage> StorageID;

	FPreAnimatedStateEntry MakeEntry(UObject* Object, FMovieSceneAnimTypeID AnimTypeID);
};


} // namespace MovieScene
} // namespace UE






