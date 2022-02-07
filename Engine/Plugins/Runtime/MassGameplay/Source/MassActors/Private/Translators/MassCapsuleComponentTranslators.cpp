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
	EntityQuery.AddRequirement<FCapsuleComponentWrapperFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassCapsuleTransformToMassTranslator::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
		{
			const TConstArrayView<FCapsuleComponentWrapperFragment> CapsuleComponentList = Context.GetFragmentView<FCapsuleComponentWrapperFragment>();
			const TArrayView<FTransformFragment> LocationList = Context.GetMutableFragmentView<FTransformFragment>();
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
	EntityQuery.AddRequirement<FCapsuleComponentWrapperFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
}

void UMassTransformToActorCapsuleTranslator::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
		{
			const TArrayView<FCapsuleComponentWrapperFragment> CapsuleComponentList = Context.GetMutableFragmentView<FCapsuleComponentWrapperFragment>();
			const TConstArrayView<FTransformFragment> LocationList = Context.GetFragmentView<FTransformFragment>();
			for (int i = 0; i < CapsuleComponentList.Num(); ++i)
			{
				if (UCapsuleComponent* CapsuleComp = CapsuleComponentList[i].Component.Get())
				{
					CapsuleComp->SetWorldTransform(LocationList[i].GetTransform(), /*bSweep=*/false);
				}
			}
		});
}
