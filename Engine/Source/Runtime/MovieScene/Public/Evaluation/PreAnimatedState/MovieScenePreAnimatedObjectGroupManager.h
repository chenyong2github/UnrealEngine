// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Map.h"
#include "UObject/ObjectKey.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateExtension.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStorageID.h"

namespace UE
{
namespace MovieScene
{

struct MOVIESCENE_API FPreAnimatedObjectGroupManager : IPreAnimatedStateGroupManager, TSharedFromThis<FPreAnimatedObjectGroupManager>
{
	static TAutoRegisterPreAnimatedStorageID<FPreAnimatedObjectGroupManager> GroupManagerID;

	void InitializeGroupManager(FPreAnimatedStateExtension* Extension) override;
	void OnGroupDestroyed(FPreAnimatedStorageGroupHandle Group) override;

	FPreAnimatedStorageGroupHandle FindGroupForObject(const FObjectKey& Object) const;

	FPreAnimatedStorageGroupHandle MakeGroupForObject(const FObjectKey& Object);

	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap);

	void GetGroupsByClass(UClass* GeneratedClass, TArray<FPreAnimatedStorageGroupHandle>& OutGroupHandles);

private:

	TMap<FObjectKey, FPreAnimatedStorageGroupHandle> StorageGroupsByObject;
	TMap<FPreAnimatedStorageGroupHandle, FObjectKey> StorageGroupsToObject;

	FPreAnimatedStateExtension* Extension;
};


} // namespace MovieScene
} // namespace UE
