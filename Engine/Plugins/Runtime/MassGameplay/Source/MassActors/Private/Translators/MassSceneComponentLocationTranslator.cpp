// Copyright Epic Games, Inc. All Rights Reserved.

#include "Translators/MassSceneComponentLocationTranslator.h"
#include "MassMovementTypes.h"
#include "Components/SceneComponent.h"
#include "MassEntitySubsystem.h"

//----------------------------------------------------------------------//
//  UMassSceneComponentLocationToMassTranslator
//----------------------------------------------------------------------//
UMassSceneComponentLocationToMassTranslator::UMassSceneComponentLocationToMassTranslator()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::SyncWorldToMass;
	RequiredTags.Add<FMassSceneComponentLocationCopyToMassTag>();
}

void UMassSceneComponentLocationToMassTranslator::ConfigureQueries()
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FMassSceneComponentWrapperFragment>(ELWComponentAccess::ReadOnly);
	EntityQuery.AddRequirement<FDataFragment_Transform>(ELWComponentAccess::ReadWrite);
}

void UMassSceneComponentLocationToMassTranslator::Execute(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FLWComponentSystemExecutionContext& Context)
	{
		const TConstArrayView<FMassSceneComponentWrapperFragment> ComponentList = Context.GetComponentView<FMassSceneComponentWrapperFragment>();
		const TArrayView<FDataFragment_Transform> LocationList = Context.GetMutableComponentView<FDataFragment_Transform>();

		const int32 NumEntities = Context.GetEntitiesNum();
		for (int32 i = 0; i < NumEntities; ++i)
		{
			if (const USceneComponent* AsComponent = ComponentList[i].Component.Get())
			{
				LocationList[i].GetMutableTransform().SetLocation(AsComponent->GetComponentTransform().GetLocation() - FVector(0.f, 0.f, AsComponent->Bounds.BoxExtent.Z));
			}
		}
	});
}

//----------------------------------------------------------------------//
//  UMassSceneComponentLocationToActorTranslator
//----------------------------------------------------------------------//
UMassSceneComponentLocationToActorTranslator::UMassSceneComponentLocationToActorTranslator()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::UpdateWorldFromMass;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Avoidance);
	RequiredTags.Add<FMassSceneComponentLocationCopyToActorTag>();
}

void UMassSceneComponentLocationToActorTranslator::ConfigureQueries()
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FMassSceneComponentWrapperFragment>(ELWComponentAccess::ReadOnly);
	EntityQuery.AddRequirement<FDataFragment_Transform>(ELWComponentAccess::ReadWrite);
}

void UMassSceneComponentLocationToActorTranslator::Execute(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FLWComponentSystemExecutionContext& Context)
		{
			const TConstArrayView<FMassSceneComponentWrapperFragment> ComponentList = Context.GetComponentView<FMassSceneComponentWrapperFragment>();
			const TArrayView<FDataFragment_Transform> LocationList = Context.GetMutableComponentView<FDataFragment_Transform>();

			const int32 NumEntities = Context.GetEntitiesNum();
			for (int32 i = 0; i < NumEntities; ++i)
			{
				if (USceneComponent* AsComponent = ComponentList[i].Component.Get())
				{
					AsComponent->SetWorldLocation(LocationList[i].GetTransform().GetLocation() + FVector(0.f, 0.f, AsComponent->Bounds.BoxExtent.Z));
				}
			}
		});
}