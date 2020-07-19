// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusNodeGraphCollectionOwner.h"

#include "UObject/Object.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "ComputeKernelCollection.h"

#include "OptimusDeformer.generated.h"

class USkeletalMesh;
class UOptimusActionStack;



UCLASS()
class OPTIMUSCORE_API UOptimusDeformer : 
	public UComputeKernelCollection, 
	public IOptimusNodeGraphCollectionOwner
{
	GENERATED_BODY()

public:
	UOptimusDeformer();

	UOptimusActionStack *GetActionStack() const { return ActionStack; }

#if WITH_EDITOR
	/// Add a setup graph. This graph is executed once when the deformer is first run from a
	/// mesh component.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	UOptimusNodeGraph* AddSetupGraph();

	/// Add a trigger graph. This graph will be scheduled to execute on next tick, prior to the
	/// update graph being executed, after being triggered from a blueprint.
	UFUNCTION(BlueprintCallable, Category = RigVMController)
	UOptimusNodeGraph* AddTriggerGraph();
#endif

	/// IOptimusNodeGraphCollectionOwner overrides
	UOptimusNodeGraph* ResolveGraphPath(const FString& InGraphPath) override;
	UOptimusNode* ResolveNodePath(const FString& InNodePath) override;
	UOptimusNodePin* ResolvePinPath(const FString& InPinPath) override;
	const TArray<UOptimusNodeGraph*> &GetGraphs() override { return Graphs; }


public:

	UPROPERTY(EditAnywhere, Category=Preview)
	USkeletalMesh *Mesh = nullptr;

private:
	UOptimusNodeGraph* ResolveGraphPath(const FString& InPath, FString& OutRemainingPath);
	UOptimusNode* ResolveNodePath(const FString& InPath, FString& OutRemainingPath);
	
	UPROPERTY()
	TArray<UOptimusNodeGraph*> Graphs;

	UPROPERTY(transient)
	UOptimusActionStack *ActionStack;
};
