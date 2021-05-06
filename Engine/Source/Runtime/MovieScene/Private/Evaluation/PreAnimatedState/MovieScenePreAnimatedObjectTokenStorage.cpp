// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedObjectTokenStorage.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStorageID.inl"
#include "Evaluation/MovieSceneEvaluationKey.h"

namespace UE
{
namespace MovieScene
{

TAutoRegisterPreAnimatedStorageID<FAnimTypePreAnimatedStateObjectStorage> FAnimTypePreAnimatedStateObjectStorage::StorageID;

void FAnimTypePreAnimatedStateObjectStorage::Initialize(FPreAnimatedStorageID InStorageID, FPreAnimatedStateExtension* InParentExtension)
{
	TPreAnimatedStateStorage<FPreAnimatedObjectTokenTraits>::Initialize(InStorageID, InParentExtension);

	ObjectGroupManager = InParentExtension->GetOrCreateGroupManager<FPreAnimatedObjectGroupManager>();
}

void FAnimTypePreAnimatedStateObjectStorage::OnObjectReplaced(FPreAnimatedStorageIndex StorageIndex, const FObjectKey& OldObject, const FObjectKey& NewObject)
{
	FPreAnimatedObjectTokenTraits::FAnimatedKey ExistingKey = GetKey(StorageIndex);
	ExistingKey.BoundObject = NewObject;

	ReplaceKey(StorageIndex, ExistingKey);
}

FPreAnimatedStateEntry FAnimTypePreAnimatedStateObjectStorage::MakeEntry(UObject* Object, FMovieSceneAnimTypeID AnimTypeID)
{
	FPreAnimatedObjectTokenTraits::FAnimatedKey Key{ Object, AnimTypeID };

	// Begin by finding or creating a pre-animated state group for this bound object
	FPreAnimatedStorageGroupHandle Group = ObjectGroupManager->MakeGroupForObject(Object);

	// Find the storage index for the specific anim-type and object we're animating
	FPreAnimatedStorageIndex StorageIndex = GetOrCreateStorageIndex(Key);

	return FPreAnimatedStateEntry{ Group, FPreAnimatedStateCachedValueHandle{ StorageID, StorageIndex } };
}

} // namespace MovieScene
} // namespace UE






