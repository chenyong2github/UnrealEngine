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


struct FPreAnimatedObjectTokenTraits
{
	struct FAnimatedKey
	{
		FObjectKey BoundObject;
		FMovieSceneAnimTypeID AnimTypeID;

		friend uint32 GetTypeHash(const FAnimatedKey& In)
		{
			return HashCombine(GetTypeHash(In.BoundObject), GetTypeHash(In.AnimTypeID));
		}
		friend bool operator==(const FAnimatedKey& A, const FAnimatedKey& B)
		{
			return A.BoundObject == B.BoundObject && A.AnimTypeID == B.AnimTypeID;
		}
	};

	using KeyType     = FAnimatedKey;
	using StorageType = IMovieScenePreAnimatedTokenPtr;

	static void RestorePreAnimatedValue(const FAnimatedKey& InKey, IMovieScenePreAnimatedTokenPtr& Token, const FRestoreStateParams& Params)
	{
		if (UObject* Object = InKey.BoundObject.ResolveObjectPtr())
		{
			Token->RestoreState(*Object, Params);
		}
	}

	TSharedPtr<FPreAnimatedObjectGroupManager> ObjectGroupManager;
};


struct MOVIESCENE_API FAnimTypePreAnimatedStateObjectStorage : TPreAnimatedStateStorage<FPreAnimatedObjectTokenTraits>
{
	static TAutoRegisterPreAnimatedStorageID<FAnimTypePreAnimatedStateObjectStorage> StorageID;

	FPreAnimatedStateEntry MakeEntry(UObject* Object, FMovieSceneAnimTypeID AnimTypeID);

	void Initialize(FPreAnimatedStorageID InStorageID, FPreAnimatedStateExtension* InParentExtension) override;
	void OnObjectReplaced(FPreAnimatedStorageIndex StorageIndex, const FObjectKey& OldObject, const FObjectKey& NewObject) override;


private:

	TSharedPtr<FPreAnimatedObjectGroupManager> ObjectGroupManager;
};


} // namespace MovieScene
} // namespace UE






