// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassUpdateISMProcessor.h"
#include "MassVisualizationComponent.h"
#include "MassRepresentationSubsystem.h"
#include "MassEntitySubsystem.h"
#include "MassRepresentationFragments.h"
#include "MassCommonFragments.h"
#include "MassLODTypes.h"
#include "Engine/World.h"

UMassUpdateISMProcessor::UMassUpdateISMProcessor()
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);

	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Representation);
}

void UMassUpdateISMProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

	if (const UWorld* World = Owner.GetWorld())
	{
		for (FMassUpdateISMConfig& Config : ISMConfigs)
		{
			Config.RepresentationSubsystem = Cast<UMassRepresentationSubsystem>(World->GetSubsystemBase(Config.RepresentationSubsystemClass ? Config.RepresentationSubsystemClass : TSubclassOf<UMassRepresentationSubsystem>(UMassRepresentationSubsystem::StaticClass())));
		}
	}
}

void UMassUpdateISMProcessor::ConfigureQueries()
{
	for (FMassUpdateISMConfig& Config : ISMConfigs)
	{
		if (Config.TagFilter.GetScriptStruct())
		{
			Config.EntityQuery.AddTagRequirement(*Config.TagFilter.GetScriptStruct(), EMassFragmentPresence::All);
		}
		Config.EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadOnly);
		Config.EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadWrite);
		Config.EntityQuery.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadWrite);
		Config.EntityQuery.AddChunkRequirement<FMassVisualizationChunkFragment>(EMassFragmentAccess::ReadWrite);
		Config.EntityQuery.SetChunkFilter(&FMassVisualizationChunkFragment::AreAnyEntitiesVisibleInChunk);
	}
}

void UMassUpdateISMProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{

	for (FMassUpdateISMConfig& Config : ISMConfigs)
	{
		check(Config.RepresentationSubsystem);
		FMassInstancedStaticMeshInfoArrayView ISMInfo = Config.RepresentationSubsystem->GetMutableInstancedStaticMeshInfos();

		Config.EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&ISMInfo](FMassExecutionContext& Context)
		{
			const TConstArrayView<FDataFragment_Transform> TransformList = Context.GetFragmentView<FDataFragment_Transform>();
			const TArrayView<FMassRepresentationFragment> RepresentationList = Context.GetMutableFragmentView<FMassRepresentationFragment>();
			const TConstArrayView<FMassRepresentationLODFragment> RepresentationLODList = Context.GetFragmentView<FMassRepresentationLODFragment>();

			const int32 NumEntities = Context.GetNumEntities();
			for (int32 EntityIdx = 0; EntityIdx < NumEntities; EntityIdx++)
			{
				const FDataFragment_Transform& TransformFragment = TransformList[EntityIdx];
				const FMassRepresentationLODFragment& RepresentationLOD = RepresentationLODList[EntityIdx];
				FMassRepresentationFragment& Representation = RepresentationList[EntityIdx];

				if (Representation.CurrentRepresentation == ERepresentationType::StaticMeshInstance)
				{
					UpdateISMTransform(GetTypeHash(Context.GetEntity(EntityIdx)), ISMInfo[Representation.StaticMeshDescIndex], TransformFragment.GetTransform(), Representation.PrevTransform, RepresentationLOD.LODSignificance, Representation.PrevLODSignificance);
				}
				Representation.PrevTransform = TransformFragment.GetTransform();
				Representation.PrevLODSignificance = RepresentationLOD.LODSignificance;
			}
		});
	}
}

void UMassUpdateISMProcessor::UpdateISMTransform(int32 EntityId, FMassInstancedStaticMeshInfo& ISMInfo, const FTransform& Transform, const FTransform& PrevTransform, const float LODSignificance, const float PrevLODSignificance/* = -1.0f*/)
{
	if (ISMInfo.ShouldUseTransformOffset())
	{
		const FTransform& TransformOffset = ISMInfo.GetTransformOffset();
		const FTransform SMTransform = TransformOffset * Transform;
		const FTransform SMPrevTransform = TransformOffset * PrevTransform;

		ISMInfo.AddBatchedTransform(EntityId, SMTransform, SMPrevTransform, LODSignificance, PrevLODSignificance);
	}
	else
	{
		ISMInfo.AddBatchedTransform(EntityId, Transform, PrevTransform, LODSignificance, PrevLODSignificance);
	}
}
