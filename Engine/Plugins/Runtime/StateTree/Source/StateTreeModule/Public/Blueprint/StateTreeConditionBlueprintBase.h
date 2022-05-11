// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "StateTreeTypes.h"
#include "StateTreeConditionBase.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeItemBlueprintBase.h"
#include "StateTreeConditionBlueprintBase.generated.h"

struct FStateTreeExecutionContext;

/*
 * Base class for Blueprint based Conditions. 
 */
UCLASS(Abstract, Blueprintable)
class STATETREEMODULE_API UStateTreeConditionBlueprintBase : public UStateTreeItemBlueprintBase
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintImplementableEvent)
	bool ReceiveTestCondition(AActor* OwnerActor) const;

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const;
};

/**
 * Wrapper for Blueprint based Conditions.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeBlueprintConditionWrapper : public FStateTreeConditionBase
{
	GENERATED_BODY()

	virtual const UStruct* GetInstanceDataType() const override { return ConditionClass; };
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	UPROPERTY()
	TSubclassOf<UStateTreeConditionBlueprintBase> ConditionClass = nullptr;

	TArray<FStateTreeBlueprintExternalDataHandle> ExternalDataHandles;
};
