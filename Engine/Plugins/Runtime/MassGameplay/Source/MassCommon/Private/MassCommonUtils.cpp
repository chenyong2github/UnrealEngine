// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCommonUtils.h"

namespace UE::Mass::Utils
{
	TArray<FLWEntity> EntityQueueToArray(TQueue<FLWEntity, EQueueMode::Mpsc>& EntitiesQueue, const int32 EntitiesCount)
	{
		check(EntitiesCount > 0);
		TArray<FLWEntity> EntitiesArray;
		EntitiesArray.AddUninitialized(EntitiesCount);

		FLWEntity TempEntity;
		uint32 CurrentIndex = 0;
		while (EntitiesQueue.Dequeue(TempEntity))
		{
			EntitiesArray[CurrentIndex++] = TempEntity;
		}
		ensure(CurrentIndex == EntitiesCount);

		return MoveTemp(EntitiesArray);
	}
} // namespace UE::Mass::Utils