// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "MovieSceneExecutionToken.h"
#include "EntitySystem/MovieSceneSequenceInstanceHandle.h"
#include "Evaluation/MovieSceneAnimTypeID.h"
#include "Evaluation/PreAnimatedState/MovieSceneRestoreStateParams.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateStorage.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStorageID.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateExtension.h"


namespace UE
{
namespace MovieScene
{

struct FPreAnimatedMasterTokenTraits
{
	using KeyType     = FMovieSceneAnimTypeID;
	using StorageType = IMovieScenePreAnimatedGlobalTokenPtr;

	static void RestorePreAnimatedValue(FMovieSceneAnimTypeID, IMovieScenePreAnimatedGlobalTokenPtr& Token, const FRestoreStateParams& Params)
	{
		Token->RestoreState(Params);
	}
};

struct MOVIESCENE_API FAnimTypePreAnimatedStateMasterStorage : TPreAnimatedStateStorage<FPreAnimatedMasterTokenTraits>, IPreAnimatedStateGroupManager
{
	static TAutoRegisterPreAnimatedStorageID<FAnimTypePreAnimatedStateMasterStorage> StorageID;

	FPreAnimatedStateEntry MakeEntry(FMovieSceneAnimTypeID AnimTypeID);

public:

	FPreAnimatedStorageID GetStorageType() const override { return StorageID; }
	void Initialize(FPreAnimatedStorageID InStorageID, FPreAnimatedStateExtension* ParentExtension) override;

	void InitializeGroupManager(FPreAnimatedStateExtension* Extension) override;
	void OnGroupDestroyed(FPreAnimatedStorageGroupHandle Group) override;

private:


	TMap<FMovieSceneAnimTypeID, FPreAnimatedStorageGroupHandle> GroupsByAnimTypeID;
	FPreAnimatedStorageGroupHandle GroupHandle;
};



} // namespace MovieScene
} // namespace UE






