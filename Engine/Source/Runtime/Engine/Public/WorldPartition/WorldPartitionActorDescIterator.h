// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_EDITOR
#include "Containers/Map.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

template <typename ActorType, typename ActorDescType = FWorldPartitionActorDesc>
class TWorldPartitionActorDescIterator
{
public:
	explicit TWorldPartitionActorDescIterator(UWorldPartition* InWorldPartition)
		: ActorsIterator(InWorldPartition->Actors)
	{
		if (ShouldSkip())
		{
			operator++();
		}
	}

	/**
	 * Iterates to next suitable actor desc.
	 */
	void operator++()
	{
		do
		{
			++ActorsIterator;
		} while (ShouldSkip());
	}

	/**
	 * Returns the current suitable actor desc pointed at by the Iterator
	 *
	 * @return	Current suitable actor desc
	 */
	FORCEINLINE ActorDescType* operator*() const
	{
		return StaticCast<ActorDescType*>(ActorsIterator->Value->Get());
	}

	/**
	 * Returns the current suitable actor desc pointed at by the Iterator
	 *
	 * @return	Current suitable actor desc
	 */
	FORCEINLINE ActorDescType* operator->() const
	{
		return StaticCast<ActorDescType*>(ActorsIterator->Value->Get());
	}
	/**
	 * Returns whether the iterator has reached the end and no longer points
	 * to a suitable actor desc.
	 *
	 * @return true if iterator points to a suitable actor desc, false if it has reached the end
	 */
	FORCEINLINE explicit operator bool() const
	{
		return (bool)ActorsIterator;
	}

private:
	/**
	 * Determines whether the iterator currently points to a valid actor desc or not.
	 * @return	true
	 */
	FORCEINLINE bool ShouldSkip() const
	{
		if (!ActorsIterator)
		{
			return false;
		}

		return !ActorsIterator->Value->Get()->GetActorClass()->IsChildOf(ActorType::StaticClass());
	}

private:
	TMap<FGuid, TUniquePtr<FWorldPartitionActorDesc>*>::TIterator ActorsIterator;
};
#endif