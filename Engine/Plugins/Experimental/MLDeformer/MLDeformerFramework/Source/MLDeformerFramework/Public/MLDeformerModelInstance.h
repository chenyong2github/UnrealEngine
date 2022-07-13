// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MLDeformerModule.h"
#include "UObject/ObjectPtr.h"
#include "MLDeformerModelInstance.generated.h"

class UMLDeformerModel;
class UNeuralNetwork;
class USkeletalMeshComponent;

UCLASS()
class MLDEFORMERFRAMEWORK_API UMLDeformerModelInstance
	: public UObject
{
	GENERATED_BODY()

public:
	UMLDeformerModelInstance() = default;

	// UObject overrides.
	virtual void BeginDestroy() override;
	// ~END UObject overrides.
	
	/**
	 * Initialize the model instance.
	 * This internally builds the structures to quickly grab bone transforms.
	 * If you override this method, you probably also want to call the Init of this base class inside your overloaded method.
	 * @param SkelMeshComponent The skeletal mesh component that we will grab bone transforms and curve values from.
	 */
	virtual void Init(USkeletalMeshComponent* SkelMeshComponent);

	/**
	 * Release all built lookup tables etc.
	 * Also call this base class method if you override this method.
	 */
	virtual void Release();

	/**
	 * Update the deformer instance.
	 * If you use a neural network, this should provide inputs to the neural network.
	 * @param DeltaTime The delta time since the last frame, in seconds.
	 * @param ModelWeight The weight of the model, between 0 and 1, where 0 means it has no influence, and 1 means it is fully active.
	 */
	virtual void Tick(float DeltaTime, float ModelWeight);

	/**
	 * Update the neural network input values directly inside its input tensor.
	 * This will copy over the bone transforms (and possibly morph and curve weights) into this flat array.
	 * @param InputData The buffer of floats to write the input values to.
	 * @param NumInputFloats The number of floats of the InputData buffer. Do not write more than this number of floats to the InputData buffer.
	 * @return Returns the new buffer element index. For example if there were 2 bones and 3 curves, and each bone takes 6 inputs and each curve takes one 
	 * input, then after calling this base class method, it will return 15 (2*6 + 3*1). You could call this base class method and then write data after that, starting
	 * from the offset that is returned.
	 */
	virtual int64 SetNeuralNetworkInputValues(float* InputData, int64 NumInputFloats);

	/**
	 * Check whether the deformer is compatible with a given skeletal mesh component.
	 * This internally also edits the value returned by GetCompatibilityErrorText().
	 * @param InSkelMeshComponent The skeletal mesh component to check compatibility with.
	 * @param LogIssues Set to true to automatically log any compatibility errors.
	 * @return Returns the error string. When the returned string is empty, there were no errors and thus
	 *         the specified skeletal mesh component is compatible.
	 */
	virtual FString CheckCompatibility(USkeletalMeshComponent* InSkelMeshComponent, bool LogIssues=false);

	/**
	 * Check if we are in a valid state for the deformer graph's data provider.
	 * In the base class this will check whether the neural network is valid, if the vertex map buffer is valid
	 * and whether the neural network instance handle is valid.
	 * @return Returns true if the data provider in would be in a valid state, otherwise false is returned.
	 */
	virtual bool IsValidForDataProvider() const;

	/**
	 * Setup the neural network for this frame.
	 * This has to perform compatibility checks, valid pointer checks, and call the SetNeuralNetworkInputValues.
	 * After this method is executed, the neural network should be ready to execute.
	 * The UMLDeformerModelInstance::Tick method internally calls both the SetupNeuralNetworkForFrame and RunNeuralNetwork methods.
	 * @return Returns true when the setup is done correctly and the neural network is ready to be executed. Otherwise false is returned, which 
	 * can happen when the NeuralNetwork pointer is invalid, when the model is not set, when the network is not compatible, etc.
	 */
	virtual bool SetupNeuralNetworkForFrame();

	/**
	 * Run the neural network.
	 * This already assumes that compatibility checks are done, and that the network inputs are set etc.
	 * Internally this will execute the UNeuralNetwork::Run() method, either on GPU or CPU.
	 * @param ModelWeight The weight of the model, must be between 0 and 1.
	 */
	virtual void RunNeuralNetwork(float ModelWeight);

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
	 * Get the skeletal mesh component we're working with.
	 * @return A pointer to the skeletal mesh component.
	 */
	USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent; }

	/** Update the compatibility status, as returned by IsCompatible() and GetCompatibilityErrorText(). */
	void UpdateCompatibilityStatus();

	/** Get the model that this is an instance of. */
	UMLDeformerModel* GetModel() const { return Model.Get(); }

	/** Set the deformer model that this is an instance of. */
	void SetModel(UMLDeformerModel* InModel) { Model = InModel; }

	/**
	 * Get the neural network inference handle.
	 * @return Returns the handle. This is -1 for an invalid handle.
	 */
	int32 GetNeuralNetworkInferenceHandle() const { return NeuralNetworkInferenceHandle; }

protected:
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

	/**
	 * Updates the bone transforms array.
	 */
	void UpdateBoneTransforms();

public:
	/** The ML Deformer model that this is an instance of. */
	UPROPERTY(Transient)
	TObjectPtr<UMLDeformerModel> Model = nullptr;

protected:
	/** The skeletal mesh component we work with. This is mainly used for compatibility checks. */
	TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent = nullptr;

	/** The cached current local space bone transforms for the current frame. */
	TArray<FTransform> TrainingBoneTransforms;

	/** A temp array of bone transforms. */
	TArray<FTransform> BoneTransforms;

	/** Maps the ML deformer asset bone index to a skeletal mesh component bone index. */
	TArray<int32> AssetBonesToSkelMeshMappings;

	/** The compatibility error text, in case bIsCompatible is false. */
	FString ErrorText;

	/** Inference handle for neural network inference. */
	int32 NeuralNetworkInferenceHandle = -1;

	/** Are the deformer asset and the used skeletal mesh component compatible? */
	bool bIsCompatible = false;
};
