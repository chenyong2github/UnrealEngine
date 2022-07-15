// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerModelInstance.h"
#include "MLDeformerModel.h"
#include "MLDeformerModule.h"
#include "MLDeformerAsset.h"
#include "MLDeformerInputInfo.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "NeuralNetwork.h"

void UMLDeformerModelInstance::BeginDestroy()
{
	Release();
	Super::BeginDestroy();
}

void UMLDeformerModelInstance::Release()
{
	UNeuralNetwork* NeuralNetwork = Model.Get() ? Model->GetNeuralNetwork() : nullptr;
	if (NeuralNetwork != nullptr && NeuralNetworkInferenceHandle != -1)
	{
		NeuralNetwork->DestroyInferenceContext(NeuralNetworkInferenceHandle);
		NeuralNetworkInferenceHandle = -1;
	}
}

void UMLDeformerModelInstance::Init(USkeletalMeshComponent* SkelMeshComponent)
{
	SkeletalMeshComponent = SkelMeshComponent;

	if (SkelMeshComponent == nullptr)
	{
		AssetBonesToSkelMeshMappings.Empty();
		return;
	}

	USkeletalMesh* SkelMesh = SkelMeshComponent->GetSkeletalMesh();
	if (SkelMesh)
	{
		// Init the bone mapping table.
		const UMLDeformerInputInfo* InputInfo = Model->GetInputInfo();
		const int32 NumAssetBones = InputInfo->GetNumBones();
		AssetBonesToSkelMeshMappings.Reset();
		AssetBonesToSkelMeshMappings.AddUninitialized(NumAssetBones);
		TrainingBoneTransforms.SetNumUninitialized(NumAssetBones);

		// For each bone in the deformer asset, find the matching bone index inside the skeletal mesh component.
		for (int32 Index = 0; Index < NumAssetBones; ++Index)
		{
			const FName BoneName = InputInfo->GetBoneName(Index);
			const int32 SkelMeshBoneIndex = SkeletalMeshComponent->GetBaseComponent()->GetBoneIndex(BoneName);
			AssetBonesToSkelMeshMappings[Index] = SkelMeshBoneIndex;
		}
	}

	// Perform a compatibility check.
	UpdateCompatibilityStatus();
}

void UMLDeformerModelInstance::UpdateCompatibilityStatus()
{
	bIsCompatible = SkeletalMeshComponent->GetSkeletalMesh() && CheckCompatibility(SkeletalMeshComponent, true).IsEmpty();
}

FString UMLDeformerModelInstance::CheckCompatibility(USkeletalMeshComponent* InSkelMeshComponent, bool LogIssues)
{
	ErrorText = FString();

	// If we're not compatible, generate a compatibility string.
	USkeletalMesh* SkelMesh = InSkelMeshComponent ? InSkelMeshComponent->GetSkeletalMesh() : nullptr;
	UMLDeformerInputInfo* InputInfo = Model->GetInputInfo();
	if (SkelMesh && !InputInfo->IsCompatible(SkelMesh) && Model->GetDeformerAsset())
	{
		ErrorText += InputInfo->GenerateCompatibilityErrorString(SkelMesh);
		ErrorText += "\n";
		check(!ErrorText.IsEmpty());
		if (LogIssues)
		{
			UE_LOG(LogMLDeformer, Error, TEXT("ML Deformer '%s' isn't compatible with Skeletal Mesh '%s'.\nReason(s):\n%s"), 
				*Model->GetDeformerAsset()->GetName(), 
				*SkelMesh->GetName(), 
				*ErrorText);
		}
	}

	UNeuralNetwork* NeuralNetwork = Model->GetNeuralNetwork();
	if (NeuralNetwork != nullptr && NeuralNetwork->IsLoaded() && Model->GetDeformerAsset())
	{
		const int64 NumNeuralNetInputs = NeuralNetwork->GetInputTensor().Num();
		const int64 NumDeformerAssetInputs = static_cast<int64>(Model->GetInputInfo()->CalcNumNeuralNetInputs());
		if (NumNeuralNetInputs != NumDeformerAssetInputs)
		{
			const FString InputErrorString = "The number of network inputs doesn't match the asset. Please retrain the asset."; 
			ErrorText += InputErrorString + "\n";
			if (LogIssues)
			{
				UE_LOG(LogMLDeformer, Error, TEXT("Deformer '%s': %s"), *(Model->GetDeformerAsset()->GetName()), *InputErrorString);
			}
		}

		// Check if the neural network is on the right device, if not, we can't continue.
		if (Model->IsNeuralNetworkOnGPU())
		{
			if (NeuralNetwork->GetDeviceType() != ENeuralDeviceType::GPU || NeuralNetwork->GetOutputDeviceType() != ENeuralDeviceType::GPU || NeuralNetwork->GetInputDeviceType() != ENeuralDeviceType::CPU)
			{
				const FString DeviceErrorString = "The neural network is expected to run and output on the GPU, but it isn't.";
				ErrorText += DeviceErrorString + "\n";
				if (LogIssues)
				{
					UE_LOG(LogMLDeformer, Error, TEXT("Deformer '%s': %s"), *(Model->GetDeformerAsset()->GetName()), *DeviceErrorString);
				}
			}
		}
		else
		{
			if (NeuralNetwork->GetDeviceType() != ENeuralDeviceType::CPU || NeuralNetwork->GetOutputDeviceType() != ENeuralDeviceType::CPU || NeuralNetwork->GetInputDeviceType() != ENeuralDeviceType::CPU)
			{
				const FString DeviceErrorString = "The neural network is expected to run fully on the CPU, but it isn't.";
				ErrorText += DeviceErrorString + "\n";
				if (LogIssues)
				{
					UE_LOG(LogMLDeformer, Error, TEXT("Deformer '%s': %s"), *(Model->GetDeformerAsset()->GetName()), *DeviceErrorString);
				}
			}
		}
	}

	return ErrorText;
}

void UMLDeformerModelInstance::UpdateBoneTransforms()
{
	const USkinnedMeshComponent* LeaderPoseComponent = SkeletalMeshComponent->LeaderPoseComponent.Get();
	if (LeaderPoseComponent)
	{
		const TArray<FTransform>& LeaderTransforms = LeaderPoseComponent->GetComponentSpaceTransforms();
		USkinnedAsset* SkinnedAsset = LeaderPoseComponent->GetSkinnedAsset();
		const int32 NumTrainingBones = AssetBonesToSkelMeshMappings.Num();
		for (int32 Index = 0; Index < NumTrainingBones; ++Index)
		{
			const int32 ComponentBoneIndex = AssetBonesToSkelMeshMappings[Index];
			const FTransform& ComponentSpaceTransform = LeaderTransforms[ComponentBoneIndex];
			const int32 ParentIndex = SkinnedAsset->GetRefSkeleton().GetParentIndex(ComponentBoneIndex);
			if (ParentIndex != INDEX_NONE)
			{
				TrainingBoneTransforms[Index] = ComponentSpaceTransform.GetRelativeTransform(LeaderTransforms[ParentIndex]);
			}
			else
			{
				TrainingBoneTransforms[Index] = ComponentSpaceTransform;
			}
			TrainingBoneTransforms[Index].NormalizeRotation();
		}
	}
	else
	{
		BoneTransforms = SkeletalMeshComponent->GetBoneSpaceTransforms();
		const int32 NumTrainingBones = AssetBonesToSkelMeshMappings.Num();
		for (int32 Index = 0; Index < NumTrainingBones; ++Index)
		{
			const int32 ComponentBoneIndex = AssetBonesToSkelMeshMappings[Index];
			TrainingBoneTransforms[Index] = BoneTransforms[ComponentBoneIndex];
		}
	}
}

int64 UMLDeformerModelInstance::SetBoneTransforms(float* OutputBuffer, int64 OutputBufferSize, int64 StartIndex)
{
	// Get the transforms for the bones we used during training.
	// These are in the space relative to their parent.
	UpdateBoneTransforms();

	// Write the transforms into the output buffer.
	const UMLDeformerInputInfo* InputInfo = Model->GetInputInfo();
	const int32 AssetNumBones = InputInfo->GetNumBones();
	int64 Index = StartIndex;
	check((Index + AssetNumBones * 6) <= OutputBufferSize); // Make sure we don't write past the OutputBuffer. (6 because of two columns of the 3x3 rotation matrix)
	for (int32 BoneIndex = 0; BoneIndex < AssetNumBones; ++BoneIndex)
	{
		const FMatrix RotationMatrix = TrainingBoneTransforms[BoneIndex].GetRotation().ToMatrix();
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

int64 UMLDeformerModelInstance::SetCurveValues(float* OutputBuffer, int64 OutputBufferSize, int64 StartIndex)
{
	const UMLDeformerInputInfo* InputInfo = Model->GetInputInfo();

	// Write the weights into the output buffer.
	int64 Index = StartIndex;
	const int32 AssetNumCurves = InputInfo->GetNumCurves();
	check((Index + AssetNumCurves) <= OutputBufferSize); // Make sure we don't write past the OutputBuffer.

	// Write the curve weights to the output buffer.
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
		for (int32 CurveIndex = 0; CurveIndex < AssetNumCurves; ++CurveIndex)
		{
			OutputBuffer[Index++] = 0.0f;
		}
	}

	return Index;
}

int64 UMLDeformerModelInstance::SetNeuralNetworkInputValues(float* InputData, int64 NumInputFloats)
{
	check(SkeletalMeshComponent);

	// Feed data to the network inputs.
	int64 BufferOffset = 0;
	BufferOffset = SetBoneTransforms(InputData, NumInputFloats, BufferOffset);
	BufferOffset = SetCurveValues(InputData, NumInputFloats, BufferOffset);
	return BufferOffset;
}

bool UMLDeformerModelInstance::IsValidForDataProvider() const
{
	const UNeuralNetwork* NeuralNetwork = Model->GetNeuralNetwork();
	if (NeuralNetwork == nullptr || !NeuralNetwork->IsLoaded())
	{
		return false;
	}

	// We expect to run on the GPU when using a data provider for the deformer graph system (Optimus).
	if (Model->IsNeuralNetworkOnGPU())
	{
		// Make sure we're actually running the network on the GPU.
		// Inputs are expected to come from the CPU though.
		if (NeuralNetwork->GetDeviceType() != ENeuralDeviceType::GPU || NeuralNetwork->GetOutputDeviceType() != ENeuralDeviceType::GPU || NeuralNetwork->GetInputDeviceType() != ENeuralDeviceType::CPU)
		{
			return false;
		}
	}

	return (Model->GetVertexMapBuffer().ShaderResourceViewRHI != nullptr) && (GetNeuralNetworkInferenceHandle() != -1);
}

void UMLDeformerModelInstance::RunNeuralNetwork(float ModelWeight)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UMLDeformerModelInstance::RunNeuralNetwork)

	UNeuralNetwork* NeuralNetwork = Model->GetNeuralNetwork();
	if (Model->IsNeuralNetworkOnGPU())
	{
		// NOTE: Inputs still come from the CPU.
		check(NeuralNetwork->GetDeviceType() == ENeuralDeviceType::GPU && NeuralNetwork->GetInputDeviceType() == ENeuralDeviceType::CPU && NeuralNetwork->GetOutputDeviceType() == ENeuralDeviceType::GPU);
		ENQUEUE_RENDER_COMMAND(RunNeuralNetwork)
			(
				[NeuralNetwork, Handle = NeuralNetworkInferenceHandle](FRHICommandListImmediate& RHICmdList)
				{
					// Output deltas will be available on GPU for DeformerGraph via UMLDeformerDataProvider.
					FRDGBuilder GraphBuilder(RHICmdList);
					NeuralNetwork->Run(GraphBuilder, Handle);
					GraphBuilder.Execute();
				}
		);
	}
	else
	{
		// Run on the CPU.
		check(NeuralNetwork->GetDeviceType() == ENeuralDeviceType::CPU && NeuralNetwork->GetInputDeviceType() == ENeuralDeviceType::CPU && NeuralNetwork->GetOutputDeviceType() == ENeuralDeviceType::CPU);
		NeuralNetwork->Run(NeuralNetworkInferenceHandle);
	}
}

bool UMLDeformerModelInstance::SetupNeuralNetworkForFrame()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UMLDeformerModelInstance::SetupNeuralNetworkForFrame)

	// Some safety checks.
	if (Model == nullptr ||
		SkeletalMeshComponent == nullptr ||
		SkeletalMeshComponent->GetSkeletalMesh() == nullptr ||
		!bIsCompatible)
	{
		return false;
	}

	// Get the network and make sure it's loaded.
	UNeuralNetwork* NeuralNetwork = Model->GetNeuralNetwork();
	if (NeuralNetwork == nullptr || !NeuralNetwork->IsLoaded())
	{
		return false;
	}

	// Allocate an inference context if none has already been allocated.
	if (NeuralNetworkInferenceHandle == -1)
	{
		NeuralNetworkInferenceHandle = NeuralNetwork->CreateInferenceContext();
		if (NeuralNetworkInferenceHandle == -1)
		{
			return false;
		}
	}

	// If the neural network expects a different number of inputs, do nothing.
	const int64 NumNeuralNetInputs = NeuralNetwork->GetInputTensorForContext(NeuralNetworkInferenceHandle).Num();
	const int64 NumDeformerAssetInputs = Model->GetInputInfo()->CalcNumNeuralNetInputs();
	if (NumNeuralNetInputs != NumDeformerAssetInputs)
	{
		return false;
	}

	// Update and write the input values directly into the input tensor.
	float* InputDataPointer = static_cast<float*>(NeuralNetwork->GetInputDataPointerMutableForContext(NeuralNetworkInferenceHandle));
	const int64 NumFloatsWritten = SetNeuralNetworkInputValues(InputDataPointer, NumNeuralNetInputs);
	check(NumFloatsWritten == NumNeuralNetInputs);

	return true;
}


void UMLDeformerModelInstance::Tick(float DeltaTime, float ModelWeight)
{
	if (!SetupNeuralNetworkForFrame())
	{
		return;
	}

	RunNeuralNetwork(ModelWeight);
}
