// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityTestTypes.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "Engine/World.h"

//----------------------------------------------------------------------//
// Test bases 
//----------------------------------------------------------------------//
bool FExecutionTestBase::SetUp()
{
	World = FAITestHelpers::GetWorld();
	EntitySubsystem = NewObject<UMassEntitySubsystem>(World);
	check(EntitySubsystem);
	struct FSubsystemCollection_TestInit : FSubsystemCollectionBase
	{
		FSubsystemCollection_TestInit(){}
	};
	FSubsystemCollection_TestInit Collection;
	EntitySubsystem->Initialize(Collection);

	return true;
}

bool FEntityTestBase::SetUp()
{
	FExecutionTestBase::SetUp();
	check(EntitySubsystem);

	const UScriptStruct* FragmentTypes[] = { FTestFragment_Float::StaticStruct(), FTestFragment_Int::StaticStruct() };

	EmptyArchetype = EntitySubsystem->CreateArchetype(MakeArrayView<const UScriptStruct*>(nullptr, 0));
	FloatsArchetype = EntitySubsystem->CreateArchetype(MakeArrayView(&FragmentTypes[0], 1));
	IntsArchetype = EntitySubsystem->CreateArchetype(MakeArrayView(&FragmentTypes[1], 1));
	FloatsIntsArchetype = EntitySubsystem->CreateArchetype(MakeArrayView(FragmentTypes, 2));

	FTestFragment_Int IntFrag;
	IntFrag.Value = TestIntValue;
	InstanceInt = FInstancedStruct::Make(IntFrag);

	return true;
}


//----------------------------------------------------------------------//
// Processors 
//----------------------------------------------------------------------//
UMassTestProcessorBase::UMassTestProcessorBase()
{
#if WITH_EDITORONLY_DATA
	bCanShowUpInSettings = false;
#endif // WITH_EDITORONLY_DATA
	bAutoRegisterWithProcessingPhases = false;
	ExecutionFlags = int32(EProcessorExecutionFlags::All);

	ExecutionFunction = [](UMassEntitySubsystem& InEntitySubsystem, FMassExecutionContext& Context) {};
	RequirementsFunction = [](FMassEntityQuery& Query){};
}

UMassTestProcessor_Floats::UMassTestProcessor_Floats()
{
	RequirementsFunction = [this](FMassEntityQuery& Query)
	{
		EntityQuery.AddRequirement<FTestFragment_Float>(EMassFragmentAccess::ReadWrite);
	};
}

UMassTestProcessor_Ints::UMassTestProcessor_Ints()
{
	RequirementsFunction = [this](FMassEntityQuery& Query)
	{
		EntityQuery.AddRequirement<FTestFragment_Int>(EMassFragmentAccess::ReadWrite);
	};
}

UMassTestProcessor_FloatsInts::UMassTestProcessor_FloatsInts()
{
	RequirementsFunction = [this](FMassEntityQuery& Query)
	{
		EntityQuery.AddRequirement<FTestFragment_Float>(EMassFragmentAccess::ReadWrite);
		EntityQuery.AddRequirement<FTestFragment_Int>(EMassFragmentAccess::ReadWrite);
	};
}