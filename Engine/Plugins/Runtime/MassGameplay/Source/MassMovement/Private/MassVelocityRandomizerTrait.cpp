// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassVelocityRandomizerTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "Engine/World.h"
#include "MassMovementFragments.h"


//----------------------------------------------------------------------//
//  UMassVelocityRandomizerTrait
//----------------------------------------------------------------------//
void UMassVelocityRandomizerTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	BuildContext.AddFragment<FMassVelocityFragment>();
	
	UMassRandomVelocityInitializer* VelocityInitializer = NewObject<UMassRandomVelocityInitializer>(&World);
	check(VelocityInitializer);
	VelocityInitializer->SetParameters(MinSpeed, MaxSpeed, bSetZComponent);
	BuildContext.AddInitializer(*VelocityInitializer);
}

//----------------------------------------------------------------------//
//  UMassRandomVelocityInitializer
//----------------------------------------------------------------------//
UMassRandomVelocityInitializer::UMassRandomVelocityInitializer()
{
	FragmentType = FMassVelocityFragment::StaticStruct();
}

void UMassRandomVelocityInitializer::SetParameters(const float InMinSpeed, const float InMaxSpeed, const bool bInSetZComponent)
{
	MinSpeed = InMinSpeed;
	MaxSpeed = InMaxSpeed;
	bSetZComponent = bInSetZComponent;
}

void UMassRandomVelocityInitializer::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassVelocityFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassRandomVelocityInitializer::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	// note: the author is aware that the vectors produced below are not distributed uniformly, but it's good enough
	if (bSetZComponent)
	{
		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, ([this](FMassExecutionContext& Context)
			{
				const TArrayView<FMassVelocityFragment> VelocitiesList = Context.GetMutableFragmentView<FMassVelocityFragment>();
				for (FMassVelocityFragment& VelocityFragment : VelocitiesList)
				{
					const FVector RandomVector = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)).GetSafeNormal();

					VelocityFragment.Value = RandomVector * FMath::FRandRange(MinSpeed, MaxSpeed);
				}
			}));
	}
	else
	{
		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, ([this](FMassExecutionContext& Context)
			{
				const TArrayView<FMassVelocityFragment> VelocitiesList = Context.GetMutableFragmentView<FMassVelocityFragment>();
				for (FMassVelocityFragment& VelocityFragment : VelocitiesList)
				{
					const FVector RandomVector = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0).GetSafeNormal2D();

					VelocityFragment.Value = RandomVector * FMath::FRandRange(MinSpeed, MaxSpeed);
				}
			}));
	}	
}
