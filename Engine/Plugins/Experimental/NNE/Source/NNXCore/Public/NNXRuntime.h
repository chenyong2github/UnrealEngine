// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNECoreTypes.h"
#include "CoreMinimal.h"

class FRDGBuffer;
class FRDGBuilder;
class UMLInferenceModel;

namespace NNX
{
using FTensorDesc = UE::NNECore::FTensorDesc;
using FTensorShape = UE::NNECore::FTensorShape;

class FMLInferenceModel;

/** Runtime support flags */
enum class EMLRuntimeSupportFlags : uint32
{
	None	= 0,
	RDG		= 1,		//!< Can run inference on the Render Graph
	CPU		= 2,		//!< Can run inference on the CPU
	GPU		= 4			//!< Can run inference on the GPU (without Render Graph)
};

/** NNX runtime module interface */
class IRuntime
{
public:
	virtual ~IRuntime() = default;
	virtual FString GetRuntimeName() const = 0;

	// Each runtime has already created its own internal optimizer called inside CreateModelData
	//virtual TUniquePtr<IModelOptimizer> CreateModelOptimizer() const = 0;

	// TODO: Replace with QuerySupport()
	// Returns flags from ERuntimeSupportFlags
	virtual EMLRuntimeSupportFlags GetSupportFlags() const = 0;

//	// Get Operator registry
//	virtual IOperatorRegistry* GetOperatorRegistry() = 0;

	// ModelOptimizer

	//virtual FMLInferenceModel* CreateInferenceModel(UMLInferenceModel* Model) = 0;

	// Model data factory interface
	virtual bool CanCreateModelData(FString FileType, TConstArrayView<uint8> FileData) const = 0;
	virtual TArray<uint8> CreateModelData(FString FileType, TConstArrayView<uint8> FileData) = 0;

	// Model factory interface
	virtual bool CanCreateModel(TConstArrayView<uint8> ModelData) const = 0;
	virtual TUniquePtr<FMLInferenceModel> CreateModel(TConstArrayView<uint8> ModelData) = 0;
};

/** Tensor memory binding type */
enum class EMLTensorBindingDataType : uint8
{
	RDGBuffer,
	CPUMemory,
	GPUMemory
};

/** Tensor binding */
struct NNXCORE_API FMLTensorBinding
{
	union
	{
		// Q: Do we need RHI buffer here?
		// A: Caller can import RHI buffer into RDG using FGraphBuilder, prior invoking the interface
		FRDGBuffer*				Buffer;								//!< RDG buffer
		void*					CpuMemory;							//!< Pointer to the CPU memory
		uint64					GpuMemory;							//!< Pointer to the GPU memory
	};

	uint64						SizeInBytes;						//!< Size in bytes
	uint64						OffsetInBytes;						//!< Offset in bytes from the start of data
	EMLTensorBindingDataType	BindingType;

	static FMLTensorBinding FromCPU(void* CpuMemory, uint64 InSize, uint64 InOffset = 0);
	static FMLTensorBinding FromGPU(uint64 GpuMemory, uint64 InSize, uint64 InOffset = 0);
	static FMLTensorBinding FromRDG(FRDGBuffer* BufferRef, uint64 InSize, uint64 InOffset = 0);
};

enum class EMLInferenceModelType : uint8
{
	CPU,
	RDG,
	GPU
};

/** Runtime Inference model is used to execute / run model inference */
class NNXCORE_API FMLInferenceModel
{
public:

	virtual ~FMLInferenceModel() = default;

	EMLInferenceModelType GetType() const
	{
		return Type;
	}

	/** Getters for tensor description as defined by the model potentially with variable dimensions() */
	TConstArrayView<FTensorDesc> GetInputTensorDescs() const;
	TConstArrayView<FTensorDesc> GetOutputTensorDescs() const;

	/** Getters for input shapes if they were set already (see SetInputTensorShapes). Empty list otherwise. */
	TConstArrayView<FTensorShape> GetInputTensorShapes() const;

	/** Getters for outputs shapes if they were already resolved. Empty list otherwise.
	* Output shape might be resolved after a call to SetInputTensorShapes() if the model and engine support it
	* otherwise they will be resolved during Run() or EnqueueRDG() */
	TConstArrayView<FTensorShape> GetOutputTensorShapes() const;
	
	/** Prepare the model to be run with the given input shape, The call is mandatory before Run or Enqueue() can be called. */
	virtual int SetInputTensorShapes(TConstArrayView<FTensorShape> InInputShapes);

	/** This call is synchronous on all engine types(CPU, RDG, GPU), i.e.the calling thread will be blocked until execution is finished.
	* Bindings should point to a buffers big enough */
	virtual int RunSync(TConstArrayView<FMLTensorBinding> InInputTensors,
					TConstArrayView<FMLTensorBinding> InOutputTensors) = 0;

	/** Enqueue the execution on the Render graph render thread. It's caller's responsibility to actually run the graph.
	* Bindings should point to a buffers big enough */
	virtual int EnqueueRDG(FRDGBuilder& RDGBuilder, 
		TConstArrayView<FMLTensorBinding> InInputTensors, 
		TConstArrayView<FMLTensorBinding> InOutputTensors)
	{
		return -1;
	}

protected:
	
	FMLInferenceModel(EMLInferenceModelType InType)
		: Type(InType)
	{}

	TArray<FTensorShape>	InputTensorShapes;
	TArray<FTensorShape>	OutputTensorShapes;
	TArray<FTensorDesc>		InputSymbolicTensors;
	TArray<FTensorDesc>		OutputSymbolicTensors;
	EMLInferenceModelType	Type;
};


//-----------------------------------------------------------------------------
// IMPLEMENTATION
//-----------------------------------------------------------------------------

inline TConstArrayView<FTensorDesc> FMLInferenceModel::GetInputTensorDescs() const
{
	return InputSymbolicTensors;
}

inline TConstArrayView<FTensorDesc> FMLInferenceModel::GetOutputTensorDescs() const
{
	return OutputSymbolicTensors;
}

inline TConstArrayView<FTensorShape> FMLInferenceModel::GetInputTensorShapes() const
{
	return InputTensorShapes;
}

inline TConstArrayView<FTensorShape> FMLInferenceModel::GetOutputTensorShapes() const
{
	return OutputTensorShapes;
}

//struct FMLOperatorDomain
//{
//	constexpr FString Default = "";
//	constexpr FString Onnx = "Onnx";
//};
//
//struct FMLOperatorDesc
//{
//	FString		Name;
//	FString		Domain;
//	uint32		Version;
//
//	// Custom operator data
//	//union FCustomData
//	//{
//	//	FGlobalShader*	Shader;
//	//	uint64			CustomOpId;
//	//};
//
//	// TODO: Do we need parameter specification here?
//};
//
///// NNX ML Operator registry
//class IOperatorRegistry
//{
//public:
//
//	virtual int HasOperator(const FString Name, uint32 Version, const FString Domain = FOperatorDomain::Default) = 0;
//
//	// Return number of supported operators (by version)
//	virtual int HasOperator(const FString Name) = 0;
//
//	virtual FMLOperatorDesc GetOperator(const FString Name, int index) = 0;
//
//	virtual TArray<FMLOperatorDesc> GetOperators(const FString Name) = 0;
//
//	//virtual int AddCustomOp() = 0;
//	//virtual void RemoveCustomOp() = 0;
//	//virtual void GetCustomOp() = 0;
//};

} // NNX
