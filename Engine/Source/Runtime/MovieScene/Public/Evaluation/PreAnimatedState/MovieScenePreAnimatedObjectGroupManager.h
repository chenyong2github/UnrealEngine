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

struct MOVIESCENE_API FPreAnimatedObjectGroupManager : TPreAnimatedStateGroupManager<FObjectKey>
{
	static TAutoRegisterPreAnimatedStorageID<FPreAnimatedObjectGroupManager> GroupManagerID;

	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap);

	void GetGroupsByClass(UClass* GeneratedClass, TArray<FPreAnimatedStorageGroupHandle>& OutGroupHandles);
};


} // namespace MovieScene
} // namespace UE
