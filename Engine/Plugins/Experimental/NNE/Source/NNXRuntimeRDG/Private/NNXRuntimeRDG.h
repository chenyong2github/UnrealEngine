// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NNECoreAttributeMap.h"
#include "NNECoreAttributeValue.h"
#include "NNECoreTensor.h"
#include "NNECoreTypes.h"
#include "NNXCore.h"
#include "NNXModelOptimizerInterface.h"
#include "NNXRuntime.h"
#include "NNXRuntimeFormat.h"
#include "RenderGraphResources.h"
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

namespace UE::NNECore { class FTensorDesc; }

namespace NNX
{

struct FTensorBinding;

/**
 * Interface for all operators to prepare the model tensors at scheduling time
 */
struct IPrepareOperator
{
	virtual ~IPrepareOperator() = default;
	virtual int PrepareOutputs(TConstArrayView<UE::NNECore::Internal::FTensorRef> InputTensors, TArrayView<UE::NNECore::Internal::FTensorRef> OutputTensors) const = 0;
};

/**
* Interface for all ML operators running on the RDG 
*/
struct IOperatorRDG
{
	virtual ~IOperatorRDG() = default;
};

class FTensorRDG : public UE::NNECore::Internal::FTensor
{
	FRDGBufferRef Buffer{};
	
public:
	static FTensorRDG Make(const FTensorDesc& TensorDesc, const FTensorShape& Shape, FRDGBufferRef Buffer)
	{
		check(Shape.IsCompatibleWith(TensorDesc.GetShape()));
		FTensorRDG TensorRDG;
		TensorRDG.Buffer = nullptr;
		TensorRDG.Name = TensorDesc.GetName();
		TensorRDG.DataType = TensorDesc.GetDataType();
		TensorRDG.Shape = Shape;
		TensorRDG.Volume = Shape.Volume();
		TensorRDG.DataSize = (uint64)UE::NNECore::GetTensorDataTypeSizeInBytes(TensorRDG.DataType) * TensorRDG.Volume;
		TensorRDG.Buffer = Buffer;
		check(TensorRDG.Volume <= TNumericLimits<uint32>::Max());
		return TensorRDG;
	}

	bool HasBuffer() const { return Buffer != FRDGBufferRef{}; }
	void SetBuffer(FRDGBufferRef Inbuffer){ Buffer = Inbuffer; }
	FRDGBufferRef GetBuffer() const { return Buffer; }
};

using FTensorRDGRef = FTensorRDG*;
using FTensorRDGArray = TArray<FTensorRDG, TInlineAllocator<16>>;
using FTensorRDGRefArray = TArray<FTensorRDGRef, TInlineAllocator<64>>;
using FIntArray = TArray<int32, TInlineAllocator<16>>;

class FMLRuntimeRDG : public IRuntime
{
public:
	static FGuid GUID;
	static int32 Version;

	virtual bool CanCreateModelData(FString FileType, TConstArrayView<uint8> FileData) const;
	virtual TArray<uint8> CreateModelData(FString FileType, TConstArrayView<uint8> FileData) = 0;
	virtual bool CanCreateModel(TConstArrayView<uint8> ModelData) const;
};

/** 
 * RDG inference model base class
 */
class FMLInferenceModelRDG : public FMLInferenceModel
{

	struct FReadbackEntry;

public:

	~FMLInferenceModelRDG();

	virtual int SetInputTensorShapes(TConstArrayView<FTensorShape> InputShapes) override;
	virtual int RunSync(TConstArrayView<FMLTensorBinding> InputBindings, TConstArrayView<FMLTensorBinding> OutputBindings) override;
	virtual int EnqueueRDG(FRDGBuilder& RDGBuilder, TConstArrayView<FMLTensorBinding> InputBindings, TConstArrayView<FMLTensorBinding> OutputBindings) override;

protected:

	FMLInferenceModelRDG();

	bool LoadModel(TConstArrayView<uint8> ModelData, FMLRuntimeFormat& Format);

	int SetTensors(FRDGBuilder& GraphBuilder, FTensorRDGArray& InTensorRDGs, FIntArray& OutIndices, TConstArrayView<FMLTensorBinding> InBindings);
	
	virtual int PrepareTensorShapesAndData() = 0;
	virtual void AddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder) = 0;

	virtual void AddTensorUploads_RenderThread(FRDGBuilder& GraphBuilder, TConstArrayView<int32> InUploadIndices, TConstArrayView<FTensorRDG> InRDGBindings, TConstArrayView<FMLTensorBinding> InBindings);
	virtual void AddTensorReadbacks_RenderThread(FRDGBuilder& GraphBuilder, TConstArrayView<int32> InReadbackIndices, TConstArrayView<FTensorRDG> InRDGBindings, TConstArrayView<FMLTensorBinding> InBindings);

	//Tensor descriptor
	TArray<FTensorDesc>			AllSymbolicTensorDescs;

	//Tensor indices for models
	TArray<int32>				IntermediateTensorIndices;
	TArray<int32>				WeightTensorIndices;
	TArray<int32>				InputTensorIndices;
	TArray<int32>				OutputTensorIndices;
	
	//Tensor indices by operator
	TArray<TArray<uint32>>		OperatorInputTensorIndices;
	TArray<TArray<uint32>>		OperatorOutputTensorIndices;

	//RDG Tensors
	FTensorRDGRefArray			AllTensorRDGs;
	FTensorRDGArray				InputTensorRDGs;
	FTensorRDGArray				OutputTensorRDGs;
	FTensorRDGArray				IntermediateTensorRDGs;
	FTensorRDGArray				WeightTensorRDGs;
	
	TArray<FReadbackEntry>		Readbacks;
	bool						bUseManualTransitions;
};

//TODO jira 167584 remove default validation and declare contract in all DML operator (see HLSL Gemm for current example)
bool AlwaysValidValidationFunction(const UE::NNECore::FAttributeMap& AttributeMap, TConstArrayView<ENNETensorDataType> InputTypes, TConstArrayView<UE::NNECore::FSymbolicTensorShape> InputShapes);

class FInputValidator
{
public:
	FInputValidator();
	void AddRequired(int32 TemplateIdx = 0);
	void AddOptional(int32 TemplateIdx = 0);
	void SetTemplateCount(int32 TemplateCount);
	void AddSupportedType(ENNETensorDataType Type, int32 TemplateIdx = 0);
	bool Validate(TConstArrayView<ENNETensorDataType> InputTypes);

private:
	TArray<TArray<ENNETensorDataType>> TemplateTypes;
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

		//Idea: we could extended as needed by operator to support more validation especially around the range of the values.
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
	typedef bool (*OperatorValidateFunc)(const UE::NNECore::FAttributeMap& AttributeMap, TConstArrayView<ENNETensorDataType> InputTypes, TConstArrayView<UE::NNECore::FSymbolicTensorShape> InputShapes);

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
			TArray<ENNETensorDataType> InputTensorTypes;
			TArray<UE::NNECore::FSymbolicTensorShape> InputTensorShapes;
			UE::NNECore::FAttributeMap AttributeMap;
			
			for (int32 InputTensorIndex: Format.Operators[Idx].InTensors)
			{
				InputTensorTypes.Add(Format.Tensors[InputTensorIndex].DataType);
				InputTensorShapes.Add(UE::NNECore::FSymbolicTensorShape::Make(Format.Tensors[InputTensorIndex].Shape));
			}
			for (const FMLFormatAttributeDesc& Desc : Format.Operators[Idx].Attributes)
			{
				AttributeMap.SetAttribute(Desc.Name, Desc.Value);
			}

			const FString& OpType = Format.Operators[Idx].TypeName;
			
			typename TOperatorRegistryRDG<TOperatorType>::OperatorValidateFunc ValidationFn = Registry->OpFindValidation(OpType);

			if (!ValidationFn)
			{
				UE_LOG(LogNNX, Warning, TEXT("Hlsl MLOperatorRegistry failed to find validation for operator:%s"), *OpType);
				return false;
			}
			
			if (!ValidationFn(AttributeMap, InputTensorTypes, InputTensorShapes))
			{
				UE_LOG(LogNNX, Warning, TEXT("Hlsl MLOperatorRegistry failed to validate operator:%s"), *OpType);
				return false;
			}
		}

		return true;
	}
};

TArray<uint8> ConvertToModelData(TArrayView<uint8> ModelBuffer);

// NOTE: For now we only have DML on Windows, we should add support for XSX
#ifdef NNE_USE_DIRECTML

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
 