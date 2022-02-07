// Copyright Epic Games, Inc. All Rights Reserved.

#include "Translators/MassCharacterMovementTranslators.h"
#include "Logging/LogMacros.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "VisualLogger/VisualLogger.h"
#include "MassEntitySubsystem.h"
#include "MassCommonTypes.h"
#include "MassMovementFragments.h"
#include "MassSpawnerTypes.h"

//----------------------------------------------------------------------//
//  UMassCharacterMovementToMassTranslator
//----------------------------------------------------------------------//
UMassCharacterMovementToMassTranslator::UMassCharacterMovementToMassTranslator()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::SyncWorldToMass;
	RequiredTags.Add<FMassCharacterMovementCopyToMassTag>();
}

void UMassCharacterMovementToMassTranslator::ConfigureQueries()
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FCharacterMovementComponentWrapperFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassCharacterMovementToMassTranslator::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const TConstArrayView<FCharacterMovementComponentWrapperFragment> ComponentList = Context.GetFragmentView<FCharacterMovementComponentWrapperFragment>();
		const TArrayView<FTransformFragment> LocationList = Context.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FMassVelocityFragment> VelocityList = Context.GetMutableFragmentView<FMassVelocityFragment>();

		const int32 NumEntities = Context.GetNumEntities();
		
		for (int32 i = 0; i < NumEntities; ++i)
		{
			if (const UCharacterMovementComponent* AsMovementComponent = ComponentList[i].Component.Get())
			{
				LocationList[i].GetMutableTransform().SetLocation(AsMovementComponent->GetActorNavLocation());
				
				VelocityList[i].Value = AsMovementComponent->Velocity;
			}
		}
	});
}

//----------------------------------------------------------------------//
//  UMassCharacterMovementToActorTranslator
//----------------------------------------------------------------------//
UMassCharacterMovementToActorTranslator::UMassCharacterMovementToActorTranslator()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::UpdateWorldFromMass;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
	RequiredTags.Add<FMassCharacterMovementCopyToActorTag>();
}

void UMassCharacterMovementToActorTranslator::ConfigureQueries()
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FCharacterMovementComponentWrapperFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
}

void UMassCharacterMovementToActorTranslator::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const TArrayView<FCharacterMovementComponentWrapperFragment> ComponentList = Context.GetMutableFragmentView<FCharacterMovementComponentWrapperFragment>();
		const TConstArrayView<FMassVelocityFragment> VelocityList = Context.GetFragmentView<FMassVelocityFragment>();
		
		const int32 NumEntities = Context.GetNumEntities();

		for (int32 i = 0; i < NumEntities; ++i)
		{
			if (UCharacterMovementComponent* AsMovementComponent = ComponentList[i].Component.Get())
			{
				AsMovementComponent->RequestDirectMove(VelocityList[i].Value, /*bForceMaxSpeed=*/false);
			}
		}
	});
}

//----------------------------------------------------------------------//
//  UMassCharacterOrientationToMassTranslator
//----------------------------------------------------------------------//
UMassCharacterOrientationToMassTranslator::UMassCharacterOrientationToMassTranslator()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::SyncWorldToMass;
	RequiredTags.Add<FMassCharacterOrientationCopyToMassTag>();
}

void UMassCharacterOrientationToMassTranslator::ConfigureQueries()
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FCharacterMovementComponentWrapperFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassCharacterOrientationToMassTranslator::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const TConstArrayView<FCharacterMovementComponentWrapperFragment> ComponentList = Context.GetFragmentView<FCharacterMovementComponentWrapperFragment>();
		const TArrayView<FTransformFragment> LocationList = Context.GetMutableFragmentView<FTransformFragment>();

		const int32 NumEntities = Context.GetNumEntities();
		
		for (int32 i = 0; i < NumEntities; ++i)
		{
			if (const UCharacterMovementComponent* AsMovementComponent = ComponentList[i].Component.Get())
			{
				if (AsMovementComponent->UpdatedComponent != nullptr)
				{
					LocationList[i].GetMutableTransform().SetRotation(AsMovementComponent->UpdatedComponent->GetComponentTransform().GetRotation());
				}
			}
		}
	});
}

//----------------------------------------------------------------------//
//  UMassCharacterOrientationToActorTranslator
//----------------------------------------------------------------------//
UMassCharacterOrientationToActorTranslator::UMassCharacterOrientationToActorTranslator()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::UpdateWorldFromMass;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
	RequiredTags.Add<FMassCharacterOrientationCopyToActorTag>();
}

void UMassCharacterOrientationToActorTranslator::ConfigureQueries()
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FCharacterMovementComponentWrapperFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
}

void UMassCharacterOrientationToActorTranslator::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const TArrayView<FCharacterMovementComponentWrapperFragment> ComponentList = Context.GetMutableFragmentView<FCharacterMovementComponentWrapperFragment>();
		const TConstArrayView<FTransformFragment> TransformList = Context.GetFragmentView<FTransformFragment>();
		
		const int32 NumEntities = Context.GetNumEntities();

		for (int32 i = 0; i < NumEntities; ++i)
		{
			if (UCharacterMovementComponent* AsMovementComponent = ComponentList[i].Component.Get())
			{
				if (AsMovementComponent->UpdatedComponent != nullptr)
				{
					const FTransformFragment& Transform = TransformList[i];
					AsMovementComponent->bOrientRotationToMovement = false;
					AsMovementComponent->UpdatedComponent->SetWorldRotation(Transform.GetTransform().GetRotation());
				}
			}
		}
	});
}
