// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNECoreAttributeMap.h"
#include "NNECoreAttributeValue.h"
#include "NNXCore.h"
#include "NNXModelOptimizerInterface.h"
#include "NNXRuntime.h"
#include "NNXRuntimeFormat.h"
#include "RHIGPUReadback.h"
#include "Serialization/MemoryReader.h"
#include "ShaderParameterUtils.h"

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
struct FNNIModelRaw;

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

	virtual int SetInputShapes(TConstArrayView<const FTensorShape> InputShapes) override;
	virtual int Run(TConstArrayView<const FMLTensorBinding> InputBindings, TConstArrayView<const FTensorShape> InputShapes, TConstArrayView<const FMLTensorBinding> OutputBindings) override;
	virtual int EnqueueRDG(FRDGBuilder& RDGBuilder, TConstArrayView<const FMLTensorBinding> InputBindings, TConstArrayView<const FTensorShape> InputShapes, TConstArrayView<const FMLTensorBinding> OutputBindings) override;

protected:

	FMLInferenceModelRDG();

	bool LoadModel(const FNNIModelRaw& InModel, FMLRuntimeFormat& Format);

	int CreateIntermediateTensors(FRDGBuilder& GraphBuilder, FMLTensorBindingArray& OutBindings, TConstArrayView<const FMLTensorDesc> InTensors);
	int SetTensors(FRDGBuilder& GraphBuilder, FMLTensorBindingArray& OutRDGBindings, FMLIntArray& OutIndices, TArrayView<const FMLTensorBinding> InBindings, TArrayView<const FMLTensorDesc> InTensors);

	virtual void AddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder) = 0;

	virtual void AddTensorUploads_RenderThread(FRDGBuilder& GraphBuilder, TArrayView<const int32> InUploadIndices, TArrayView<FMLTensorBinding> InRDGBindings, TArrayView<const FMLTensorBinding> InBindings);
	virtual void AddTensorReadbacks_RenderThread(FRDGBuilder& GraphBuilder, TArrayView<const int32> InReadbackIndices, TArrayView<const FMLTensorBinding> InRDGBindings, TArrayView<const FMLTensorBinding> InBindings);

	//Tensors
	TArray<FMLTensorDesc>		AllTensors;
	TArray<FSymbolicTensorDesc>	AllSymbolicTensors;
	TArray<FMLTensorDesc>		InputTensors;
	TArray<FMLTensorDesc>		OutputTensors;

	//Tensor indices for models
	TArray<int32>				IntermediateTensorIndices;
	TArray<int32>				InputTensorIndices;
	TArray<int32>				OutputTensorIndices;
	
	//Tensor indices by operator
	TArray<TArray<uint32>>		OperatorInputTensorIndices;
	TArray<TArray<uint32>>		OperatorOutputTensorIndices;
	
	FMLTensorBindingArray       AllTensorBindings;
	FReadbackEntry				Readback;
	bool						bUseManualTransitions;
};

//TODO jira 167585 remove default validation and declare contract in all HLSL operator (see HLSL Gemm for current example)
//TODO jira 167584 remove default validation and declare contract in all DML operator (see HLSL Gemm for current example)
bool AlwaysValidValidationFunction(const UE::NNECore::FAttributeMap& AttributeMap, TConstArrayView<EMLTensorDataType> InputTypes, TConstArrayView<const FSymbolicTensorShape> InputShapes);

class FInputValidator
{
public:
	FInputValidator();
	void AddRequired(int32 TemplateIdx = 0);
	void AddOptional(int32 TemplateIdx = 0);
	void SetTemplateCount(int32 TemplateCount);
	void AddSupportedType(EMLTensorDataType Type, int32 TemplateIdx = 0);
	bool Validate(TConstArrayView<EMLTensorDataType> InputTypes);

private:
	TArray<TArray<EMLTensorDataType>> TemplateTypes;
	TArray<int32> InputTemplateIndices;
	int32 NumRequiredInput;
	int32 NumOptionalInput;
};

class FAttributeValidator
{
public:
	void AddOptional(const FString& Name, ENNEAttributeDataType Type);
	void AddRequired(const FString& Name, ENNEAttributeDataType Type);
	bool Validate(const UE::NNECore::FAttributeMap& AttributesToValidate);

private:
	struct FEntry
	{
		FEntry(const FString& InName, ENNEAttributeDataType InType)
			: Name(InName), Type(InType)
		{
		}

		//TODO should be extended as needed by operator to support more validation especially around the range of the values.
		//An example is ConvTranspose `auto_pad` enum style string that can only take a few values
		//In the same direction we might only support a range of value for a float (for example
		//we only support integer but the type is float, or only positive values for an int32)
		FString Name;
		ENNEAttributeDataType Type;
	};

	TArray<FEntry> RequiredAttributes;
	TArray<FEntry> OptionalAttributes;
};

/**
 * Registry for RDG ML operators
 */
template<class TOperatorType>
class TOperatorRegistryRDG
{
public:

	typedef TOperatorType* (*OperatorCreateFunc)();
	typedef bool (*OperatorValidateFunc)(const UE::NNECore::FAttributeMap& AttributeMap, TConstArrayView<EMLTensorDataType> InputTypes, TConstArrayView<const FSymbolicTensorShape> InputShapes);

	static TOperatorRegistryRDG* Get()
	{
		static TOperatorRegistryRDG Inst;

		return &Inst;
	}

	OperatorValidateFunc OpFindValidation(const FString& Name)
	{
		OperatorValidateFunc* Fn = OperatorValidations.Find(Name);

		if (!Fn)
		{
			UE_LOG(LogNNX, Warning, TEXT("RDG MLOperator:%s is not registered"), *Name);
			return nullptr;
		}

		return *Fn;
	}

	OperatorCreateFunc OpFind(const FString& Name)
	{
		OperatorCreateFunc* Fn = Operators.Find(Name);

		if (!Fn)
		{
			UE_LOG(LogNNX, Warning, TEXT("RDG MLOperator:%s is not registered"), *Name);
			return nullptr;
		}

		return *Fn;
	}

	bool OpAdd(const FString& Name, OperatorCreateFunc Func, OperatorValidateFunc ValidateFunc = AlwaysValidValidationFunction)
	{
		if (Operators.Find(Name) != nullptr)
		{
			UE_LOG(LogNNX, Warning, TEXT("RDG MLOperator is already registered:%s"), *Name);
			return false;
		}

		Operators.Add(Name, Func);
		OperatorValidations.Add(Name, ValidateFunc);
		return true;
	}

private:

	TMap<FString, OperatorCreateFunc> Operators;
	TMap<FString, OperatorValidateFunc> OperatorValidations;
};

/**
 * Validator for RDG ML operators
 */
template<class TOperatorType>
class TModelValidatorRDG : public IModelValidator
{
public:
	virtual FString GetName() const 
	{
		return TEXT("RDG Model validator");
	}
	
	virtual bool ValidateModel(const FNNIModelRaw& InputModel, const FOptimizerOptionsMap& Options) const override
	{
		FMLRuntimeFormat	Format;

		ENNXInferenceFormat FormatType = InputModel.Format;
		if (FormatType != ENNXInferenceFormat::NNXRT)
		{
			UE_LOG(LogNNX, Warning, TEXT("Unsupported format type for validator %s"), *GetName());
			return false;
		}

		FMemoryReader Reader(InputModel.Data);
		FMLRuntimeFormat::StaticStruct()->SerializeBin(Reader, &Format);

		TOperatorRegistryRDG<TOperatorType>* Registry = TOperatorRegistryRDG<TOperatorType>::Get();
		check(Registry != nullptr);

		for (int32 Idx = 0; Idx < Format.Operators.Num(); ++Idx)
		{
			TArray<EMLTensorDataType> InputTensorTypes;
			TArray<FSymbolicTensorShape> InputTensorShapes;
			UE::NNECore::FAttributeMap AttributeMap;
			
			for (int32 InputTensorIndex: Format.Operators[Idx].InTensors)
			{
				InputTensorTypes.Add(Format.Tensors[InputTensorIndex].DataType);
				InputTensorShapes.Add(FSymbolicTensorShape::Make(Format.Tensors[InputTensorIndex].Shape));
			}
			for (const FMLFormatAttributeDesc& Desc : Format.Operators[Idx].Attributes)
			{
				AttributeMap.SetAttribute(Desc.Name, Desc.Value);
			}

			const FString& OpType = Format.Operators[Idx].TypeName;
			
			//TODO jira 167587: we should extract constant tensor from the model
			//and pass them to the operator validation so that it can validate
			//the shapes of the constant tensors and ensure no gpu-cpu sync
			//will be needed during the execution of the operator.
			typename TOperatorRegistryRDG<TOperatorType>::OperatorValidateFunc ValidationFn = Registry->OpFindValidation(OpType);

			if (!ValidationFn(AttributeMap, InputTensorTypes, InputTensorShapes))
			{
				UE_LOG(LogNNX, Warning, TEXT("Hlsl MLOperatorRegistry failed to validate operator:%s"), *OpType);
				return false;
			}
		}

		return true;
	}
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
 