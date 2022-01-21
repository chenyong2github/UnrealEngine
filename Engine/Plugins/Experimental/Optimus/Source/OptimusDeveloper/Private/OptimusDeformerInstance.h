// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/MeshDeformerInstance.h"
#include "ComputeFramework/ComputeGraphInstance.h"

#include "OptimusDeformerInstance.generated.h"


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


UCLASS()
class UOptimusDeformerInstance :
	public UMeshDeformerInstance
{
	GENERATED_BODY()

public:
	void SetupFromDeformer(UOptimusDeformer* InDeformer);

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
