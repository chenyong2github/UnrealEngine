// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MLDeformerMorphModelInstance.h"
#include "NeuralTensor.h"
#include "NearestNeighborModelInstance.generated.h"

UCLASS()
class NEARESTNEIGHBORMODEL_API UNearestNeighborModelInstance
    : public UMLDeformerModelInstance
{
    GENERATED_BODY()
public:

    // UMLDeformerModelInstance overrides
    virtual void RunNeuralNetwork(float ModelWeight) override;
    virtual bool SetupNeuralNetworkForFrame() override;

protected:
    virtual int64 SetBoneTransforms(float* OutputBuffer, int64 OutputBufferSize, int64 StartIndex) override;
    // ~END UMLDeformerModelInstance overrides

private:
    void RunNearestNeighborModel(float ModelWeight);
    int32 FindNearestNeighbor(const FNeuralTensor& PCACoeffTensor, int32 PartId);
    void UpdateWeight(TArray<float>& MorphWeights, const int32 Index, const float W);
};
