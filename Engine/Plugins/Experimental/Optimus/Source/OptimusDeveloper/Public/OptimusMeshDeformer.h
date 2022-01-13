// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/MeshDeformer.h"
#include "Animation/MeshDeformerInstance.h"
#include "ComputeFramework/ComputeGraphInstance.h"
#include "OptimusMeshDeformer.generated.h"

/** 
 * Optimus implementation of UMeshDeformer.
 * This should be temporary until we move this interface to UOptimusDefomer.
 */
UCLASS()
class OPTIMUSDEVELOPER_API UOptimusMeshDeformer : public UMeshDeformer
{
	GENERATED_BODY()

public:
	/** The Compute Graph asset. */
	UPROPERTY(EditAnywhere, Category = "Compute", meta = (DisplayName = "Deformer Graph"))
	TObjectPtr<UComputeGraph> ComputeGraph = nullptr;

protected:
	/** Implementation of UMeshDeformer. */
	UMeshDeformerInstance* CreateInstance(UMeshComponent* InMeshComponent) override;
};

/**
 * Optimus implementation of UMeshDeformerInstance.
 * This should be temporary until we base it on the multiple graphs in UOptimusDefomer.
 */
UCLASS()
class OPTIMUSDEVELOPER_API UOptimusMeshDeformerInstance : public UMeshDeformerInstance
{
	GENERATED_BODY()

protected:
	friend UOptimusMeshDeformer;

	/** The ComputeGraph asset. */
	UPROPERTY()
	TObjectPtr<UComputeGraph> ComputeGraph = nullptr;

	/** The cached state for the ComputeGraph. */
	UPROPERTY()
 	FComputeGraphInstance ComputeGraphInstance;

protected:
	/** Implementation of UMeshDeformerInstance. */
	bool IsActive() const override;
	void EnqueueWork(FSceneInterface* InScene, EWorkLoad WorkLoadType) override;
};
