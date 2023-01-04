// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "UObject/Object.h"
#include "NeuralMorphModel.h"
#include "NeuralMorphNetwork.generated.h"

class UNeuralMorphNetworkInstance;

/** A fully connected layer, which contains the weights and biases for those connections. */ 
UCLASS()
class NEURALMORPHMODEL_API UNeuralMorphNetworkLayer
	: public UObject
{
	GENERATED_BODY()

public:
	/** The weight matrix number of inputs (rows). */
	UPROPERTY()
	int32 NumInputs = 0;

	/** The weight matrix number of outputs (columns). */
	UPROPERTY()
	int32 NumOutputs = 0;

	/** The third dimension of the layer. This basically contains the number of bones in local mode. */
	UPROPERTY()
	int32 Depth = 1;

	/** The weights, which is basically a 2d array. The number of weights will be equal to the number of rows multiplied by number of columns multiplied by the depth. */
	UPROPERTY()
	TArray<float> Weights;

	/** The biases. The number of biases will be the same as the number of columns multiplied by the depth. */
	UPROPERTY()
	TArray<float> Biases;
};

/**
 * The specialized neural network for the Neural Morph Model.
 * This class is used to do inference at runtime at a higher performance than using UNeuralNetwork.
 * The reason why it is faster is because it is a highly specialized network for this specific model.
 */
UCLASS()
class NEURALMORPHMODEL_API UNeuralMorphNetwork
	: public UObject
{
	GENERATED_BODY()

public:
	/** Clear the network, getting rid of all weights and biases. */
	void Empty();

	/** 
	 * Check if the network is empty or not.
	 * If it is empty, it means it hasn't been loaded, and cannot do anything.
	 * Empty means it has no inputs, outputs or weights.
	 * @return Returns true if empty, otherwise false is returned.
	 */
	bool IsEmpty() const;

	/**
	 * Load the network from a file on disk.
	 * When loading fails, the network will be emptied.
	 * @param Filename The file path to load from.
	 * @return Returns true when successfully loaded this model, otherwise false is returned.
	 */
	bool Load(const FString& Filename);

	/**
	 * Create an instance of this neural network.
	 * @return A pointer to the neural network instance.
	 */
	UNeuralMorphNetworkInstance* CreateInstance();

	/**
	 * Get the number of inputs, which is the number of floats the network takes as input.
	 * @result The number of input floats to the network.
	 */
	int32 GetNumInputs() const;

	/**
	 * Get the number of outputs, which is the number of floats the network will output.
	 * @return The number of floats that the network outputs.
	 */
	int32 GetNumOutputs() const;

	/**
	 * Get the number of bones that are input to the network.
	 * @return The number of bones.
	 */
	int32 GetNumBones() const;

	/**
	 * Get the number of curves that are input to the network.
	 * @return The number of curves.
	 */
	int32 GetNumCurves() const;

	/**
	 * Get the number of morph targets per bone. This will only be valid if the GetMode() method returns the local model mode.
	 * For global mode networks, this will return 0.
	 * @return The number of morph targets per bone.
	 */
	int32 GetNumMorphsPerBone() const;

	/**
	 * Get the mode that the model was trained for, either global or local mode.
	 * @return The mode the model was trained for.
	 */
	ENeuralMorphMode GetMode() const;

	/**
	 * Get the means of each input, used for normalizing the input values.
	 * @return The array of mean values, one value for each network input.
	 */
	const TArrayView<const float> GetInputMeans() const;

	/**
	 * Get the standard deviations of each input, used for normalizing the input values.
	 * @return The array that contains the standard deviations, one value for each network input.
	 */
	const TArrayView<const float> GetInputStds() const;

	/**
	 * Get the number of network layers.
	 * This equals to the number of hidden layers plus one.
	 * @return The number of network layer.
	 */
	int32 GetNumLayers() const;

	/**
	 * Get a given network layer.
	 * @return A reference to the layer, which will contain the weights and biases.
	 */
	UNeuralMorphNetworkLayer& GetLayer(int32 Index) const;

	/** 
	 * Get the number of floats used to represent a single curve value.
	 * @return The number of float values per curve.
	 */
	int32 GetNumFloatsPerCurve() const;

private:
	/** The network weights and biases, between the different layers. */
	UPROPERTY()
	TArray<TObjectPtr<UNeuralMorphNetworkLayer>> Layers;

	/** The means of the input values, used to normalize inputs. */
	UPROPERTY()
	TArray<float> InputMeans;

	/** The standard deviation of the input values, used to normalize inputs. */
	UPROPERTY()
	TArray<float> InputStd;

	/** The mode of the network, either local or global. */
	UPROPERTY()
	ENeuralMorphMode Mode = ENeuralMorphMode::Global;

	/** The number of morph targets per bone, if set Mode == Local, otherwise ignored. */
	UPROPERTY()
	int32 NumMorphsPerBone = 0;

	/** The number of bones that were input. */
	UPROPERTY()
	int32 NumBones = 0;

	/** The number of curves that were input. */
	UPROPERTY()
	int32 NumCurves = 0;

	/** The number of floats per curve. */
	UPROPERTY()
	int32 NumFloatsPerCurve = 1;
};

/** 
 * An instance of a UNeuralMorphNetwork.
 * The instance holds its own input and output buffers and only reads from the network object it was instanced from.
 * This allows it to be multithreaded.
 */
UCLASS()
class NEURALMORPHMODEL_API UNeuralMorphNetworkInstance
	: public UObject
{
	friend class UNeuralMorphNetwork;

	GENERATED_BODY()

public:
	/**
	 * Get the network input buffer.
	 * @return The array view of floats that represents the network inputs.
	 */
	TArrayView<float> GetInputs();
	TArrayView<const float> GetInputs() const;

	/**
	 * Get the network output buffer.
	 * @return The array view of floats that represents the network outputs.
	 */
	TArrayView<float> GetOutputs();
	TArrayView<const float> GetOutputs() const;

	/**
	 * Get the neural network this is an instance of.
	 * @return A reference to the neural network.
	 */
	const UNeuralMorphNetwork& GetNeuralNetwork() const;

	/**
	 * Run the neural network, performing inference.
	 * This will update the values in the output buffer that you can get with the GetOutputs method.
	 * This also assumes you have set all the right input values already.
	 */
	void Run();

private:
	struct FRunSettings
	{
		float* TempInputBuffer;
		float* TempOutputBuffer;
		const float* InputStdsBuffer;
		const float* InputMeansBuffer;
		const float* InputBuffer;
		float* OutputBuffer;
	};

	/**
 	 * Run inference using the global model mode, which is one fully connected MLP.
	 * @param RunSettings The settings used to run the model. This specifies a set of buffers.
	 */
	void RunGlobalModel(const FRunSettings& RunSettings);

	/**
 	 * Run inference using the local model mode, which basically has a small MLP for every bone.
	 * @param RunSettings The settings used to run the model. This specifies a set of buffers.
	 */
	void RunLocalModel(const FRunSettings& RunSettings);

	/**
 	 * Initialize this instance using a given network object.
	 * This is automatically called by the UNeuralMorphNetwork::CreateInstance() method.
	 * @param InNeuralNetwork The network that this is an instance of.
	 */
	void Init(UNeuralMorphNetwork* InNeuralNetwork);

private:
	/** The input values. */
	TArray<float> Inputs;

	/** The output values. */
	TArray<float> Outputs;

	/** A pre-allocated temp buffer for inputs. */
	TArray<float> TempInputArray;

	/** A pre-allocated temp buffer for outputs. */
	TArray<float> TempOutputArray;

	/** The neural network this is an instance of. */
	UPROPERTY(Transient)
	TObjectPtr<UNeuralMorphNetwork> Network;
};
