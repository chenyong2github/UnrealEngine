// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NNECoreTypes.h"
#include "NNECoreRuntime.h"
#include "UObject/Interface.h"

#include "NNECoreRuntimeCPU.generated.h"

class UNNEModelData;

namespace UE::NNECore
{

struct NNXCORE_API FTensorBindingCPU
{
	void*	Data;
	uint64	SizeInBytes;
};

class NNXCORE_API IModelCPU
{
public:

	virtual ~IModelCPU() = default;

	/** Getters for tensor description as defined by the model potentially with variable dimensions() */
	virtual TConstArrayView<FTensorDesc> GetInputTensorDescs() const = 0;
	virtual TConstArrayView<FTensorDesc> GetOutputTensorDescs() const = 0;

	/** Getters for input shapes if they were set already (see SetInputTensorShapes). Empty list otherwise. */
	virtual TConstArrayView<FTensorShape> GetInputTensorShapes() const = 0;

	/** Getters for outputs shapes if they were already resolved. Empty list otherwise.
	* Output shape might be resolved after a call to SetInputTensorShapes() if the model and engine support it
	* otherwise they will be resolved during Run() or EnqueueRDG() */
	virtual TConstArrayView<FTensorShape> GetOutputTensorShapes() const = 0;
	
	/** Prepare the model to be run with the given input shape, The call is mandatory before Run or Enqueue() can be called. */
	virtual int SetInputTensorShapes(TConstArrayView<FTensorShape> InInputShapes) = 0;

	/** This call is synchronous on all engine types(CPU, RDG), i.e.the calling thread will be blocked until execution is finished.
	* Bindings should point to a buffers big enough */
	virtual int RunSync(TConstArrayView<FTensorBindingCPU> InInputTensors, TConstArrayView<FTensorBindingCPU> InOutputTensors) = 0;
};

} // UE::NNECore

UINTERFACE()
class NNXCORE_API UNNERuntimeCPU : public UInterface
{
	GENERATED_BODY()
};

class NNXCORE_API INNERuntimeCPU
{
	GENERATED_BODY()
	
public:
	virtual bool CanCreateModelCPU(TObjectPtr<UNNEModelData> ModelData) const = 0;
	virtual TUniquePtr<UE::NNECore::IModelCPU> CreateModelCPU(TObjectPtr<UNNEModelData> ModelData) = 0;
};