// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectPtr.h"
#include "Containers/Array.h"
#include "Math/Transform.h"
#include "Animation/AnimTypes.h"

// Forward declarations.
class UMLDeformerAsset;
class UNeuralNetwork;
class USkeletalMeshComponent;

/**
 * This is an instance of the ML deformer, used during runtime.
 * There will be one such instance per actor.
 */
class MLDEFORMER_API FMLDeformerInstance
{
public:
	/**
	 * Initialize this instance for a given deformer and skeletal mesh component.
	 * @param Asset The ML Deformer asset.
	 */
	void Init(UMLDeformerAsset* Asset, USkeletalMeshComponent* SkelMeshComponent);

	/**
	 * Release the resources for this instance.
	 */
	void Release();

	/** 
	 * Update the deformer instance. 
	 * This updates the neural net input values and runs the actual neural network inference to calculate the output deltas.
	 */
	void Update();

	/**
	 * Get the current component space bone transforms that we grabbed from the skeletal mesh component.
	 * @return The cached bone transforms, in component space.
	 */
	const TArray<FTransform>& GetBoneTransforms() const { return BoneTransforms; }

	/**
	 * Is the deformer asset used compatible with the skeletal mesh component used during the Init call?
	 * @return True if compatible, false if not.
	 */
	bool IsCompatible() const { return bIsCompatible; }

	/**
	 * Get the compatibility error text. This will be a non-empty string in case IsCompatible() return false. 
	 * @return Returns the string containing more information about the reasons for compatibility issues.
	 */
	const FString& GetCompatibilityErrorText() const { return ErrorText; }

	/**
	 * Get the neural network inference handle.
	 * @return Returns the handle. This is -1 for an invalid handle.
	 */
	int32 GetNeuralNetworkInferenceHandle() const { return NeuralNetworkInferenceHandle; }

	/**
	 * Get the skeletal mesh component we're working with.
	 * @return A pointer to the skeletal mesh component.
	 */
	USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent; }

	/** Update the compatibility status, as returned by IsCompatible() and GetCompatibilityErrorText(). */
	void UpdateCompatibilityStatus();

private:
	/**
	 * Check whether the deformer is compatible with a given skeletal mesh component.
	 * This internally also edits the value returned by GetCompatibilityErrorText().
	 * @param InSkelMeshComponent The skeletal mesh component to check compatibility with.
	 * @param LogIssues Set to true to automatically log any compatibility errors.
	 * @return Returns the error string. When the returned string is empty, there were no errors and thus
	 *         the specified skeletal mesh component is compatible.
	 */
	FString CheckCompatibility(USkeletalMeshComponent* InSkelMeshComponent, bool LogIssues=false);

	/**
	 * Update the neural network input values directly inside its input tensor.
	 * This will copy over the bone transforms (and possibly morph and curve weights) into this flat array.
	 * @param InputData The buffer of floats to write the input values to.
	 * @param NumInputFloats The number of floats of the InputData buffer. Do not write more than this number of floats to the InputData buffer.
	 */
	void SetNeuralNetworkInputValues(float* InputData, int64 NumInputFloats);

	/**
	 * Set the bone transformations inside a given output buffer, starting from a given StartIndex.
	 * @param OutputBuffer The buffer we need to write the transformations to.
	 * @param OutputBufferSize The number of floats inside the output buffer. Can be used to detect buffer overflows.
	 * @param StartIndex The index where we want to start writing the transformations to.
	 * @return The new start index into the output buffer, to be used when you write more data after this.
	 */
	int64 SetBoneTransforms(float* OutputBuffer, int64 OutputBufferSize, int64 StartIndex);

	/**
	 * Set the animation curve values inside a given output buffer, starting from a given StartIndex.
	 * @param OutputBuffer The buffer we need to write the weights to.
	 * @param OutputBufferSize The number of floats inside the output buffer. Can be used to detect buffer overflows.
	 * @param StartIndex The index where we want to start writing the weights to.
	 * @return The new start index into the output buffer, to be used when you write more data after this.
	 */
	int64 SetCurveValues(float* OutputBuffer, int64 OutputBufferSize, int64 StartIndex);

private:
	/** The ML Deformer asset we're an instance of. */
	TObjectPtr<UMLDeformerAsset> DeformerAsset = nullptr;

	/** Inference handle for neural network inference */
	int32 NeuralNetworkInferenceHandle = -1;

	/** The skeletal mesh component. */
	TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent = nullptr;

	/** The cached current component space bone transforms. */
	TArray<FTransform> BoneTransforms;

	/** Maps the ML deformer asset bone index to a skeletal mesh component bone index. */
	TArray<int32> AssetBonesToSkelMeshMappings;

	/** The compatibility error text, in case bIsCompatible is false. */
	FString ErrorText;

	/** Are the deformer asset and the used skeletal mesh component compatible? */
	bool bIsCompatible = false;
};
