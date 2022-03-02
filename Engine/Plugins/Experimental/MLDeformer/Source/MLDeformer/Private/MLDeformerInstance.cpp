// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerInstance.h"
#include "MLDeformer.h"
#include "MLDeformerAsset.h"
#include "MLDeformerInputInfo.h"
#include "Components/SkeletalMeshComponent.h"
#include "NeuralNetwork.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

void FMLDeformerInstance::Init(UMLDeformerAsset* Asset, USkeletalMeshComponent* SkelMeshComponent)
{
	DeformerAsset = Asset;
	SkeletalMeshComponent = SkelMeshComponent;

	if (SkelMeshComponent == nullptr || Asset == nullptr)
	{
		AssetBonesToSkelMeshMappings.Empty();
		return;
	}

	USkeletalMesh* SkelMesh = SkelMeshComponent->SkeletalMesh;
	if (SkelMesh)
	{
		// Init the bone mapping table.
		const FMLDeformerInputInfo& InputInfo = DeformerAsset->GetInputInfo();
		const int32 NumAssetBones = InputInfo.GetNumBones();
		AssetBonesToSkelMeshMappings.Reset();
		AssetBonesToSkelMeshMappings.AddUninitialized(NumAssetBones);

		// For each bone in the deformer asset, find the matching bone index inside the skeletal mesh component.
		for (int32 Index = 0; Index < NumAssetBones; ++Index)
		{
			const FName BoneName = InputInfo.GetBoneName(Index);
			const int32 SkelMeshBoneIndex = SkeletalMeshComponent->GetBoneIndex(BoneName);
			AssetBonesToSkelMeshMappings[Index] = SkelMeshBoneIndex;
		}
	}

	// Perform a compatibility check.
	UpdateCompatibilityStatus();
}

void FMLDeformerInstance::Release()
{
	UNeuralNetwork* NeuralNetwork = DeformerAsset != nullptr ? DeformerAsset->GetInferenceNeuralNetwork() : nullptr;
	if (NeuralNetwork != nullptr && NeuralNetworkInferenceHandle != -1)
	{
		NeuralNetwork->DestroyInferenceContext(NeuralNetworkInferenceHandle);
		NeuralNetworkInferenceHandle = -1;
	}
}

void FMLDeformerInstance::UpdateCompatibilityStatus()
{
	bIsCompatible = SkeletalMeshComponent->SkeletalMesh && CheckCompatibility(SkeletalMeshComponent, true).IsEmpty();
}

FString FMLDeformerInstance::CheckCompatibility(USkeletalMeshComponent* InSkelMeshComponent, bool LogIssues)
{
	ErrorText = FString();

	// If we're not compatible, generate a compatibility string.
	USkeletalMesh* SkelMesh = InSkelMeshComponent ? InSkelMeshComponent->SkeletalMesh.Get() : nullptr;
	if (SkelMesh && !DeformerAsset->GetInputInfo().IsCompatible(SkelMesh))
	{
		ErrorText += DeformerAsset->GetInputInfo().GenerateCompatibilityErrorString(SkelMesh);
		ErrorText += "\n";
		check(!ErrorText.IsEmpty());
		if (LogIssues)
		{
			UE_LOG(LogMLDeformer, Error, TEXT("ML Deformer '%s' isn't compatible with Skeletal Mesh '%s'.\nReason(s):\n%s"), 
				*DeformerAsset->GetName(), 
				*SkelMesh->GetName(), 
				*ErrorText);
		}
	}

	UNeuralNetwork* NeuralNetwork = DeformerAsset->GetInferenceNeuralNetwork();
	if (NeuralNetwork == nullptr || !NeuralNetwork->IsLoaded())
	{
		const FString NetworkErrorString = "The neural network needs to be trained.";
		ErrorText += NetworkErrorString + "\n";
		if (LogIssues)
		{
			UE_LOG(LogMLDeformer, Error, TEXT("Deformer '%s': %s"), *DeformerAsset->GetName(), *NetworkErrorString);
		}
	}
	else
	{
		const int64 NumNeuralNetInputs = NeuralNetwork->GetInputTensor().Num();
		const int64 NumDeformerAssetInputs = static_cast<int64>(DeformerAsset->GetInputInfo().CalcNumNeuralNetInputs());
		if (NumNeuralNetInputs != NumDeformerAssetInputs)
		{
			const FString InputErrorString = "The number of network inputs doesn't match the asset. Please retrain the asset."; 
			ErrorText += InputErrorString + "\n";
			if (LogIssues)
			{
				UE_LOG(LogMLDeformer, Error, TEXT("Deformer '%s': %s"), *DeformerAsset->GetName(), *InputErrorString);
			}
		}
	}

	return ErrorText;
}

int64 FMLDeformerInstance::SetBoneTransforms(float* OutputBuffer, int64 OutputBufferSize, int64 StartIndex)
{
	// Extract the component space bone transforms from the component.
	// Write the output transforms into the BoneTransforms array.
	BoneTransforms = SkeletalMeshComponent->GetBoneSpaceTransforms();

	// Write the transforms into the output buffer.
	const FMLDeformerInputInfo& InputInfo = DeformerAsset->GetInputInfo();
	const int32 AssetNumBones = InputInfo.GetNumBones();
	int64 Index = StartIndex;
	check((Index + AssetNumBones * 6) <= OutputBufferSize); // Make sure we don't write past the OutputBuffer. (6 because of two columns of the 3x3 rotation matrix)
	for (int32 BoneIndex = 0; BoneIndex < AssetNumBones; ++BoneIndex)
	{
		const int32 SkelMeshBoneIndex = AssetBonesToSkelMeshMappings[BoneIndex];
		const FMatrix RotationMatrix = (SkelMeshBoneIndex != INDEX_NONE) ? BoneTransforms[SkelMeshBoneIndex].GetRotation().ToMatrix() : FMatrix::Identity;
		const FVector X = RotationMatrix.GetColumn(0);
		const FVector Y = RotationMatrix.GetColumn(1);	
		OutputBuffer[Index++] = X.X;
		OutputBuffer[Index++] = X.Y;
		OutputBuffer[Index++] = X.Z;
		OutputBuffer[Index++] = Y.X;
		OutputBuffer[Index++] = Y.Y;
		OutputBuffer[Index++] = Y.Z;
	}

	return Index;
}

int64 FMLDeformerInstance::SetCurveValues(float* OutputBuffer, int64 OutputBufferSize, int64 StartIndex)
{
	const FMLDeformerInputInfo& InputInfo = DeformerAsset->GetInputInfo();

	// Write the weights into the output buffer.
	int64 Index = StartIndex;
	const int32 AssetNumCurves = InputInfo.GetNumCurves();
	check((Index + AssetNumCurves) <= OutputBufferSize); // Make sure we don't write past the OutputBuffer.

	// Write the curve weights to the output buffer.
	UAnimInstance* AnimInstance = SkeletalMeshComponent->GetAnimInstance();
	if (AnimInstance)
	{
		for (int32 CurveIndex = 0; CurveIndex < AssetNumCurves; ++CurveIndex)
		{
			const FName CurveName = InputInfo.GetCurveName(CurveIndex);
			const float CurveValue = AnimInstance->GetCurveValue(CurveName);	// Outputs 0.0 when not found.
			OutputBuffer[Index++] = CurveValue;
		}
	}
	else
	{
		for (int32 CurveIndex = 0; CurveIndex < AssetNumCurves; ++CurveIndex)
		{
			OutputBuffer[Index++] = 0.0f;
		}
	}

	return Index;
}

void FMLDeformerInstance::SetNeuralNetworkInputValues(float* InputData, int64 NumInputFloats)
{
	check(SkeletalMeshComponent);

	// Feed data to the network inputs.
	int64 BufferOffset = 0;
	BufferOffset = SetBoneTransforms(InputData, NumInputFloats, BufferOffset);
	BufferOffset = SetCurveValues(InputData, NumInputFloats, BufferOffset);
	check(BufferOffset == NumInputFloats);
}

void FMLDeformerInstance::Update()
{
	// Some safety checks.
	if (DeformerAsset == nullptr || 
		SkeletalMeshComponent == nullptr || 
		SkeletalMeshComponent->SkeletalMesh == nullptr || 
		!bIsCompatible)
	{
		return;
	}

	// Get the network and make sure it's loaded.
	UNeuralNetwork* NeuralNetwork = DeformerAsset->GetInferenceNeuralNetwork();
	if (NeuralNetwork == nullptr || !NeuralNetwork->IsLoaded())
	{
		return;
	}

	
	// If we're not on the GPU we can't continue really.
	// This is needed as the deformer graph system needs it on the GPU.
	// Some platforms might not support GPU yet.
	// Only the inputs are on the CPU.
	check(NeuralNetwork->GetInputDeviceType() == ENeuralDeviceType::CPU);
	if (NeuralNetwork->GetDeviceType() != ENeuralDeviceType::GPU ||
		NeuralNetwork->GetOutputDeviceType() != ENeuralDeviceType::GPU)
	{
		return;
	}

	// Allocate an inference context if none has already been allocated.
	if (NeuralNetworkInferenceHandle == -1)
	{
		NeuralNetworkInferenceHandle = NeuralNetwork->CreateInferenceContext();
	}
	if (NeuralNetworkInferenceHandle == -1)
	{
		return;
	}

	// If the neural network expects a different number of inputs, do nothing.
	const int64 NumNeuralNetInputs = NeuralNetwork->GetInputTensorForContext(NeuralNetworkInferenceHandle).Num();
	const int64 NumDeformerAssetInputs = static_cast<int64>(DeformerAsset->GetInputInfo().CalcNumNeuralNetInputs());
	if (NumNeuralNetInputs != NumDeformerAssetInputs)
	{
		return;
	}

	// Update and write the input values directly into the input tensor.
	float* InputDataPointer = (float*)NeuralNetwork->GetInputDataPointerMutableForContext(NeuralNetworkInferenceHandle);
	SetNeuralNetworkInputValues(InputDataPointer, NumNeuralNetInputs);

	ENQUEUE_RENDER_COMMAND(RunNeuralNetwork)(
		[NeuralNetwork, Handle = NeuralNetworkInferenceHandle](FRHICommandListImmediate& RHICmdList)
	{
			// Output deltas will be available on GPU for DeformerGraph via UMLDeformerDataProvider.
			FRDGBuilder GraphBuilder(RHICmdList);
			NeuralNetwork->Run(GraphBuilder, Handle);
			GraphBuilder.Execute();
	});
}
