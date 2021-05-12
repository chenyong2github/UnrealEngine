// Copyright Epic Games, Inc. All Rights Reserved.

#include "PreAnimatedState/MovieScenePreAnimatedComponentTransformStorage.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStorageID.inl"

#include "Components/SceneComponent.h"

namespace UE
{
namespace MovieScene
{

TAutoRegisterPreAnimatedStorageID<FPreAnimatedComponentTransformStorage> FPreAnimatedComponentTransformStorage::StorageID;

void FComponentTransformPreAnimatedTraits::CachePreAnimatedValue(UObject* InObject, FIntermediate3DTransform& OutCachedTransform)
{
	OutCachedTransform = GetComponentTransform(InObject);
}

void FComponentTransformPreAnimatedTraits::RestorePreAnimatedValue(const FObjectKey& InKey, FIntermediate3DTransform& CachedTransform, const FRestoreStateParams& Params)
{
	USceneComponent* SceneComponent = Cast<USceneComponent>(InKey.ResolveObjectPtr());
	if (SceneComponent)
	{
		// Ideally we would not be temporarily changing mobility here, but there are some very specific
		// edge cases where mobility can be legitimately restored whilst pre-animated transforms are still
		// maintained. One example is where an attach track has previously been run and since restored - 
		// thus detatching and resetting the transform. If nothing else animates the mobility, this will also
		// be reset, but the object's global transform may have been captured.

		const EComponentMobility::Type PreviousMobility = SceneComponent->Mobility;
		if (PreviousMobility != EComponentMobility::Movable)
		{
			SceneComponent->SetMobility(EComponentMobility::Movable);
		}

		SetComponentTransform(SceneComponent, CachedTransform);

		if (PreviousMobility != EComponentMobility::Movable)
		{
			SceneComponent->SetMobility(PreviousMobility);
		}
	}
}

} // namespace MovieScene
} // namespace UE