// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "MovieSceneTracksPropertyTypes.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedObjectStorage.h"

namespace UE
{
namespace MovieScene
{

struct MOVIESCENETRACKS_API FComponentTransformPreAnimatedTraits
{
	using KeyType     = FObjectKey;
	using StorageType = FIntermediate3DTransform;

	static void CachePreAnimatedValue(UObject* InObject, FIntermediate3DTransform& OutCachedTransform);
	static void RestorePreAnimatedValue(const FObjectKey& InKey, FIntermediate3DTransform& CachedTransform, const FRestoreStateParams& Params);
};

struct MOVIESCENETRACKS_API FPreAnimatedComponentTransformStorage
	: TPreAnimatedStateStorage_ObjectTraits<FComponentTransformPreAnimatedTraits>
{
	static TAutoRegisterPreAnimatedStorageID<FPreAnimatedComponentTransformStorage> StorageID;

	FPreAnimatedStorageID GetStorageType() const override { return StorageID; }

	void OnObjectReplaced(FPreAnimatedStorageIndex StorageIndex, const FObjectKey& OldObject, const FObjectKey& NewObject) override
	{
		ReplaceKey(StorageIndex, NewObject);
	}
};


} // namespace MovieScene
} // namespace UE