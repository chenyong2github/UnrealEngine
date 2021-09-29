// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LWComponentTypes.h"
#include "StateTreeExecutionContext.h"
#include "MassStateTreeExecutionContext.generated.h"

struct FLWComponentSystemExecutionContext;
class UEntitySubsystem;
class UMassSignalSubsystem;

/**
 * Extends FStateTreeExecutionContext to provide additional data to Evaluators and Tasks related to MassSimulation
 */
USTRUCT()
struct MASSAIBEHAVIOR_API FMassStateTreeExecutionContext : public FStateTreeExecutionContext 
{
	GENERATED_BODY()
public:
	/** Should never be used but has to be public for 'void UScriptStruct::TCppStructOps<FMassStateTreeExecutionContext>::ConstructForTests(void *)' */
	FMassStateTreeExecutionContext() = default;
	FMassStateTreeExecutionContext(UEntitySubsystem& InEntitySubsystem, FLWComponentSystemExecutionContext& InContext);

	UEntitySubsystem& GetEntitySubsystem() const { return *EntitySubsystem; }
	FLWComponentSystemExecutionContext& GetEntitySubsystemExecutionContext() const { return *EntitySubsystemExecutionContext; }

	FLWEntity GetEntity() const { return Entity; }
	void SetEntity(const FLWEntity& InEntity) { Entity = InEntity; }

	int32 GetEntityIndex() const { return EntityIndex; }
	void SetEntityIndex(const int32 InIndex) { EntityIndex = InIndex; }

protected:

	/** Prefix that will be used by STATETREE_LOG and STATETREE_CLOG, using Entity description. */
	virtual FString GetInstanceDescription() const override { return FString::Printf(TEXT("Entity [%s]"), *Entity.DebugGetDescription()); }

	virtual void BeginGatedTransition(const FStateTreeExecutionState& Exec) override;

	UEntitySubsystem* EntitySubsystem = nullptr;
	UMassSignalSubsystem* SignalSubsystem = nullptr;
	FLWComponentSystemExecutionContext* EntitySubsystemExecutionContext = nullptr;
	FLWEntity Entity;
	int32 EntityIndex = INDEX_NONE;
};
