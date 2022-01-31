// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/MeshDeformerInstance.h"
#include "ComputeFramework/ComputeGraphInstance.h"

#include "OptimusDeformerInstance.generated.h"


class UOptimusVariableDescription;
class UMeshComponent;
class UOptimusDeformer;


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


UCLASS(Blueprintable, BlueprintType, EditInlineNew)
class UOptimusDeformerInstance :
	public UMeshDeformerInstance
{
	GENERATED_BODY()

public:
	void SetupFromDeformer(UOptimusDeformer* InDeformer);

	UPROPERTY(BlueprintReadWrite, Category="Deformer")
	int32 Spanner;

	UFUNCTION(BlueprintGetter)
	const TArray<UOptimusVariableDescription*>& GetVariables() const;

	/** Set the value of a boolean variable */
	UFUNCTION(BlueprintPure, Category="Control Rig", meta=(DisplayName="Set Variable (bool)"))
	bool SetBoolVariable(FName InVariableName, bool InValue);

	/** Set the value of a boolean variable */
	UFUNCTION(BlueprintCallable, Category="Control Rig", meta=(DisplayName="Set Variable (int)"))
	bool SetIntVariable(FName InVariableName, int32 InValue);

	/** Set the value of a boolean variable */
	UFUNCTION(BlueprintCallable, Category="Deformer", meta=(DisplayName="Set Variable (float)"))
	bool SetFloatVariable(FName InVariableName, float InValue);

	/** Set the value of a boolean variable */
	UFUNCTION(BlueprintCallable, Category="Deformer", meta=(DisplayName="Set Variable (Vector)"))
	bool SetVectorVariable(FName InVariableName, const FVector& InValue);

	/** Set the value of a boolean variable */
	UFUNCTION(BlueprintCallable, Category="Deformer", meta=(DisplayName="Set Variable (Vector4)"))
	bool SetVector4Variable(FName InVariableName, const FVector4& InValue);
	
	
	UPROPERTY()
	TWeakObjectPtr<UMeshComponent> MeshComponent;
	
protected:
	/** Implementation of UMeshDeformerInstance. */
	bool IsActive() const override;
	void EnqueueWork(FSceneInterface* InScene, EWorkLoad WorkLoadType) override;

private:
	UPROPERTY()
	TArray<FOptimusDeformerInstanceExecInfo> ComputeGraphExecInfos;
};
