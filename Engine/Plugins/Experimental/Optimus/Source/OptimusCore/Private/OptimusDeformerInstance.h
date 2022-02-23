// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/MeshDeformerInstance.h"
#include "ComputeFramework/ComputeGraphInstance.h"

#include "OptimusDeformerInstance.generated.h"

class UMeshComponent;
class UOptimusVariableDescription;
class UOptimusDeformer;
class UOptimusNode_ConstantValue;
class UOptimusVariableContainer;

/** Structure with cached state for a single compute graph. */
USTRUCT()
struct FOptimusDeformerInstanceExecInfo
{
	GENERATED_BODY()
	
	/** The ComputeGraph asset. */
	UPROPERTY()
	TObjectPtr<UComputeGraph> ComputeGraph = nullptr;

	/** The cached state for the ComputeGraph. */
	UPROPERTY()
	FComputeGraphInstance ComputeGraphInstance;
};

/** 
 * Class representing an instance of an Optimus Mesh Deformer.
 * This implements the UMeshDeformerInstance interface to enqueue the graph execution.
 * It also contains the per instance deformer variable state and local state for each of the graphs in the deformer.
 */
UCLASS(Blueprintable, BlueprintType, EditInlineNew)
class UOptimusDeformerInstance :
	public UMeshDeformerInstance
{
	GENERATED_BODY()

public:
	/** 
	 * Set the Mesh Component that owns this instance.
	 * Call once before first call to SetupFromDeformer().
	 */
	void SetMeshComponent(UMeshComponent* InMeshComponent) { MeshComponent = InMeshComponent; }

	/** 
	 * Setup the instance. 
	 * Needs to be called after the UOptimusDeformer creates this instance, and whenever the instance is invalidated.
	 * Invalidation happens whenever any bound Data Providers become invalid.
	 */
	void SetupFromDeformer(UOptimusDeformer* InDeformer);

	/** Set the value of a boolean variable. */
	UFUNCTION(BlueprintPure, Category="Deformer", meta=(DisplayName="Set Variable (bool)"))
	bool SetBoolVariable(FName InVariableName, bool InValue);

	/** Set the value of a boolean variable. */
	UFUNCTION(BlueprintCallable, Category="Deformer", meta=(DisplayName="Set Variable (int)"))
	bool SetIntVariable(FName InVariableName, int32 InValue);

	/** Set the value of a boolean variable. */
	UFUNCTION(BlueprintCallable, Category="Deformer", meta=(DisplayName="Set Variable (float)"))
	bool SetFloatVariable(FName InVariableName, float InValue);

	/** Set the value of a boolean variable. */
	UFUNCTION(BlueprintCallable, Category="Deformer", meta=(DisplayName="Set Variable (Vector)"))
	bool SetVectorVariable(FName InVariableName, const FVector& InValue);

	/** Set the value of a boolean variable. */
	UFUNCTION(BlueprintCallable, Category="Deformer", meta=(DisplayName="Set Variable (Vector4)"))
	bool SetVector4Variable(FName InVariableName, const FVector4& InValue);

	/** Get an array containing all the variables. */
	UFUNCTION(BlueprintGetter)
	const TArray<UOptimusVariableDescription*>& GetVariables() const;

	/** Directly set a graph constant value. */
	void SetConstantValueDirect(FString const& InVariableName, TArray<uint8> const& InValue);

protected:
	/** Implementation of UMeshDeformerInstance. */
	bool IsActive() const override;
	void EnqueueWork(FSceneInterface* InScene, EWorkLoad WorkLoadType) override;

private:
	/** The Mesh Component that owns this Mesh Deformer Instance. */
	UPROPERTY()
	TWeakObjectPtr<UMeshComponent> MeshComponent;

	/** An array of state. One for each graph owned by the deformer. */
	UPROPERTY()
	TArray<FOptimusDeformerInstanceExecInfo> ComputeGraphExecInfos;

	/** Storage for variable data. */
	UPROPERTY()
	TObjectPtr<UOptimusVariableContainer> Variables;
};
