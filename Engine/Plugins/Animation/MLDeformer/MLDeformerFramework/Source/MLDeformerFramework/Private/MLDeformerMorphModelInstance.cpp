// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerMorphModelInstance.h"
#include "MLDeformerMorphModel.h" 
#include "NeuralNetwork.h"
#include "Components/SkeletalMeshComponent.h"

void UMLDeformerMorphModelInstance::Release()
{
	Super::Release();

	if (SkeletalMeshComponent == nullptr)
	{
		return;
	}

	const UMLDeformerMorphModel* MorphModel = Cast<UMLDeformerMorphModel>(Model);
	if (MorphModel == nullptr)
	{
		return;
	}

	const int32 LOD = 0;
	SkeletalMeshComponent->RemoveExternalMorphSet(LOD, MorphModel->GetExternalMorphSetID());
	SkeletalMeshComponent->RefreshExternalMorphTargetWeights();
}

// Run the neural network, which calculates its outputs, which are the weights of our morph targets.
void UMLDeformerMorphModelInstance::RunNeuralNetwork(float ModelWeight)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UNeuralMorphModelInstance::RunNeuralNetwork)

	const UMLDeformerMorphModel* MorphModel = Cast<UMLDeformerMorphModel>(Model);
	if (MorphModel == nullptr)
	{
		return;
	}

	// Grab the weight data for this morph set.
	// This could potentially fail if we are applying this deformer to the wrong skeletal mesh component.
	const int LOD = 0;	// For now we only support LOD 0, as we can't setup an ML Deformer per LOD yet.
	FExternalMorphSetWeights* WeightData = MorphModel->FindExternalMorphWeights(LOD, SkeletalMeshComponent);
	if (WeightData == nullptr)
	{
		return;
	}

	// If our model is active, we want to run the neural network and update the morph weights
	// with the values that the neural net calculated for us.
	const UNeuralNetwork* NeuralNetwork = Model->GetNeuralNetwork();
	if (ModelWeight > 0.0f && NeuralNetwork != nullptr)
	{
		// Perform the neural network inference, which updates the output tensor.
		// This takes most CPU time inside this method.
		UMLDeformerModelInstance::RunNeuralNetwork(ModelWeight);

		// Get the output tensor, read the values and use them as morph target weights inside the skeletal mesh component.
		const FNeuralTensor& OutputTensor = NeuralNetwork->GetOutputTensorForContext(NeuralNetworkInferenceHandle);
		const int32 NumNetworkWeights = OutputTensor.Num();
		const int32 NumMorphTargets = WeightData->Weights.Num();;

		// If we have the expected amount of weights.
		// +1 because we always have an extra morph target that represents the means, with fixed weight of 1.
		// Therefore, the neural network output tensor will contain one less float than the number of morph targets in our morph set.
		if (NumMorphTargets == NumNetworkWeights + 1)
		{
			// Set the first morph target, which represents the means, to a weight of 1.0, as it always needs to be fully active.
			WeightData->Weights[0] = 1.0f * ModelWeight;

			// Update all generated morph target weights with the values calculated by our neural network.
			for (int32 MorphIndex = 0; MorphIndex < NumNetworkWeights; ++MorphIndex)
			{
				const float OutputTensorValue = OutputTensor.At<float>(MorphIndex);
				WeightData->Weights[MorphIndex + 1] = OutputTensorValue * ModelWeight;
			}
		}
	}
	else
	{
		WeightData->ZeroWeights();
	}
}
