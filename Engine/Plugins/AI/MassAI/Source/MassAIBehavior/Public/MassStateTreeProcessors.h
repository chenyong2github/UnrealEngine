// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassSignals/Public/MassSignalProcessorBase.h"
#include "MassStateTreeFragments.h"
#include "MassTranslator.h"
#include "MassStateTreeProcessors.generated.h"

struct FMassStateTreeExecutionContext;

/** 
 * Processor to prepare StateTree fragments for their execution
 */
UCLASS()
class MASSAIBEHAVIOR_API UMassStateTreeFragmentInitializer : public UMassFragmentInitializer
{
	GENERATED_BODY()

public:
	UMassStateTreeFragmentInitializer();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSAIBEHAVIOR_API UMassStateTreeFragmentDestructor : public UMassFragmentDestructor
{
	GENERATED_BODY()

public:
	UMassStateTreeFragmentDestructor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};

/** 
 * Processor for executing a StateTree
 */
UCLASS()
class MASSAIBEHAVIOR_API UMassStateTreeProcessor : public UMassSignalProcessorBase
{
	GENERATED_BODY()

public:
	UMassStateTreeProcessor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void Initialize(UObject& Owner) override;
	virtual void ConfigureQueries() override;
	virtual void SignalEntities(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context, FMassSignalNameLookup& EntitySignals) override;

	UPROPERTY(Transient)
	UMassStateTreeSubsystem* MassStateTreeSubsystem;
};
