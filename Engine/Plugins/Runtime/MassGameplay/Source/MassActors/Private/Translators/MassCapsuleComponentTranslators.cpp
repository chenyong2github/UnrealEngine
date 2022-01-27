// Copyright Epic Games, Inc. All Rights Reserved.

#include "Translators/MassCapsuleComponentTranslators.h"
#include "MassCommonTypes.h"
#include "MassEntitySubsystem.h"
#include "Components/CapsuleComponent.h"


//----------------------------------------------------------------------//
// UMassCapsuleTransformToMassTranslator
//----------------------------------------------------------------------//
UMassCapsuleTransformToMassTranslator::UMassCapsuleTransformToMassTranslator()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	RequiredTags.Add<FMassCapsuleTransformCopyToMassTag>();
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::SyncWorldToMass;
}

void UMassCapsuleTransformToMassTranslator::ConfigureQueries()
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FDataFragment_CapsuleComponentWrapper>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadWrite);
}

void UMassCapsuleTransformToMassTranslator::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
		{
			const TConstArrayView<FDataFragment_CapsuleComponentWrapper> CapsuleComponentList = Context.GetFragmentView<FDataFragment_CapsuleComponentWrapper>();
			const TArrayView<FDataFragment_Transform> LocationList = Context.GetMutableFragmentView<FDataFragment_Transform>();
			for (int i = 0; i < CapsuleComponentList.Num(); ++i)
			{
				if (const UCapsuleComponent* CapsuleComp = CapsuleComponentList[i].Component.Get())
				{
					LocationList[i].GetMutableTransform() = CapsuleComp->GetComponentTransform();
				}
			}
		});
}

//----------------------------------------------------------------------//
// UMassTransformToActorCapsuleTranslator
//----------------------------------------------------------------------//
UMassTransformToActorCapsuleTranslator::UMassTransformToActorCapsuleTranslator()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	RequiredTags.Add<FMassCapsuleTransformCopyToActorTag>();
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::UpdateWorldFromMass;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
}

void UMassTransformToActorCapsuleTranslator::ConfigureQueries()
{
	AddRequiredTagsToQuery(EntityQuery);
	EntityQuery.AddRequirement<FDataFragment_CapsuleComponentWrapper>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadOnly);
}

void UMassTransformToActorCapsuleTranslator::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
		{
			const TArrayView<FDataFragment_CapsuleComponentWrapper> CapsuleComponentList = Context.GetMutableFragmentView<FDataFragment_CapsuleComponentWrapper>();
			const TConstArrayView<FDataFragment_Transform> LocationList = Context.GetFragmentView<FDataFragment_Transform>();
			for (int i = 0; i < CapsuleComponentList.Num(); ++i)
			{
				if (UCapsuleComponent* CapsuleComp = CapsuleComponentList[i].Component.Get())
				{
					CapsuleComp->SetWorldTransform(LocationList[i].GetTransform(), /*bSweep=*/false);
				}
			}
		});
}
