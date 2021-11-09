// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NeuralNetwork.h"

#include <atomic>
#include "ModelProto.h"
#include "NeuralOperator.h"
#include "UEOnly/NeuralTensorManager.h"

/* FImplBackEndUEOnly
 *****************************************************************************/

struct UNeuralNetwork::FImplBackEndUEOnly
{
	/**
	 * It should always be false when loaded from uasset (FNeuralTensors are not auto-loaded to GPU).
	 */
	bool bAreTensorsInGpu;

	FModelProto ModelProto;

	/**
	 * It contains a few TArray and TMaps for all FNeuralTensors (Input, Output, Intermediate(Not)Initialized, Weight).
	 */
	FNeuralTensorManager TensorManager;

	/**
	 * Only for the vanilla back end.
	 * Set of operators that the network need to run on the Forward pass and that might need to run on the PostForward pass.
	 */
	TArray<TSharedPtr<FNeuralOperator>> Operators;

	static bool Load(TSharedPtr<FImplBackEndUEOnly>& InOutImplBackEndUEOnly, const TArray<uint8>& InModelReadFromFileInBytes);
	static bool Load(TSharedPtr<FImplBackEndUEOnly>& InOutImplBackEndUEOnly, FNeuralTensorManager& InTensorManager, const TArray<TSharedPtr<FNeuralOperator>>& InOperators);

	//static bool Load(TSharedPtr<FImplBackEndUEOnly>& InOutImplBackEndUEOnly, FNeuralTensorManager& InTensorManager, const TArray<TSharedPtr<FNeuralOperator>>& InOperators);

	void Run(FOnAsyncRunCompleted& InOutOnAsyncRunCompletedDelegate, std::atomic<bool>& bInIsBackgroundThreadRunning, const ENeuralNetworkSynchronousMode InSynchronousMode, const ENeuralDeviceType InDeviceType, const ENeuralDeviceType InInputDeviceType, const ENeuralDeviceType InOutputDeviceType);

private:
	/**
	 *  Used by both Load() functions to reset InOutImplBackEndUEOnly.
	 */
	static void Reset(TSharedPtr<FImplBackEndUEOnly>& InOutImplBackEndUEOnly);
};
