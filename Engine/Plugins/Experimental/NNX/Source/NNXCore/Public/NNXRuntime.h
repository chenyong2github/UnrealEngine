// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNXTypes.h"

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FRDGBuffer;

// TODO: 
/*
- OperatorRegistry
	- Q: Should registry be an internal part of runtime ModelOptimizer, i.e. never exposed to the NNX utils?

- ModelOptimizer interface for runtime dependent model optimizations
*/

class UMLInferenceModel;
class FRDGBuilder;

namespace NNX
{
class FMLInferenceModel;

//class IOperatorRegistry;

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

	// TODO: Replace with QuerySupport()
	// Returns flags from ERuntimeSupportFlags
	virtual EMLRuntimeSupportFlags GetSupportFlags() const = 0;

//	// Get Operator registry
//	virtual IOperatorRegistry* GetOperatorRegistry() = 0;

	// ModelOptimizer

	virtual FMLInferenceModel* CreateInferenceModel(UMLInferenceModel* Model) = 0;
};

/** Tensor memory binding type */
enum class EMLTensorBindingDataType : uint8
{
	RDGBuffer,
	CPUMemory,
	GPUMemory
};

/** Tensor binding */
struct FMLTensorBinding
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

	/**
	 * Initialize binding from CPU memory
	 */
	static FMLTensorBinding FromCPU(void* CpuMemory, uint64 InSize, uint64 InOffset = 0)
	{
		FMLTensorBinding	Binding;

		Binding.BindingType = EMLTensorBindingDataType::CPUMemory;
		Binding.CpuMemory = CpuMemory;
		Binding.SizeInBytes = InSize;
		Binding.OffsetInBytes = InOffset;

		return Binding;
	}

	/**
	 * Initialize binding from GPU memory
	 */
	static FMLTensorBinding FromGPU(uint64 GpuMemory, uint64 InSize, uint64 InOffset = 0)
	{
		FMLTensorBinding	Binding;

		Binding.BindingType = EMLTensorBindingDataType::GPUMemory;
		Binding.GpuMemory = GpuMemory;
		Binding.SizeInBytes = InSize;
		Binding.OffsetInBytes = InOffset;

		return Binding;
	}

	/**
	 * Initialize binding from RDG allocated buffer memory
	 */
	static FMLTensorBinding FromRDG(FRDGBuffer* BufferRef, uint64 InSize, uint64 InOffset = 0)
	{
		FMLTensorBinding	Binding;

		Binding.BindingType = EMLTensorBindingDataType::RDGBuffer;
		Binding.Buffer = BufferRef;
		Binding.SizeInBytes = InSize;
		Binding.OffsetInBytes = InOffset;

		return Binding;
	}
};

enum class EMLInferenceModelType : uint8
{
	CPU,
	RDG,
	GPU
};

/** Runtime Inference model is used to execute / run model inference */
class FMLInferenceModel
{
public:

	virtual ~FMLInferenceModel() = default;

	EMLInferenceModelType GetType() const
	{
		return Type;
	}

	int32 InputTensorNum() const;
	const FMLTensorDesc& GetInputTensor(int32 Index) const;
	TArrayView<const FMLTensorDesc> GetInputTensors() const;

	int32 OutputTensorNum() const;
	const FMLTensorDesc& GetOutputTensor(int32 Index) const;
	TArrayView<const FMLTensorDesc> GetOutputTensors() const;

	/** This call is synchronous on all Inference model types(CPU, RDG, GPU), i.e.the calling thread will be blocked
	 * until inference is not finished
	 */
	virtual int Run(TArrayView<const FMLTensorBinding> InInputTensors, TArrayView<const FMLTensorBinding> OutOutputTensors) = 0;

	/** Enqueue the inference operators on the Render graph render thread. It's caller's responsibility to actually run the graph.
	 */
	virtual int EnqueueRDG(FRDGBuilder& GraphBuilder, TArrayView<const FMLTensorBinding> InInputTensors, TArrayView<const FMLTensorBinding> OutOutputTensors)
	{
		return -1;
	}

protected:
	
	FMLInferenceModel(EMLInferenceModelType InType)
		: Type(InType)
	{}

	TArray<FMLTensorDesc >	InputTensors;
	TArray<FMLTensorDesc >	OutputTensors;
	EMLInferenceModelType	Type;
};


//-----------------------------------------------------------------------------
// IMPLEMENTATION
//-----------------------------------------------------------------------------

inline int32 FMLInferenceModel::InputTensorNum() const
{
	return InputTensors.Num();
}

inline const FMLTensorDesc& FMLInferenceModel::GetInputTensor(int32 Index) const
{
	checkf(Index < InputTensors.Num(), TEXT("Invalid tensor index"));
	return InputTensors[Index];
}

inline TArrayView<const FMLTensorDesc> FMLInferenceModel::GetInputTensors() const
{
	return InputTensors;
}

inline int32 FMLInferenceModel::OutputTensorNum() const
{
	return OutputTensors.Num();
}

inline const FMLTensorDesc& FMLInferenceModel::GetOutputTensor(int32 Index) const
{
	checkf(Index < OutputTensors.Num(), TEXT("Invalid tensor index"));
	return OutputTensors[Index];
}

inline TArrayView<const FMLTensorDesc> FMLInferenceModel::GetOutputTensors() const
{
	return OutputTensors;
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
