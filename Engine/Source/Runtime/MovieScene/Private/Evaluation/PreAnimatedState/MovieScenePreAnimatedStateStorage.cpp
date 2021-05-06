// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateStorage.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/EntityAllocationIterator.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"

namespace UE
{
namespace MovieScene
{

FPreAnimatedTrackerParams::FPreAnimatedTrackerParams(FEntityAllocationIteratorItem Item)
{
	Num = Item.GetAllocation()->Num();
	bWantsRestoreState = Item.GetAllocationType().Contains(FBuiltInComponentTypes::Get()->Tags.RestoreState);
}


} // namespace MovieScene
} // namespace UE






