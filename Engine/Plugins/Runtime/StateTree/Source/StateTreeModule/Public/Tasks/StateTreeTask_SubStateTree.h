// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "StateTreeTypes.h"
#include "StateTreeInstance.h"
#include "StateTreeVariable.h"
#include "StateTreeParameterLayout.h"
#include "StateTreeParameterStorage.h"
#include "StateTreeTaskBase.h"
#include "StateTreeTask_SubStateTree.generated.h"

struct FStateTreeVariableLayout;
struct FStateTreeConstantStorage;
class UStateTree;

/**
 * Sub StateTree Task
 * Runs specified StateTree while the state is active.
 */

UCLASS(BlueprintType, EditInlineNew, CollapseCategories)
class STATETREEMODULE_API UStateTreeTask_SubStateTree : public UStateTreeTaskBase
{
	GENERATED_BODY()

	public:
	UStateTreeTask_SubStateTree(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// Called when instantiated first time.
	virtual bool Initialize(FStateTreeInstance& StateTreeInstance) override;
	// Called when evaluator becomes active/ticking.
	virtual void Activate(FStateTreeInstance& StateTreeInstance) override;
	// Called when evaluator becomes inactive.
	virtual void Deactivate(FStateTreeInstance& StateTreeInstance) override;

	// Called on each tick.
	virtual EStateTreeRunStatus Tick(FStateTreeInstance& StateTreeInstance, const float DeltaTime) override;

#if WITH_GAMEPLAY_DEBUGGER
	virtual void AppendDebugInfoString(FString& DebugString, const FStateTreeInstance& StateTreeInstance) const;
#endif

#if WITH_EDITOR
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);

	virtual bool ResolveVariables(const FStateTreeVariableLayout& Variables, FStateTreeConstantStorage& Constants, UObject* Outer) override;
	virtual bool ValidateParameterLayout() override;
#endif

protected:

	UPROPERTY(EditDefaultsOnly, Category = Asset)
	UStateTree* StateTree;

	UPROPERTY(EditDefaultsOnly, Category = Asset)
	FStateTreeParameterLayout Parameters;

	UPROPERTY(Transient)
	FStateTreeParameterStorage ParameterStorage;

	UPROPERTY(Transient)
	FStateTreeInstance SubStateTreeInstance;
};


