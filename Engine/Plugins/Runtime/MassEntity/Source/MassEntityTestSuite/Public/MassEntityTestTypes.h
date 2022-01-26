// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassEntityTypes.h"
#include "MassEntitySubsystem.h"
#include "AITestsCommon.h"
#include "MassEntityTestTypes.generated.h"


class UWorld;
class UMassEntitySubsystem;

USTRUCT()
struct FTestFragment_Float : public FMassFragment
{
	GENERATED_BODY()
	float Value = 0;
};

USTRUCT()
struct FTestFragment_Int : public FMassFragment
{
	GENERATED_BODY()
	int32 Value = 0;
};

USTRUCT()
struct FTestFragment_Bool : public FMassFragment
{
	GENERATED_BODY()
	bool bValue = false;
};

/** @todo rename to FTestTag */
USTRUCT()
struct FTestFragment_Tag : public FMassTag
{
	GENERATED_BODY()
};

USTRUCT()
struct FTestTag_A : public FMassTag
{
	GENERATED_BODY()
};

USTRUCT()
struct FTestTag_B : public FMassTag
{
	GENERATED_BODY()
};

USTRUCT()
struct FTestTag_C : public FMassTag
{
	GENERATED_BODY()
};

USTRUCT()
struct FTestTag_D : public FMassTag
{
	GENERATED_BODY()
};


UCLASS()
class UMassTestProcessorBase : public UMassProcessor
{
	GENERATED_BODY()
public:
	UMassTestProcessorBase();
	FMassProcessorExecutionOrder& GetMutableExecutionOrder() { return ExecutionOrder; }
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override 
	{
		ExecutionFunction(EntitySubsystem, Context);
	}
	virtual void ConfigureQueries() override
	{
		RequirementsFunction(EntityQuery);
	}

	TFunction<void(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)> ExecutionFunction;
	TFunction<void(FMassEntityQuery& Query)> RequirementsFunction;

	FMassEntityQuery& TestGetQuery() { return EntityQuery; }

protected:
	FMassEntityQuery EntityQuery;
};

UCLASS()
class UMassTestProcessor_A : public UMassTestProcessorBase
{
	GENERATED_BODY()
};

UCLASS()
class UMassTestProcessor_B : public UMassTestProcessorBase
{
	GENERATED_BODY()
};

UCLASS()
class UMassTestProcessor_C : public UMassTestProcessorBase
{
	GENERATED_BODY()
};

UCLASS()
class UMassTestProcessor_D : public UMassTestProcessorBase
{
	GENERATED_BODY()
};

UCLASS()
class UMassTestProcessor_E : public UMassTestProcessorBase
{
	GENERATED_BODY()
};

UCLASS()
class UMassTestProcessor_F : public UMassTestProcessorBase
{
	GENERATED_BODY()
};

UCLASS()
class UMassTestProcessor_Floats : public UMassTestProcessorBase
{
	GENERATED_BODY()
public:
	TArrayView<FTestFragment_Float> Floats;
	UMassTestProcessor_Floats();
};

UCLASS()
class UMassTestProcessor_Ints : public UMassTestProcessorBase
{
	GENERATED_BODY()
public:
	TArrayView<FTestFragment_Int> Ints;
	UMassTestProcessor_Ints();
};

UCLASS()
class UMassTestProcessor_FloatsInts : public UMassTestProcessorBase
{
	GENERATED_BODY()
public:
	TArrayView<FTestFragment_Float> Floats;
	TArrayView<FTestFragment_Int> Ints;
	UMassTestProcessor_FloatsInts();
};

struct FExecutionTestBase : FAITestBase
{
	UMassEntitySubsystem* EntitySubsystem = nullptr;
	UWorld* World = nullptr;

	virtual bool SetUp() override;
};

const int TestIntValue = 123456;

struct FEntityTestBase : FExecutionTestBase
{
	FMassArchetypeHandle EmptyArchetype;
	FMassArchetypeHandle FloatsArchetype;
	FMassArchetypeHandle IntsArchetype;
	FMassArchetypeHandle FloatsIntsArchetype;

	FInstancedStruct InstanceInt;

	virtual bool SetUp() override;
};
