// Copyright Epic Games, Inc. All Rights Reserved.

#include "Translators/MassCharacterMovementTranslators.h"
#include "Logging/LogMacros.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "VisualLogger/VisualLogger.h"
#include "MassEntitySystem.h"
#include "MassCommonTypes.h"
#include "MassMovementTypes.h"
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
	EntityQuery.AddRequirement<FDataFragment_CharacterMovementComponentWrapper>(ELWComponentAccess::ReadOnly);
	EntityQuery.AddRequirement<FDataFragment_Transform>(ELWComponentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassVelocityFragment>(ELWComponentAccess::ReadWrite);
}

void UMassCharacterMovementToMassTranslator::Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FLWComponentSystemExecutionContext& Context)
	{
		const TConstArrayView<FDataFragment_CharacterMovementComponentWrapper> ComponentList = Context.GetComponentView<FDataFragment_CharacterMovementComponentWrapper>();
		const TArrayView<FDataFragment_Transform> LocationList = Context.GetMutableComponentView<FDataFragment_Transform>();
		const TArrayView<FMassVelocityFragment> VelocityList = Context.GetMutableComponentView<FMassVelocityFragment>();

		const int32 NumEntities = Context.GetEntitiesNum();
		
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
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Avoidance);
	RequiredTags.Add<FMassCharacterMovementCopyToActorTag>();
}

void UMassCharacterMovementToActorTranslator::ConfigureQueries()
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FDataFragment_CharacterMovementComponentWrapper>(ELWComponentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassVelocityFragment>(ELWComponentAccess::ReadOnly);
}

void UMassCharacterMovementToActorTranslator::Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FLWComponentSystemExecutionContext& Context)
	{
		const TArrayView<FDataFragment_CharacterMovementComponentWrapper> ComponentList = Context.GetMutableComponentView<FDataFragment_CharacterMovementComponentWrapper>();
		const TConstArrayView<FMassVelocityFragment> VelocityList = Context.GetComponentView<FMassVelocityFragment>();
		
		const int32 NumEntities = Context.GetEntitiesNum();

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
	EntityQuery.AddRequirement<FDataFragment_CharacterMovementComponentWrapper>(ELWComponentAccess::ReadOnly);
	EntityQuery.AddRequirement<FDataFragment_Transform>(ELWComponentAccess::ReadWrite);
}

void UMassCharacterOrientationToMassTranslator::Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FLWComponentSystemExecutionContext& Context)
	{
		const TConstArrayView<FDataFragment_CharacterMovementComponentWrapper> ComponentList = Context.GetComponentView<FDataFragment_CharacterMovementComponentWrapper>();
		const TArrayView<FDataFragment_Transform> LocationList = Context.GetMutableComponentView<FDataFragment_Transform>();

		const int32 NumEntities = Context.GetEntitiesNum();
		
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
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Avoidance);
	RequiredTags.Add<FMassCharacterOrientationCopyToActorTag>();
}

void UMassCharacterOrientationToActorTranslator::ConfigureQueries()
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FDataFragment_CharacterMovementComponentWrapper>(ELWComponentAccess::ReadWrite);
	EntityQuery.AddRequirement<FDataFragment_Transform>(ELWComponentAccess::ReadOnly);
}

void UMassCharacterOrientationToActorTranslator::Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FLWComponentSystemExecutionContext& Context)
	{
		const TArrayView<FDataFragment_CharacterMovementComponentWrapper> ComponentList = Context.GetMutableComponentView<FDataFragment_CharacterMovementComponentWrapper>();
		const TConstArrayView<FDataFragment_Transform> TransformList = Context.GetComponentView<FDataFragment_Transform>();
		
		const int32 NumEntities = Context.GetEntitiesNum();

		for (int32 i = 0; i < NumEntities; ++i)
		{
			if (UCharacterMovementComponent* AsMovementComponent = ComponentList[i].Component.Get())
			{
				if (AsMovementComponent->UpdatedComponent != nullptr)
				{
					const FDataFragment_Transform& Transform = TransformList[i];
					AsMovementComponent->bOrientRotationToMovement = false;
					AsMovementComponent->UpdatedComponent->SetWorldRotation(Transform.GetTransform().GetRotation());
				}
			}
		}
	});
}

//----------------------------------------------------------------------//
// UMassFragmentInitializer_NavLocation 
//----------------------------------------------------------------------//
UMassFragmentInitializer_NavLocation::UMassFragmentInitializer_NavLocation()
{
	FragmentType = FDataFragment_NavLocation::StaticStruct();
}

void UMassFragmentInitializer_NavLocation::ConfigureQueries() 
{
	EntityQuery.AddRequirement<FDataFragment_NavLocation>(ELWComponentAccess::ReadWrite);
}

void UMassFragmentInitializer_NavLocation::Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FLWComponentSystemExecutionContext& Context)
		{
			const TArrayView<FDataFragment_NavLocation> NavLocationList = Context.GetMutableComponentView<FDataFragment_NavLocation>();
			const int32 NumEntities = Context.GetEntitiesNum();
			for (int32 i = 0; i < NumEntities; ++i)
			{
				NavLocationList[i].NodeRef = INVALID_NAVNODEREF;
			}
		});
}

//----------------------------------------------------------------------//
// UMassFragmentInitializer_Transform 
//----------------------------------------------------------------------//
UMassFragmentInitializer_Transform::UMassFragmentInitializer_Transform()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	FragmentType = FDataFragment_Transform::StaticStruct();
}

void UMassFragmentInitializer_Transform::ConfigureQueries()
{
	EntityQuery.AddRequirement<FDataFragment_Transform>(ELWComponentAccess::ReadWrite);
}

void UMassFragmentInitializer_Transform::Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
	const UWorld* World = EntitySubsystem.GetWorld();

	check(World);

	const ENetMode NetMode = World->GetNetMode();

	if (NetMode != NM_Client)
	{
		const FInstancedStruct& AuxInput = Context.GetMutableAuxData();
		if (!(AuxInput.IsValid() && AuxInput.GetScriptStruct()->IsChildOf(FMassSpawnAuxData::StaticStruct())))
		{
			UE_VLOG_UELOG(this, LogMass, Log, TEXT("Execution context has invalid AuxData or it's not FMassSpawnAuxData. Entity transforms won't be initialized."));
			return;
		}

		TArray<FTransform>& Transforms = AuxInput.GetMutable<FMassSpawnAuxData>().Transforms;

		const int32 NumSpawnTransforms = Transforms.Num();
		if (NumSpawnTransforms == 0)
		{
			UE_VLOG_UELOG(this, LogMass, Error, TEXT("No spawn transforms provided. Entity transforms won't be initialized."));
			return;
		}

		int32 NumRequiredSpawnTransforms = 0;
		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&NumRequiredSpawnTransforms](const FLWComponentSystemExecutionContext& Context)
			{
				NumRequiredSpawnTransforms += Context.GetEntitiesNum();
			});

		const int32 NumToAdd = NumRequiredSpawnTransforms - NumSpawnTransforms;
		if (NumToAdd > 0)
		{
			UE_VLOG_UELOG(this, LogMass, Warning,
				TEXT("Not enough spawn locations provided (%d) for all entities (%d). Existing locations will be reused randomly to fill the %d missing positions."),
				NumSpawnTransforms, NumRequiredSpawnTransforms, NumToAdd);

			Transforms.AddUninitialized(NumToAdd);
			for (int i = 0; i < NumToAdd; ++i)
			{
				Transforms[NumSpawnTransforms + i] = Transforms[FMath::RandRange(0, NumSpawnTransforms - 1)];
			}
		}

		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&Transforms, this](FLWComponentSystemExecutionContext& Context)
			{
				const TArrayView<FDataFragment_Transform> LocationList = Context.GetMutableComponentView<FDataFragment_Transform>();
				const int32 NumEntities = Context.GetEntitiesNum();
				for (int32 i = 0; i < NumEntities; ++i)
				{
					const int32 AuxIndex = FMath::RandRange(0, Transforms.Num() - 1);
					LocationList[i].GetMutableTransform() = Transforms[AuxIndex];
					Transforms.RemoveAtSwap(AuxIndex, 1, /*bAllowShrinking=*/false);
				}
			});
	}
}
