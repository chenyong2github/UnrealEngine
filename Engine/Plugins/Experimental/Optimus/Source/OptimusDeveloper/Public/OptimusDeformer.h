// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusNodeGraphCollectionOwner.h"
#include "OptimusCoreNotify.h"
#include "OptimusDataType.h"

#include "UObject/Object.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "ComputeKernelCollection.h"
#include "Engine/Blueprint.h"

#include "OptimusDeformer.generated.h"

class USkeletalMesh;
class UOptimusActionStack;
class UOptimusResourceDescription;
class UOptimusVariableDescription;


UCLASS()
class OPTIMUSDEVELOPER_API UOptimusDeformer :
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

	/** Remove a graph and delete it. */
	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
	bool RemoveGraph(UOptimusNodeGraph* InGraph);


	// Variables
	UFUNCTION(BlueprintCallable, Category = OptimusVariables)
	UOptimusVariableDescription* AddVariable(
		FOptimusDataTypeRef InDataTypeRef,
	    FName InName = NAME_None
		);

	UFUNCTION(BlueprintCallable, Category = OptimusVariables)
	bool RemoveVariable(
	    UOptimusVariableDescription* InVariableDesc
		);

	UFUNCTION(BlueprintCallable, Category = OptimusVariables)
	bool RenameVariable(
	    UOptimusVariableDescription* InVariableDesc,
	    FName InNewName);

	UFUNCTION(BlueprintCallable, Category = OptimusVariables)
	const TArray<UOptimusVariableDescription*>& GetVariables() const { return VariableDescriptions; }

	
	UOptimusVariableDescription* ResolveVariable(
		FName InVariableName
		);

	/** Create a resource owned by this deformer but does not add it to the list of known
	  * resources. Call AddResource for that */
	UOptimusVariableDescription* CreateVariableDirect(
		FName InName
		);

	/** Adds a resource that was created by this deformer and is owned by it. */
	bool AddVariableDirect(
		UOptimusVariableDescription* InVariableDesc
		);

	bool RemoveVariableDirect(
		UOptimusVariableDescription* InVariableDesc
		);

	bool RenameVariableDirect(
	    UOptimusVariableDescription* InVariableDesc,
		FName InNewName
		);



	// Resources
	UFUNCTION(BlueprintCallable, Category = OptimusResources)
	UOptimusResourceDescription* AddResource(
		FOptimusDataTypeRef InDataTypeRef,
	    FName InName = NAME_None
		);

	UFUNCTION(BlueprintCallable, Category = OptimusResources)
	bool RemoveResource(
	    UOptimusResourceDescription* InResourceDesc
		);

	UFUNCTION(BlueprintCallable, Category = OptimusResources)
	bool RenameResource(
	    UOptimusResourceDescription* InResourceDesc,
	    FName InNewName);

	UFUNCTION(BlueprintCallable, Category = OptimusResources)
	const TArray<UOptimusResourceDescription*>& GetResources() const { return ResourceDescriptions; }

	
	UOptimusResourceDescription* ResolveResource(
		FName InResourceName
		);

	/** Create a resource owned by this deformer but does not add it to the list of known
	  * resources. Call AddResource for that */
	UOptimusResourceDescription* CreateResourceDirect(
		FName InName
		);

	/** Adds a resource that was created by this deformer and is owned by it. */
	bool AddResourceDirect(
		UOptimusResourceDescription* InResourceDesc
		);

	bool RemoveResourceDirect(
		UOptimusResourceDescription* InResourceDesc
		);

	bool RenameResourceDirect(
	    UOptimusResourceDescription* InResourceDesc,
		FName InNewName
		);


	/// IOptimusNodeGraphCollectionOwner overrides
	FOptimusGlobalNotifyDelegate& GetNotifyDelegate() override { return GlobalNotifyDelegate; }
	UOptimusNodeGraph* ResolveGraphPath(const FString& InGraphPath) override;
	UOptimusNode* ResolveNodePath(const FString& InNodePath) override;
	UOptimusNodePin* ResolvePinPath(const FString& InPinPath) override;

	UFUNCTION(BlueprintCallable, Category = OptimusNodeGraph)
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
	
	void Notify(EOptimusGlobalNotifyType InNotifyType, UObject *InObject);

	UPROPERTY()
	TArray<UOptimusNodeGraph*> Graphs;

	UPROPERTY()
	TArray<UOptimusVariableDescription *> VariableDescriptions;

	UPROPERTY()
	TArray<UOptimusResourceDescription *> ResourceDescriptions;

	UPROPERTY(transient)
	UOptimusActionStack *ActionStack;

	FOptimusGlobalNotifyDelegate GlobalNotifyDelegate;
};
