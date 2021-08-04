// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedObjectGroupManager.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStorageID.inl"

namespace UE
{
namespace MovieScene
{

TAutoRegisterPreAnimatedStorageID<FPreAnimatedObjectGroupManager> FPreAnimatedObjectGroupManager::GroupManagerID;

void FPreAnimatedObjectGroupManager::InitializeGroupManager(FPreAnimatedStateExtension* InExtension)
{
	Extension = InExtension;
}

void FPreAnimatedObjectGroupManager::OnGroupDestroyed(FPreAnimatedStorageGroupHandle Group)
{
	FObjectKey Temp = StorageGroupsToObject.FindChecked(Group);

	StorageGroupsByObject.Remove(Temp);
	StorageGroupsToObject.Remove(Group);
}

FPreAnimatedStorageGroupHandle FPreAnimatedObjectGroupManager::FindGroupForObject(const FObjectKey& Object) const
{
	return StorageGroupsByObject.FindRef(Object);
}

FPreAnimatedStorageGroupHandle FPreAnimatedObjectGroupManager::MakeGroupForObject(const FObjectKey& Object)
{
	FPreAnimatedStorageGroupHandle GroupHandle = StorageGroupsByObject.FindRef(Object);
	if (GroupHandle)
	{
		return GroupHandle;
	}

	GroupHandle = Extension->AllocateGroup(AsShared());
	StorageGroupsByObject.Add(Object, GroupHandle);
	StorageGroupsToObject.Add(GroupHandle, Object);
	return GroupHandle;
}

void FPreAnimatedObjectGroupManager::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	TMap<FObjectKey, FPreAnimatedStorageGroupHandle> OldStorageGroupsByObject = MoveTemp(StorageGroupsByObject);
	StorageGroupsByObject.Reset();
	StorageGroupsByObject.Reserve(OldStorageGroupsByObject.Num());

	for (auto It = OldStorageGroupsByObject.CreateIterator(); It; ++It)
	{
		FPreAnimatedStorageGroupHandle GroupHandle = It.Value();

		UObject* Object = It.Key().ResolveObjectPtrEvenIfPendingKill();
		if (UObject* ReplacedObject = ReplacementMap.FindRef(Object))
		{
			FObjectKey NewKey(ReplacedObject);

			StorageGroupsByObject.Add(NewKey, GroupHandle);
			// This will overwrite the existing entry
			StorageGroupsToObject.Add(GroupHandle, NewKey);

			Extension->ReplaceObjectForGroup(GroupHandle, It.Key(), NewKey);
		}
		else
		{
			StorageGroupsByObject.Add(It.Key(), It.Value());
		}
	}
}

void FPreAnimatedObjectGroupManager::GetGroupsByClass(UClass* GeneratedClass, TArray<FPreAnimatedStorageGroupHandle>& OutGroupHandles)
{
	for (auto It = StorageGroupsByObject.CreateConstIterator(); It; ++It)
	{
		UObject* Object = It.Key().ResolveObjectPtrEvenIfPendingKill();
		if (Object && Object->IsA(GeneratedClass))
		{
			OutGroupHandles.Add(It.Value());
		}
	}
}

} // namespace MovieScene
} // namespace UE
