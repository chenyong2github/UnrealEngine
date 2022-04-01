// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerModelInstance.h"
#include "MLDeformerModel.h"
#include "MLDeformerModule.h"
#include "MLDeformerAsset.h"
#include "MLDeformerInputInfo.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "NeuralNetwork.h"

namespace UE::MLDeformer
{
	FMLDeformerModelInstance::FMLDeformerModelInstance(UMLDeformerModel* InModel)
		: Model(InModel)
	{
		check(InModel);
	}

	FMLDeformerModelInstance::~FMLDeformerModelInstance()
	{
		Release();
	}

	void FMLDeformerModelInstance::Release()
	{
		UNeuralNetwork* NeuralNetwork = Model->GetNeuralNetwork();
		if (NeuralNetwork != nullptr && NeuralNetworkInferenceHandle != -1)
		{
			NeuralNetwork->DestroyInferenceContext(NeuralNetworkInferenceHandle);
			NeuralNetworkInferenceHandle = -1;
		}
	}

	void FMLDeformerModelInstance::Init(USkeletalMeshComponent* SkelMeshComponent)
	{
		SkeletalMeshComponent = SkelMeshComponent;

		if (SkelMeshComponent == nullptr)
		{
			AssetBonesToSkelMeshMappings.Empty();
			return;
		}

		USkeletalMesh* SkelMesh = SkelMeshComponent->SkeletalMesh;
		if (SkelMesh)
		{
			// Init the bone mapping table.
			const UMLDeformerInputInfo* InputInfo = Model->GetInputInfo();
			const int32 NumAssetBones = InputInfo->GetNumBones();
			AssetBonesToSkelMeshMappings.Reset();
			AssetBonesToSkelMeshMappings.AddUninitialized(NumAssetBones);

			// For each bone in the deformer asset, find the matching bone index inside the skeletal mesh component.
			for (int32 Index = 0; Index < NumAssetBones; ++Index)
			{
				const FName BoneName = InputInfo->GetBoneName(Index);
				const int32 SkelMeshBoneIndex = SkeletalMeshComponent->GetBoneIndex(BoneName);
				AssetBonesToSkelMeshMappings[Index] = SkelMeshBoneIndex;
			}
		}

		// Perform a compatibility check.
		UpdateCompatibilityStatus();
	}

	void FMLDeformerModelInstance::UpdateCompatibilityStatus()
	{
		bIsCompatible = SkeletalMeshComponent->SkeletalMesh && CheckCompatibility(SkeletalMeshComponent, true).IsEmpty();
	}

	FString FMLDeformerModelInstance::CheckCompatibility(USkeletalMeshComponent* InSkelMeshComponent, bool LogIssues)
	{
		ErrorText = FString();

		// If we're not compatible, generate a compatibility string.
		USkeletalMesh* SkelMesh = InSkelMeshComponent ? InSkelMeshComponent->SkeletalMesh.Get() : nullptr;
		UMLDeformerInputInfo* InputInfo = Model->GetInputInfo();
		if (SkelMesh && !InputInfo->IsCompatible(SkelMesh))
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
		if (NeuralNetwork != nullptr && NeuralNetwork->IsLoaded())
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
		}

		return ErrorText;
	}

	int64 FMLDeformerModelInstance::SetBoneTransforms(float* OutputBuffer, int64 OutputBufferSize, int64 StartIndex)
	{
		// Extract the bone space bone transforms from the component.
		// Write the output transforms into the BoneTransforms array.
		BoneTransforms = SkeletalMeshComponent->GetBoneSpaceTransforms();

		// Write the transforms into the output buffer.
		const UMLDeformerInputInfo* InputInfo = Model->GetInputInfo();
		const int32 AssetNumBones = InputInfo->GetNumBones();
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

	int64 FMLDeformerModelInstance::SetCurveValues(float* OutputBuffer, int64 OutputBufferSize, int64 StartIndex)
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

	void FMLDeformerModelInstance::SetNeuralNetworkInputValues(float* InputData, int64 NumInputFloats)
	{
		check(SkeletalMeshComponent);

		// Feed data to the network inputs.
		int64 BufferOffset = 0;
		BufferOffset = SetBoneTransforms(InputData, NumInputFloats, BufferOffset);
		BufferOffset = SetCurveValues(InputData, NumInputFloats, BufferOffset);
		check(BufferOffset == NumInputFloats);
	}

	bool FMLDeformerModelInstance::IsValidForDataProvider() const
	{
		const UNeuralNetwork* NeuralNetwork = Model->GetNeuralNetwork();
		if (NeuralNetwork == nullptr || 
			!NeuralNetwork->IsLoaded() || 
			NeuralNetwork->GetDeviceType() != ENeuralDeviceType::GPU || 
			NeuralNetwork->GetOutputDeviceType() != ENeuralDeviceType::GPU)
		{
			return false;
		}

		return
			Model->GetVertexMapBuffer().ShaderResourceViewRHI != nullptr &&
			GetNeuralNetworkInferenceHandle() != -1;
	}

	void FMLDeformerModelInstance::Tick(float DeltaTime)
	{
		// Some safety checks.
		if (Model == nullptr || 
			SkeletalMeshComponent == nullptr || 
			SkeletalMeshComponent->SkeletalMesh == nullptr || 
			!bIsCompatible)
		{
			return;
		}

		// Get the network and make sure it's loaded.
		UNeuralNetwork* NeuralNetwork = Model->GetNeuralNetwork();
		if (NeuralNetwork == nullptr || !NeuralNetwork->IsLoaded())
		{
			return;
		}

		// If we're not on the GPU we can't continue really.
		// This is needed as the deformer graph system needs it on the GPU.
		// Some platforms might not support GPU yet.
		// Only the inputs are on the CPU.
		check(NeuralNetwork->GetInputDeviceType() == ENeuralDeviceType::CPU);
		if (!bAllowCPU)
		{
			if (NeuralNetwork->GetDeviceType() != ENeuralDeviceType::GPU ||
				NeuralNetwork->GetOutputDeviceType() != ENeuralDeviceType::GPU)
			{
				return;
			}
		}

		// Allocate an inference context if none has already been allocated.
		if (NeuralNetworkInferenceHandle == -1)
		{
			NeuralNetworkInferenceHandle = NeuralNetwork->CreateInferenceContext();
			if (NeuralNetworkInferenceHandle == -1)
			{
				return;
			}
		}

		// If the neural network expects a different number of inputs, do nothing.
		const int64 NumNeuralNetInputs = NeuralNetwork->GetInputTensorForContext(NeuralNetworkInferenceHandle).Num();
		const int64 NumDeformerAssetInputs = Model->GetInputInfo()->CalcNumNeuralNetInputs();
		if (NumNeuralNetInputs != NumDeformerAssetInputs)
		{
			return;
		}

		// Update and write the input values directly into the input tensor.
		float* InputDataPointer = static_cast<float*>(NeuralNetwork->GetInputDataPointerMutableForContext(NeuralNetworkInferenceHandle));
		SetNeuralNetworkInputValues(InputDataPointer, NumNeuralNetInputs);

		// Run the neural network.
		if (NeuralNetwork->GetOutputDeviceType() == ENeuralDeviceType::GPU)
		{
			ENQUEUE_RENDER_COMMAND(RunNeuralNetwork)(
				[NeuralNetwork, Handle = NeuralNetworkInferenceHandle](FRHICommandListImmediate& RHICmdList)
			{
					// Output deltas will be available on GPU for DeformerGraph via UMLDeformerDataProvider.
					FRDGBuilder GraphBuilder(RHICmdList);
					NeuralNetwork->Run(GraphBuilder, Handle);
					GraphBuilder.Execute();
			});
		}
		else
		{
			NeuralNetwork->Run();
		}
	}
}	// namespace UE::MLDeformer
