// Copyright Epic Games, Inc. All Rights Reserved.

#include "SourceFilterManager.h"

#include "DataSourceFilter.h"
#include "UObject/Class.h"
#include "UObject/UObjectBase.h"
#include "UObject/Object.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Async/ParallelFor.h"

#include "TraceFilter.h"
#include "TraceSourceFiltering.h"
#include "DrawDebugHelpers.h"
#include "TraceSourceFilteringSettings.h"
#include "SourceFilterCollection.h"

FSourceFilterManager::FSourceFilterManager(UWorld* InWorld) : World(InWorld)
{
	ActorSpawningDelegateHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([this](AActor* InActor) 
	{ 
		if (CAN_TRACE_OBJECT(World))
		{
			ApplyFilters(InActor);
		}
	}));

	Settings = FTraceSourceFiltering::Get().GetSettings();
	FilterCollection = FTraceSourceFiltering::Get().GetFilterCollection();
}

FSourceFilterManager::~FSourceFilterManager()
{
	if (World)
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawningDelegateHandle);
	}
}

void FSourceFilterManager::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FSourceFilterManager::Tick);
	if (CAN_TRACE_OBJECT(World))
	{		
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (const AActor* Actor = *It)
			{
				ApplyFilters(Actor);
			}
		}
	}
}

void FSourceFilterManager::ApplyFilters(const AActor* Actor)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FSourceFilterManager::ApplyFilters);
	const TArray<UDataSourceFilter*>& Filters = FilterCollection->GetFilters();
	bool bPassesFilters = Filters.Num() ? false : true;
	bool bAnyEnabledFilter = false;

	FFilterLedger& Ledger = FFilterLedger::Get();
	Ledger.RejectedFilters.Empty();

	for (const UDataSourceFilter* Filter : Filters)
	{
		if (Filter->IsEnabled())
		{
			bAnyEnabledFilter = true;
			const bool bPassesFilter = Filter->DoesActorPassFilter(Actor);
			bPassesFilters |= bPassesFilter;

			if (!bPassesFilter)
			{
				Ledger.RejectedFilters.Add(Filter);
			}
		}
	}

	bPassesFilters = bPassesFilters ? bPassesFilters : !bAnyEnabledFilter;
	SET_OBJECT_TRACEABLE(Actor, bPassesFilters);
	
	// Debug-purpose drawing, allowing users to see impact of Filter set
	if (Settings->bDrawFilteringStates && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE))
	{
		FVector Origin, Extent;
		Actor->GetActorBounds(false, Origin, Extent);
		if (Settings->bDrawOnlyPassingActors)
		{
			if (bPassesFilters)
			{
				DrawDebugBox(World, Origin, Extent, FQuat::Identity, FColor::Green, false, -1.f, 0, 1.f);
			}			
		}
		else
		{
			DrawDebugBox(World, Origin, Extent, FQuat::Identity, bPassesFilters ? FColor::Green : FColor::Red, false, -1.f, 0, 1.f);

			if(!bPassesFilters && Settings->bDrawFilterDescriptionForRejectedActors)
			{
				FString ActorString;

				for (const UDataSourceFilter* Filter : Ledger.RejectedFilters)
				{
					if (Filter && !Filter->IsA<UDataSourceFilterSet>())
					{
						FText Text;
						Filter->Execute_GetDisplayText(Filter, Text);
						ActorString += Text.ToString();
						ActorString += TEXT("\n");
					}
				}
				
				DrawDebugString(World, FVector::ZeroVector/*Actor->GetActorLocation()*/, ActorString, (AActor*)Actor, FColor::Red, 0.f);
			}
		}
	}
}

TStatId FSourceFilterManager::GetStatId() const
{
	return TStatId();
}

