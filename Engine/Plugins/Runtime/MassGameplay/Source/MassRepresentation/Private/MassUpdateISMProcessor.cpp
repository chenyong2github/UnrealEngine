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

	RepresentationSubsystem = UWorld::GetSubsystem<UMassRepresentationSubsystem>(Owner.GetWorld());
}

void UMassUpdateISMProcessor::ConfigureQueries()
{
	for (const FInstancedStruct& TagFilter : TagFilters)
	{
		FMassEntityQuery& EntityQuery = EntityQueries.AddDefaulted_GetRef();
		if (TagFilter.GetScriptStruct())
		{
			EntityQuery.AddTagRequirement(*TagFilter.GetScriptStruct(), EMassFragmentPresence::All);
		}
		EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadOnly);
		EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadWrite);
		EntityQuery.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadWrite);
		EntityQuery.AddChunkRequirement<FMassVisualizationChunkFragment>(EMassFragmentAccess::ReadWrite);
		EntityQuery.SetChunkFilter(&FMassVisualizationChunkFragment::AreAnyEntitiesVisibleInChunk);
	}
}

void UMassUpdateISMProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	check(RepresentationSubsystem);
	FMassInstancedStaticMeshInfoArrayView ISMInfo = RepresentationSubsystem->GetMutableInstancedStaticMeshInfos();

	for (FMassEntityQuery& EntityQuery : EntityQueries)
	{
		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&ISMInfo](FMassExecutionContext& Context)
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
