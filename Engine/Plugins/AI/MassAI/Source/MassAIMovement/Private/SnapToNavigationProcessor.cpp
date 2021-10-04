// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnapToNavigationProcessor.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "AI/Navigation/NavigationTypes.h"
#include "MassAIMovementTypes.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

//----------------------------------------------------------------------//
// USnapToNavigationProcessor 
//----------------------------------------------------------------------//
USnapToNavigationProcessor::USnapToNavigationProcessor()
{
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Avoidance);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::UpdateWorldFromMass);
}

void USnapToNavigationProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FDataFragment_NavLocation>(EMassFragmentAccess::ReadWrite);
}

void USnapToNavigationProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	ANavigationData* NavData = WeakNavData.Get();
	if (NavData == nullptr)
	{
		return;
	}

	QUICK_SCOPE_CYCLE_COUNTER(UMovementProcessor_Run);

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this, NavData](FMassExecutionContext& Context)
		{
			const int32 NumEntities = Context.GetNumEntities();
			const TArrayView<FDataFragment_Transform> LocationList = Context.GetMutableComponentView<FDataFragment_Transform>();
			const TArrayView<FDataFragment_NavLocation> NavLocationList = Context.GetMutableComponentView<FDataFragment_NavLocation>();

			TArray<FNavigationProjectionWork> Workload;
			for (int32 Index = 0; Index < NumEntities; ++Index)
			{
				Workload.Add(FNavigationProjectionWork(FNavLocation(LocationList[Index].GetTransform().GetLocation(), NavLocationList[Index].NodeRef)));
			}

			NavData->BatchProjectPoints(Workload, NavData->GetDefaultQueryExtent());

			for (int32 Index = 0; Index < NumEntities; ++Index)
			{
				LocationList[Index].GetMutableTransform().SetLocation(Workload[Index].OutLocation.Location);
				NavLocationList[Index].NodeRef = Workload[Index].OutLocation.NodeRef;
			}
		});
}

void USnapToNavigationProcessor::Initialize(UObject& InOwner)
{
	Super::Initialize(InOwner);

	ANavigationData* NavData = Cast<ANavigationData>(&InOwner);
	if (NavData == nullptr)
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(InOwner.GetWorld());
		NavData = NavSys ? NavSys->GetDefaultNavDataInstance() : nullptr;
	}
	WeakNavData = NavData;
}
