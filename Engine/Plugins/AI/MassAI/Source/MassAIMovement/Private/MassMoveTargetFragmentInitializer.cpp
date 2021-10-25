// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassMoveTargetFragmentInitializer.h"
#include "MassCommonFragments.h"
#include "MassAIMovementFragments.h"
#include "MassSimulationLOD.h"


UMassMoveTargetFragmentInitializer::UMassMoveTargetFragmentInitializer()
{
	FragmentType = FMassMoveTargetFragment::StaticStruct();
}

void UMassMoveTargetFragmentInitializer::ConfigureQueries()
{
	InitializerQuery.AddRequirement<FMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	InitializerQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadOnly);
}

void UMassMoveTargetFragmentInitializer::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	InitializerQuery.ForEachEntityChunk(EntitySubsystem, Context, [](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();
		TArrayView<FMassMoveTargetFragment> MoveTargetList = Context.GetMutableFragmentView<FMassMoveTargetFragment>();
		const TConstArrayView<FDataFragment_Transform> LocationList = Context.GetFragmentView<FDataFragment_Transform>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			FMassMoveTargetFragment& MoveTarget = MoveTargetList[EntityIndex];
			const FDataFragment_Transform& Location = LocationList[EntityIndex];

			MoveTarget.Center = Location.GetTransform().GetLocation();
			MoveTarget.Forward = Location.GetTransform().GetRotation().Vector();
			MoveTarget.DistanceToGoal = 0.0f;
			MoveTarget.SlackRadius = 0.0f;
		}
	});
}
