// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassStationaryISMSwitcherProcessor.h"
#include "MassRepresentationSubsystem.h"
#include "MassCommonFragments.h"
#include "MassRepresentationProcessor.h"


UMassStationaryISMSwitcherProcessor::UMassStationaryISMSwitcherProcessor(const FObjectInitializer& ObjectInitializer)
	: EntityQuery(*this)
{
	ExecutionOrder.ExecuteAfter.Add(UMassVisualizationProcessor::StaticClass()->GetFName());
	bAutoRegisterWithProcessingPhases = true;
}

void UMassStationaryISMSwitcherProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddSharedRequirement<FMassRepresentationSubsystemSharedFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FMassStaticRepresentationTag>(EMassFragmentPresence::All);
}

void UMassStationaryISMSwitcherProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [this](FMassExecutionContext& Context)
	{
		UMassRepresentationSubsystem* RepresentationSubsystem = Context.GetMutableSharedFragment<FMassRepresentationSubsystemSharedFragment>().RepresentationSubsystem;
		check(RepresentationSubsystem);
		FMassInstancedStaticMeshInfoArrayView ISMInfosView = RepresentationSubsystem->GetMutableInstancedStaticMeshInfos();

		const TConstArrayView<FMassRepresentationLODFragment> RepresentationLODList = Context.GetFragmentView<FMassRepresentationLODFragment>();
		const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
		const TArrayView<FMassRepresentationFragment> RepresentationList = Context.GetMutableFragmentView<FMassRepresentationFragment>();

		const int32 NumEntities = Context.GetNumEntities();
		for (int32 EntityIdx = 0; EntityIdx < NumEntities; EntityIdx++)
		{
			const FMassEntityHandle EntityHandle = Context.GetEntity(EntityIdx);
			const FMassRepresentationLODFragment& RepresentationLOD = RepresentationLODList[EntityIdx];
			const FTransformFragment& TransformFragment = TransformList[EntityIdx];
			FMassRepresentationFragment& Representation = RepresentationList[EntityIdx];

			if (Representation.PrevRepresentation != EMassRepresentationType::StaticMeshInstance
				&& Representation.CurrentRepresentation != EMassRepresentationType::StaticMeshInstance)
			{
				// nothing to do here
				continue;
			}

			FMassInstancedStaticMeshInfo& ISMInfo = ISMInfosView[Representation.StaticMeshDescIndex];

			if (Representation.PrevRepresentation == EMassRepresentationType::StaticMeshInstance
				&& Representation.CurrentRepresentation != EMassRepresentationType::StaticMeshInstance)
			{
				const int32 EntityId = GetTypeHash(EntityHandle);

				// note that we're using the PrevLODSignificance here, and the reason for it is that the Prev value matches the 
				// PrevRepresentation - thus we need to remove from the "previously" used LODSignificance range.
				ISMInfo.RemoveInstance(EntityId, Representation.PrevLODSignificance);

				// consume "prev" data
				Representation.PrevRepresentation = Representation.CurrentRepresentation;
			}
			else if (Representation.PrevRepresentation != EMassRepresentationType::StaticMeshInstance
				&& Representation.CurrentRepresentation == EMassRepresentationType::StaticMeshInstance)
			{
				const int32 EntityId = GetTypeHash(EntityHandle);
				const FTransform& Transform = TransformFragment.GetTransform();
				const FTransform& PrevTransform = Representation.PrevTransform;
				const float LODSignificance = RepresentationLOD.LODSignificance;
				const float PrevLODSignificance = Representation.PrevLODSignificance;
				
				if (FMassLODSignificanceRange* NewRange = ISMInfo.GetLODSignificanceRange(RepresentationLOD.LODSignificance))
				{
					if (ISMInfo.ShouldUseTransformOffset())
					{
						const FTransform& TransformOffset = ISMInfo.GetTransformOffset();
						const FTransform SMTransform = TransformOffset * Transform;
						NewRange->AddInstance(EntityId, SMTransform);
					}
					else
					{
						NewRange->AddInstance(EntityId, Transform);
					}
				}

				// consume "prev" data
				Representation.PrevRepresentation = Representation.CurrentRepresentation;
			}
			else if (ISMInfo.GetLODSignificanceRangesNum() > 1 && Representation.PrevLODSignificance != RepresentationLOD.LODSignificance)
			{
				// we remain in ISM land, but LODSignificance changed and we have multiple LODSignificance ranges for this entity
				FMassLODSignificanceRange* OldRange = ISMInfo.GetLODSignificanceRange(Representation.PrevLODSignificance);
				FMassLODSignificanceRange* NewRange = ISMInfo.GetLODSignificanceRange(RepresentationLOD.LODSignificance);
				if (OldRange != NewRange)
				{
					const int32 EntityId = GetTypeHash(EntityHandle);
					if (OldRange)
					{
						OldRange->RemoveInstance(EntityId);
					}
					if (NewRange)
					{
						const FTransform& Transform = TransformFragment.GetTransform();
						NewRange->AddInstance(EntityId, Transform);
					}
				}
			}

			// consume "prev" data
			Representation.PrevLODSignificance = RepresentationLOD.LODSignificance;
		}
	});
}
