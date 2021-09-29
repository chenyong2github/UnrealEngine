// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "LWComponentTypes.h"
#include "MassEntitySubsystem.h"
#include "AITestsCommon.h"
#include "MassEntityTestTypes.generated.h"


class UWorld;
class UMassEntitySubsystem;

USTRUCT()
struct FTestFragment_Float : public FLWComponentData
{
	GENERATED_BODY()
	float Value = 0;
};

USTRUCT()
struct FTestFragment_Int : public FLWComponentData
{
	GENERATED_BODY()
	int32 Value = 0;
};

USTRUCT()
struct FTestFragment_Bool : public FLWComponentData
{
	GENERATED_BODY()
	bool bValue = false;
};

/** @todo rename to FTestTag */
USTRUCT()
struct FTestFragment_Tag : public FComponentTag
{
	GENERATED_BODY()
};

USTRUCT()
struct FTestTag_A : public FComponentTag
{
	GENERATED_BODY()
};

USTRUCT()
struct FTestTag_B : public FComponentTag
{
	GENERATED_BODY()
};

USTRUCT()
struct FTestTag_C : public FComponentTag
{
	GENERATED_BODY()
};

USTRUCT()
struct FTestTag_D : public FComponentTag
{
	GENERATED_BODY()
};


UCLASS()
class UPipeTestProcessorBase : public UPipeProcessor
{
	GENERATED_BODY()
public:
	UPipeTestProcessorBase();
	FPipeProcessorExecutionOrder& GetMutableExecutionOrder() { return ExecutionOrder; }
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override 
	{
		ExecutionFunction(EntitySubsystem, Context);
	}
	virtual void ConfigureQueries() override
	{
		RequirementsFunction(EntityQuery);
	}

	TFunction<void(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)> ExecutionFunction;
	TFunction<void(FLWComponentQuery& Query)> RequirementsFunction;

	FLWComponentQuery& TestGetQuery() { return EntityQuery; }

protected:
	FLWComponentQuery EntityQuery;
};

UCLASS()
class UPipeTestProcessor_A : public UPipeTestProcessorBase
{
	GENERATED_BODY()
};

UCLASS()
class UPipeTestProcessor_B : public UPipeTestProcessorBase
{
	GENERATED_BODY()
};

UCLASS()
class UPipeTestProcessor_C : public UPipeTestProcessorBase
{
	GENERATED_BODY()
};

UCLASS()
class UPipeTestProcessor_D : public UPipeTestProcessorBase
{
	GENERATED_BODY()
};

UCLASS()
class UPipeTestProcessor_E : public UPipeTestProcessorBase
{
	GENERATED_BODY()
};

UCLASS()
class UPipeTestProcessor_F : public UPipeTestProcessorBase
{
	GENERATED_BODY()
};

UCLASS()
class UPipeTestProcessor_Floats : public UPipeTestProcessorBase
{
	GENERATED_BODY()
public:
	TArrayView<FTestFragment_Float> Floats;
	UPipeTestProcessor_Floats();
};

UCLASS()
class UPipeTestProcessor_Ints : public UPipeTestProcessorBase
{
	GENERATED_BODY()
public:
	TArrayView<FTestFragment_Int> Ints;
	UPipeTestProcessor_Ints();
};

UCLASS()
class UPipeTestProcessor_FloatsInts : public UPipeTestProcessorBase
{
	GENERATED_BODY()
public:
	TArrayView<FTestFragment_Float> Floats;
	TArrayView<FTestFragment_Int> Ints;
	UPipeTestProcessor_FloatsInts();
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
	FArchetypeHandle EmptyArchetype;
	FArchetypeHandle FloatsArchetype;
	FArchetypeHandle IntsArchetype;
	FArchetypeHandle FloatsIntsArchetype;

	FInstancedStruct InstanceInt;

	virtual bool SetUp() override;
};
