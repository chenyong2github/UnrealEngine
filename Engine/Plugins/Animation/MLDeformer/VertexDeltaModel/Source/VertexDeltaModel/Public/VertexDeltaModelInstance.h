// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MLDeformerModelInstance.h"
#include "NNECore.h"
#include "NNECoreRuntime.h"
#include "NNECoreModelData.h"
#include "NNECoreRuntimeRDG.h"
#include "RenderGraphResources.h"
#include "VertexDeltaModelInstance.generated.h"

class UVertexDeltaModel;
class UNeuralNetwork;

UCLASS()
class VERTEXDELTAMODEL_API UVertexDeltaModelInstance
	: public UMLDeformerModelInstance
{
	GENERATED_BODY()

public:
	// UMLDeformerModelInstance overrides.
	virtual FString CheckCompatibility(USkeletalMeshComponent* InSkelMeshComponent, bool bLogIssues) override;
	virtual bool IsValidForDataProvider() const override;
	virtual void Execute(float ModelWeight) override;
	virtual bool SetupInputs() override;
	// ~END UMLDeformerModelInstance overrides.

	UVertexDeltaModel* GetVertexDeltaModel() const;
	virtual void PostMLDeformerComponentInit() override;

	/**
	 * Get the RDG Neural Network Model
	 * @return The IModelRDG if successfully created, or nullptr
	 */
	UE::NNECore::IModelRDG* GetNNEModelRDG() const;

	/**
	 * Get the output vertex delta buffer
	 * @return The FRDGBuffer for vertex deltas
	 */
	TRefCountPtr<FRDGPooledBuffer> GetOutputRDGBuffer() const;

	/**
	 * Get the render graph buffer description required for the output of a neural network. Return false if a flat buffer is not appropriate
	 * @return True if the OutputTensorDescs can be rppresented by a flat float  FRDGBuffer
	 */
	bool GetRDGVertexBufferDesc(TConstArrayView<UE::NNECore::FTensorDesc>& InOutputTensorDescs, FRDGBufferDesc& OutBufferDesc);

private:

	void CreateRDGBuffers(TConstArrayView<UE::NNECore::FTensorDesc>& OutputTensorDescs);
	void CreateNNEModel();

	// Input Buffer for Joint Matrices / Curve Floats
	TRefCountPtr<FRDGPooledBuffer> RDGInputBuffer;

	// Output Buffer for Vertex Deltas
	TRefCountPtr<FRDGPooledBuffer> RDGVertexDeltaBuffer;

	// The NNE RDG Model 
	TUniquePtr<UE::NNECore::IModelRDG> ModelRDG;
	
	// The CPU Input Tensor Buffer
	TArray<float> NNEInputTensorBuffer;

	// Only attempt to create NNE Model once
	bool bNNECreationAttempted = false;
};
