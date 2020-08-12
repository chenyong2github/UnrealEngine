// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusNodeGraphCollectionOwner.h"
#include "OptimusNodeGraphNotify.h"

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

	/// Add a setup graph. This graph is executed once when the deformer is first run from a
	/// mesh component. If the graph already exists, this function does nothing and returns 
	/// nullptr.
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	UOptimusNodeGraph* AddSetupGraph();

	/// Add a trigger graph. This graph will be scheduled to execute on next tick, prior to the
	/// update graph being executed, after being triggered from a blueprint.
	/// @param InName The name to give the graph. The name "Setup" cannot be used, since it's a
	/// reserved name.
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	UOptimusNodeGraph* AddTriggerGraph(const FString &InName);

	/// Add a setup graph. This graph is executed once when the deformer is first run from a
	/// mesh component.
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool RemoveGraph(UOptimusNodeGraph* InGraph);

	/// IOptimusNodeGraphCollectionOwner overrides
	FOptimusNodeGraphEvent& OnModify() override { return ModifiedEventDelegate; }
	UOptimusNodeGraph* ResolveGraphPath(const FString& InGraphPath) override;
	UOptimusNode* ResolveNodePath(const FString& InNodePath) override;
	UOptimusNodePin* ResolvePinPath(const FString& InPinPath) override;
	const TArray<UOptimusNodeGraph*> &GetGraphs() const override { return Graphs; }
	UOptimusNodeGraph* CreateGraph(
	    EOptimusNodeGraphType InType,
	    FName InName,
	    TOptional<int32> InInsertBefore) override;
	bool AddGraph(
	    UOptimusNodeGraph* InGraph,
		int32 InInsertBefore) override;
	bool RemoveGraph(
	    UOptimusNodeGraph* InGraph,
		bool bDeleteGraph) override;

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool MoveGraph(
	    UOptimusNodeGraph* InGraph,
	    int32 InInsertBefore) override;

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool RenameGraph(
	    UOptimusNodeGraph* InGraph,
	    const FString& InNewName) override;


public:

	UPROPERTY(EditAnywhere, Category=Preview)
	USkeletalMesh *Mesh = nullptr;

private:
	UOptimusNodeGraph* ResolveGraphPath(const FString& InPath, FString& OutRemainingPath);
	UOptimusNode* ResolveNodePath(const FString& InPath, FString& OutRemainingPath);
	
	void Notify(EOptimusNodeGraphNotifyType InNotifyType, UOptimusNodeGraph *InGraph);

	UPROPERTY()
	TArray<UOptimusNodeGraph*> Graphs;

	UPROPERTY(transient)
	UOptimusActionStack *ActionStack;

	FOptimusNodeGraphEvent ModifiedEventDelegate;
};
