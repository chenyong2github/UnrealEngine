// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNXCore.h"
#include "NNXRuntime.h"
#include "ShaderParameterUtils.h"
#include "RHIGPUReadback.h"

#include "Containers/Map.h"

BEGIN_SHADER_PARAMETER_STRUCT(FMLTensorUploadParameters, )
	RDG_BUFFER_ACCESS(Buffer, ERHIAccess::CopyDest)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FMLTensorReadbackParameters, )
	RDG_BUFFER_ACCESS(Buffer, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FMLElementWiseOpParameters, )
	RDG_BUFFER_ACCESS(InputBuffer, ERHIAccess::UAVCompute)			//!< NOTE: DirectML requires state to be in UAV, even though we're just reading from the InputBuffer
	RDG_BUFFER_ACCESS(OutputBuffer, ERHIAccess::UAVCompute)
END_SHADER_PARAMETER_STRUCT()

class FRDGBuilder;
struct FMLRuntimeFormat;

namespace NNX
{

struct FTensorBinding;
struct FTensorDesc;

/** Base class for all ML operators running on the RDG */
struct FMLOperatorRDG
{
	virtual ~FMLOperatorRDG() = default;
};

using FMLTensorBindingArray = TArray<FMLTensorBinding, TInlineAllocator<16>>;
using FMLIntArray = TArray<int32, TInlineAllocator<16>>;

/** 
 * RDG inference model base class
 */
class FMLInferenceModelRDG : public FMLInferenceModel
{
	struct FReadbackEntry
	{
		FRHIGPUBufferReadback*	RHI;
		void*					CpuMemory;
		size_t					Offset;
		size_t					Size;
	};

public:

	~FMLInferenceModelRDG();

	virtual int Run(TArrayView<const FMLTensorBinding> InInputBindings, TArrayView<const FMLTensorBinding> OutOutputBindings) override;
	virtual int EnqueueRDG(FRDGBuilder& Builder, TArrayView<const FMLTensorBinding> InInputBindings, TArrayView<const FMLTensorBinding> OutOutputBindings) override;

protected:

	FMLInferenceModelRDG();

	bool LoadModel(UMLInferenceModel* InModel, FMLRuntimeFormat& Format);

	int SetTensors(FRDGBuilder& GraphBuilder, FMLTensorBindingArray& OutRDGBindings, FMLIntArray& OutIndices, TArrayView<const FMLTensorBinding> InBindings, TArrayView<const FMLTensorDesc> InTensors);

	virtual void AddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder, TArrayView<const FMLTensorBinding> InInputBindings, TArrayView<const FMLTensorBinding> OutOutputBindings) = 0;

	virtual void AddTensorUploads_RenderThread(FRDGBuilder& GraphBuilder, TArrayView<const int32> InUploadIndices, TArrayView<FMLTensorBinding> InRDGBindings, TArrayView<const FMLTensorBinding> InBindings);
	virtual void AddTensorReadbacks_RenderThread(FRDGBuilder& GraphBuilder, TArrayView<const int32> InReadbackIndices, TArrayView<const FMLTensorBinding> InRDGBindings, TArrayView<const FMLTensorBinding> InBindings);

	FReadbackEntry	Readback;
	bool			bUseManualTransitions;
};


/**
 * Registry for RDG ML operators
 */
template<class TMLOperatorType>
class TMLOperatorRegistryRDG
{
public:

	typedef TMLOperatorType* (*MLOperatorCreateFunc)();

	static TMLOperatorRegistryRDG* Get()
	{
		static TMLOperatorRegistryRDG Inst;

		return &Inst;
	}

	MLOperatorCreateFunc OpFind(const FString& Name)
	{
		MLOperatorCreateFunc* Fn = Operators.Find(Name);

		if (!Fn)
		{
			UE_LOG(LogNNX, Warning, TEXT("RDG MLOperator:%s is not registered"), *Name);
			return nullptr;
		}

		return *Fn;
	}

	bool OpAdd(const FString& Name, MLOperatorCreateFunc Func)
	{
		if (Operators.Find(Name) != nullptr)
		{
			UE_LOG(LogNNX, Warning, TEXT("RDG MLOperator is already registered:%s"), *Name);
			return false;
		}

		Operators.Add(Name, Func);
		return true;
	}

private:

	TMap<FString, MLOperatorCreateFunc> Operators;
};


// NOTE: For now we only have DML on Windows, we should add support for XSX
#if PLATFORM_WINDOWS

extern IRuntime* FMLRuntimeDmlStartup();
extern void FMLRuntimeDmlShutdown();

#endif

extern IRuntime* FMLRuntimeHlslStartup();
extern void FMLRuntimeHlslShutdown();


///// OperatorId
//class FOperatorId
//{
//public:
//
//	FOperatorId(uint32 InType, uint32 InIndex)
//		: Type(InType)
//		, Index(InIndex)
//	{
//	}
//
//	uint32 GetIndex() const
//	{
//		return Index;
//	}
//
//	uint32 GetType() const
//	{
//		return Type;
//	}
//
//private:
//
//	uint32	Type : 8;
//	uint32	Index : 16;
//};

///// Base class
//class FRDGOperator
//{};

///// Base class for RDG runtime
//class FRDGRuntime : public IRuntime
//{
//public:
//
//	virtual FRDGOperator OpCreate(const FString& Name, const TArrayView<uint32>& TensorSizes) = 0;
//
//};

} // NNX
 