// Copyright Epic Games, Inc. All Rights Reserved.

#include "LoadBalanceCookBurden.h"

#include "Cooker/CookPackageData.h"
#include "Cooker/CookTypes.h"

namespace UE::Cook
{

void LoadBalanceStriped(TConstArrayView<FWorkerId> AllWorkers, TArrayView<FPackageData*> Requests,
	TMap<FPackageData*, TArray<FPackageData*>>&& RequestGraph, TArray<FWorkerId>& OutAssignments)
{
	int32 AllWorkersIndex = 0;
	int32 NumWorkers = AllWorkers.Num();
	for (FPackageData* Request : Requests)
	{
		OutAssignments.Add(AllWorkers[AllWorkersIndex]);
		AllWorkersIndex = (AllWorkersIndex + 1) % NumWorkers;
	}
}

void LoadBalanceCookBurden(TConstArrayView<FWorkerId> AllWorkers, TArrayView<FPackageData*> Requests,
	TMap<FPackageData*, TArray<FPackageData*>>&& RequestGraph, TArray<FWorkerId>& OutAssignments)
{
	return LoadBalanceStriped(AllWorkers, Requests, MoveTemp(RequestGraph), OutAssignments);
}

}