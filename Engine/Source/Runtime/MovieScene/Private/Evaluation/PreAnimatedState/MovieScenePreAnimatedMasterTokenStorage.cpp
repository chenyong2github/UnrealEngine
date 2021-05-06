// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedMasterTokenStorage.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStorageID.inl"

namespace UE
{
namespace MovieScene
{

TAutoRegisterPreAnimatedStorageID<FAnimTypePreAnimatedStateMasterStorage> FAnimTypePreAnimatedStateMasterStorage::StorageID;

void FAnimTypePreAnimatedStateMasterStorage::Initialize(FPreAnimatedStorageID InStorageID, FPreAnimatedStateExtension* InParentExtension)
{
	TPreAnimatedStateStorage<FPreAnimatedMasterTokenTraits>::Initialize(InStorageID, InParentExtension);
}

void FAnimTypePreAnimatedStateMasterStorage::InitializeGroupManager(FPreAnimatedStateExtension* Extension)
{}

void FAnimTypePreAnimatedStateMasterStorage::OnGroupDestroyed(FPreAnimatedStorageGroupHandle InGroup)
{
	check(InGroup == GroupHandle)
	GroupHandle = FPreAnimatedStorageGroupHandle();
}

FPreAnimatedStateEntry FAnimTypePreAnimatedStateMasterStorage::MakeEntry(FMovieSceneAnimTypeID AnimTypeID)
{
	if (!GroupHandle)
	{
		GroupHandle = ParentExtension->AllocateGroup(SharedThis(this));
	}

	// Find the storage index for the specific anim-type and object we're animating
	FPreAnimatedStorageIndex StorageIndex = GetOrCreateStorageIndex(AnimTypeID);
	return FPreAnimatedStateEntry{ GroupHandle, FPreAnimatedStateCachedValueHandle{ StorageID, StorageIndex } };
}



} // namespace MovieScene
} // namespace UE






