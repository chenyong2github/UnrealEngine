// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AllocationsProvider.h"
#include "Async/TaskGraphInterfaces.h"
#include "Containers/Queue.h"
#include "HAL/PlatformAtomics.h"

#include <atomic>

namespace TraceServices
{

class ILinearAllocator;

struct FAllocationsImpl
{
	FAllocationsImpl(uint32 NumItems);
	~FAllocationsImpl();

	FAllocationsImpl* Next;
	TArray<const FAllocationItem*> Items;
};

class FAllocationsQuery
{
	friend class FAllocationsQueryAsyncTask;

public:
	FAllocationsQuery(const FAllocationsProvider& InProvider, const IAllocationsProvider::FQueryParams& InParams);

	void Cancel();
	IAllocationsProvider::FQueryStatus Poll();

private:
	void Run();

private:
	const FAllocationsProvider& Provider;

	IAllocationsProvider::FQueryParams Params;

	TQueue<FAllocationsImpl*> Results;

	std::atomic<bool> IsWorking;
	std::atomic<bool> IsCanceling;

	FGraphEventRef CompletedEvent;
};

} // namespace TraceServices
