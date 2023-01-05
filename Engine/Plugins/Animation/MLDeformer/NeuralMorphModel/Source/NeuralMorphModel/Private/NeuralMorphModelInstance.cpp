// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralMorphModelInstance.h"
#include "NeuralMorphModel.h"
#include "NeuralMorphNetwork.h"
#include "MLDeformerComponent.h"
#include "MLDeformerAsset.h"
#include "Animation/AnimInstance.h"
#include "Components/ExternalMorphSet.h"
#include "Components/SkeletalMeshComponent.h"


void UNeuralMorphModelInstance::Init(USkeletalMeshComponent* SkelMeshComponent)
{
	Super::Init(SkelMeshComponent);

#if !NEURALMORPHMODEL_FORCE_USE_NNI
	UNeuralMorphModel* MorphModel = Cast<UNeuralMorphModel>(Model);
	UNeuralMorphNetwork* MorphNetwork = MorphModel->GetNeuralMorphNetwork();
	NetworkInstance = MorphNetwork ? MorphNetwork->CreateInstance() : nullptr;
#endif
}


int64 UNeuralMorphModelInstance::SetCurveValues(float* OutputBuffer, int64 OutputBufferSize, int64 StartIndex)
{
	UNeuralMorphModel* MorphModel = Cast<UNeuralMorphModel>(Model);
	UNeuralMorphNetwork* MorphNetwork = MorphModel->GetNeuralMorphNetwork();

	const UMLDeformerInputInfo* InputInfo = Model->GetInputInfo();

	int64 Index = StartIndex;
	const int32 AssetNumCurves = InputInfo->GetNumCurves();
	const int32 NumFloatsPerCurve = MorphNetwork ? MorphNetwork->GetNumFloatsPerCurve() : 1;
	const int32 NumCurveFloats = AssetNumCurves * NumFloatsPerCurve;
	checkf((Index + NumCurveFloats) <= OutputBufferSize, TEXT("Writing curves past the end of the input buffer"));

	if (NumCurveFloats > 1)
	{
		// First write all zeros.
		for (int32 CurveIndex = 0; CurveIndex < NumCurveFloats; ++CurveIndex)
		{
			OutputBuffer[Index++] = 0.0f;
		}

		// Write the 
		UAnimInstance* AnimInstance = SkeletalMeshComponent->GetAnimInstance();
		if (AnimInstance)
		{
			for (int32 CurveIndex = 0; CurveIndex < AssetNumCurves; ++CurveIndex)
			{
				const FName CurveName = InputInfo->GetCurveName(CurveIndex);
				const float CurveValue = AnimInstance->GetCurveValue(CurveName);	// Outputs 0.0 when not found.
				OutputBuffer[StartIndex + CurveIndex * NumFloatsPerCurve] = CurveValue;
			}
		}
	}
	else
	{
		checkSlow(NumCurveFloats == 1);
		UAnimInstance* AnimInstance = SkeletalMeshComponent->GetAnimInstance();
		if (AnimInstance)
		{
			for (int32 CurveIndex = 0; CurveIndex < AssetNumCurves; ++CurveIndex)
			{
				const FName CurveName = InputInfo->GetCurveName(CurveIndex);
				const float CurveValue = AnimInstance->GetCurveValue(CurveName);	// Outputs 0.0 when not found.
				OutputBuffer[Index++] = CurveValue;
			}
		}
		else
		{
			// Just write zeros.
			for (int32 CurveIndex = 0; CurveIndex < NumCurveFloats; ++CurveIndex)
			{
				OutputBuffer[Index++] = 0.0f;
			}
		}
	}

	return Index;
}


bool UNeuralMorphModelInstance::SetupInputs()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UNeuralMorphModelInstance::SetupInputs)

#if NEURALMORPHMODEL_FORCE_USE_NNI
	return Super::SetupInputs();
#else
	// If we have no neural morph network, fall back to the default NNI path.
	UNeuralMorphModel* MorphModel = Cast<UNeuralMorphModel>(Model);
	UNeuralMorphNetwork* MorphNetwork = MorphModel->GetNeuralMorphNetwork();
	if (MorphNetwork == nullptr)
	{		
		return Super::SetupInputs();
	}

	// Some safety checks.
	if (SkeletalMeshComponent == nullptr ||
		SkeletalMeshComponent->GetSkeletalMeshAsset() == nullptr ||
		!bIsCompatible)
	{
		return false;
	}

	// If the neural network expects a different number of inputs, do nothing.
	const int64 NumNeuralNetInputs = MorphNetwork->GetNumInputs();
	const int32 NumFloatsPerBone = 6;
	const int32 NumFloatsPerCurve = MorphNetwork->GetNumFloatsPerCurve();
	const int64 NumDeformerAssetInputs = Model->GetInputInfo()->CalcNumNeuralNetInputs(NumFloatsPerBone, NumFloatsPerCurve);
	if (NumNeuralNetInputs != NumDeformerAssetInputs)
	{
		return false;
	}

	// Update and write the input values directly into the input tensor.
	float* InputDataPointer = NetworkInstance->GetInputs().GetData();
	const int64 NumFloatsWritten = SetNeuralNetworkInputValues(InputDataPointer, NumNeuralNetInputs);
	check(NumFloatsWritten == NumNeuralNetInputs);

	return true;
#endif
}

void UNeuralMorphModelInstance::Execute(float ModelWeight)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UNeuralMorphModelInstance::Execute)

#if NEURALMORPHMODEL_FORCE_USE_NNI
	Super::Execute(ModelWeight);
	return;
#else
	UNeuralMorphModel* MorphModel = Cast<UNeuralMorphModel>(Model);
	UNeuralMorphNetwork* MorphNetwork = MorphModel->GetNeuralMorphNetwork();
	if (MorphNetwork == nullptr)
	{
		Super::Execute(ModelWeight);
		return;
	}

	// Grab the weight data for this morph set.
	// This could potentially fail if we are applying this deformer to the wrong skeletal mesh component.
	const int LOD = 0;	// For now we only support LOD 0, as we can't setup an ML Deformer per LOD yet.
	FExternalMorphSetWeights* WeightData = FindWeightData(LOD);
	if (WeightData == nullptr)
	{
		return;
	}

	if (!MorphNetwork->IsEmpty())
	{
		// Perform inference on the neural network, this updates its output values.
		NetworkInstance->Run();

		// If our model is active, we want to run the neural network and update the morph weights
		// with the values that the neural net calculated for us.
		// Get the network output values, read the values and use them as morph target weights inside the skeletal mesh component.
		const TArrayView<const float> NetworkOutputs = NetworkInstance->GetOutputs();
		const int32 NumNetworkWeights = NetworkOutputs.Num();
		const int32 NumMorphTargets = WeightData->Weights.Num();

		// If we have the expected amount of weights.
		// +1 because we always have an extra morph target that represents the means, with fixed weight of 1.
		// Therefore, the neural network output will contain one less float than the number of morph targets in our morph set.
		if (NumMorphTargets == NumNetworkWeights + 1)
		{
			// Set the first morph target, which represents the means, to a weight of 1.0, as it always needs to be fully active.
			WeightData->Weights[0] = ModelWeight;

			// Update all generated morph target weights with the values calculated by our neural network.
			const TArrayView<const float> ErrorValues = MorphModel->GetMorphTargetErrorValues();
			const TArrayView<const int32> ErrorOrder = MorphModel->GetMorphTargetErrorOrder();
			if (!ErrorValues.IsEmpty())
			{
				const int32 QualityLevel = GetMLDeformerComponent()->GetQualityLevel();
				const int32 NumActiveMorphs = MorphModel->GetNumActiveMorphs(QualityLevel);
				for (int32 Index = 0; Index < NumActiveMorphs; ++Index)
				{
					const int32 MorphIndex = ErrorOrder[Index];
					const float TargetWeight = NetworkOutputs[MorphIndex] * ModelWeight;
					WeightData->Weights[MorphIndex + 1] = FMath::Lerp<float, float>(StartMorphWeights[MorphIndex + 1], TargetWeight, MorphLerpAlpha);
				}

				// Disable all inactive morphs.
				for (int32 Index = NumActiveMorphs; Index < NumNetworkWeights; ++Index)
				{
					const int32 MorphIndex = ErrorOrder[Index];
					WeightData->Weights[MorphIndex + 1] = FMath::Lerp<float, float>(StartMorphWeights[MorphIndex + 1], 0.0f, MorphLerpAlpha);
				}
			}
			else
			{
				for (int32 MorphIndex = 0; MorphIndex < NumNetworkWeights; ++MorphIndex)
				{
					WeightData->Weights[MorphIndex + 1] = NetworkOutputs[MorphIndex] * ModelWeight;
				}
			}
			return;
		}
	}

	// Set the weights to zero if we have no valid network.
	WeightData->ZeroWeights();
#endif
}
