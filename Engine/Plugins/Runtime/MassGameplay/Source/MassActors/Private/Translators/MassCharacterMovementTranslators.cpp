// Copyright Epic Games, Inc. All Rights Reserved.

#include "Translators/MassCharacterMovementTranslators.h"
#include "Logging/LogMacros.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "VisualLogger/VisualLogger.h"
#include "MassEntitySubsystem.h"
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
	EntityQuery.AddRequirement<FDataFragment_CharacterMovementComponentWrapper>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassCharacterMovementToMassTranslator::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const TConstArrayView<FDataFragment_CharacterMovementComponentWrapper> ComponentList = Context.GetFragmentView<FDataFragment_CharacterMovementComponentWrapper>();
		const TArrayView<FDataFragment_Transform> LocationList = Context.GetMutableFragmentView<FDataFragment_Transform>();
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
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Avoidance);
	RequiredTags.Add<FMassCharacterMovementCopyToActorTag>();
}

void UMassCharacterMovementToActorTranslator::ConfigureQueries()
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FDataFragment_CharacterMovementComponentWrapper>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadOnly);
}

void UMassCharacterMovementToActorTranslator::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const TArrayView<FDataFragment_CharacterMovementComponentWrapper> ComponentList = Context.GetMutableFragmentView<FDataFragment_CharacterMovementComponentWrapper>();
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
	EntityQuery.AddRequirement<FDataFragment_CharacterMovementComponentWrapper>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadWrite);
}

void UMassCharacterOrientationToMassTranslator::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const TConstArrayView<FDataFragment_CharacterMovementComponentWrapper> ComponentList = Context.GetFragmentView<FDataFragment_CharacterMovementComponentWrapper>();
		const TArrayView<FDataFragment_Transform> LocationList = Context.GetMutableFragmentView<FDataFragment_Transform>();

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
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Avoidance);
	RequiredTags.Add<FMassCharacterOrientationCopyToActorTag>();
}

void UMassCharacterOrientationToActorTranslator::ConfigureQueries()
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FDataFragment_CharacterMovementComponentWrapper>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadOnly);
}

void UMassCharacterOrientationToActorTranslator::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const TArrayView<FDataFragment_CharacterMovementComponentWrapper> ComponentList = Context.GetMutableFragmentView<FDataFragment_CharacterMovementComponentWrapper>();
		const TConstArrayView<FDataFragment_Transform> TransformList = Context.GetFragmentView<FDataFragment_Transform>();
		
		const int32 NumEntities = Context.GetNumEntities();

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
	EntityQuery.AddRequirement<FDataFragment_NavLocation>(EMassFragmentAccess::ReadWrite);
}

void UMassFragmentInitializer_NavLocation::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
		{
			const TArrayView<FDataFragment_NavLocation> NavLocationList = Context.GetMutableFragmentView<FDataFragment_NavLocation>();
			const int32 NumEntities = Context.GetNumEntities();
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
	EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadWrite);
}

void UMassFragmentInitializer_Transform::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
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
		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&NumRequiredSpawnTransforms](const FMassExecutionContext& Context)
			{
				NumRequiredSpawnTransforms += Context.GetNumEntities();
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

		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&Transforms, this](FMassExecutionContext& Context)
			{
				const TArrayView<FDataFragment_Transform> LocationList = Context.GetMutableFragmentView<FDataFragment_Transform>();
				const int32 NumEntities = Context.GetNumEntities();
				for (int32 i = 0; i < NumEntities; ++i)
				{
					const int32 AuxIndex = FMath::RandRange(0, Transforms.Num() - 1);
					LocationList[i].GetMutableTransform() = Transforms[AuxIndex];
					Transforms.RemoveAtSwap(AuxIndex, 1, /*bAllowShrinking=*/false);
				}
			});
	}
}
