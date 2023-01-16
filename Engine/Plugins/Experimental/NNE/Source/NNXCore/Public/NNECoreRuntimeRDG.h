// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NNECoreTypes.h"
#include "RenderGraphFwd.h"
#include "UObject/Interface.h"

#include "NNECoreRuntimeRDG.generated.h"

class UNNEModelData;

namespace UE::NNECore
{

struct NNXCORE_API FTensorBindingRDG
{
	FRDGBufferRef Buffer;
};

class NNXCORE_API IModelRDG
{
public:

	virtual ~IModelRDG() = default;

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

	/** Enqueue the execution on the Render graph render thread. It's caller's responsibility to actually run the graph.
	* Bindings should point to a buffers big enough */
	virtual bool EnqueueRDG(FRDGBuilder& RDGBuilder, TConstArrayView<FTensorBindingRDG> Inputs, TConstArrayView<FTensorBindingRDG> Outputs) = 0;
};

} // UE::NNECore

UINTERFACE()
class NNXCORE_API UNNERuntimeRDG : public UInterface
{
	GENERATED_BODY()
};

class NNXCORE_API INNERuntimeRDG
{
	GENERATED_BODY()

public:
	virtual bool CanCreateModelRDG(TConstArrayView<uint8> Data) const = 0;
	virtual TUniquePtr<UE::NNECore::IModelRDG> CreateModelRDG(TConstArrayView<uint8> Data) = 0;
};