// Copyright Epic Games, Inc. All Rights Reserved.

#include "Translators/MassSceneComponentLocationTranslator.h"
#include "MassCommonTypes.h"
#include "Components/SceneComponent.h"
#include "MassEntitySubsystem.h"
#include "MassCommonTypes.h"

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
	EntityQuery.AddRequirement<FMassSceneComponentWrapperFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassSceneComponentLocationToMassTranslator::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const TConstArrayView<FMassSceneComponentWrapperFragment> ComponentList = Context.GetFragmentView<FMassSceneComponentWrapperFragment>();
		const TArrayView<FTransformFragment> LocationList = Context.GetMutableFragmentView<FTransformFragment>();

		const int32 NumEntities = Context.GetNumEntities();
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
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
	RequiredTags.Add<FMassSceneComponentLocationCopyToActorTag>();
}

void UMassSceneComponentLocationToActorTranslator::ConfigureQueries()
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FMassSceneComponentWrapperFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassSceneComponentLocationToActorTranslator::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
		{
			const TConstArrayView<FMassSceneComponentWrapperFragment> ComponentList = Context.GetFragmentView<FMassSceneComponentWrapperFragment>();
			const TArrayView<FTransformFragment> LocationList = Context.GetMutableFragmentView<FTransformFragment>();

			const int32 NumEntities = Context.GetNumEntities();
			for (int32 i = 0; i < NumEntities; ++i)
			{
				if (USceneComponent* AsComponent = ComponentList[i].Component.Get())
				{
					AsComponent->SetWorldLocation(LocationList[i].GetTransform().GetLocation() + FVector(0.f, 0.f, AsComponent->Bounds.BoxExtent.Z));
				}
			}
		});
}