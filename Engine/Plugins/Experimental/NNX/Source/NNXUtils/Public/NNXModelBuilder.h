// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NNXTypes.h"
#include "Containers/Array.h"

namespace NNX
{

class IMLModelBuilder
{
public:

	enum class HandleType : uint8
	{
		Invalid,
		Tensor,
		Operator
	};

	template<typename Tag>
	struct Handle
	{
		void*		Ptr { nullptr };
		HandleType	Type { HandleType::Invalid };
	};

	typedef struct Handle<struct TensorTag>		HTensor;
	typedef struct Handle<struct OperatorTag>	HOperator;

	template<typename TensorT>
	HTensor MakeTensorHandle(TensorT* TensorPtr)
	{
		return HTensor{ TensorPtr, HandleType::Tensor };
	}

	template<typename OperatorT>
	HOperator MakeOperatorHandle(OperatorT* OperatorPtr)
	{
		return HOperator{ OperatorPtr, HandleType::Operator };
	}

	virtual ~IMLModelBuilder() = default;

	/** Initialize the model builder */
	virtual bool Begin(const FString& Name = TEXT("main")) = 0;

	/** Serialize the model to given array */
	virtual bool End(TArray<uint8>& Data) = 0;

	/** Add tensor */
	virtual HTensor AddTensor(const FString& Name, EMLTensorDataType DataType, TArrayView<const int32> Shape) = 0;

	/** Add model input */
	virtual bool AddInput(HTensor InTensor) = 0;

	/** Add model output */
	virtual bool AddOutput(HTensor OutTensor) = 0;

	/** Add operator */
	virtual HOperator AddOperator(const FString& Type, const FString& Name = TEXT("")) = 0;

	/** Add operator input */
	virtual bool AddOperatorInput(HOperator Op, HTensor Tensor) = 0;

	/** Add operator attribute */
	virtual bool AddOperatorAttribute(HOperator Op, const FString& Name, const FMLAttributeValue& Value) = 0;
	
	/** Add operator output */
	virtual bool AddOperatorOutput(HOperator Op, HTensor Tensor) = 0;
};

/** Default ONNX IR version */
static constexpr int64 OnnxIrVersion = 7;

/** Default ONNX operator set version */
static constexpr int64 OnnxOpsetVersion = 15;

/**
 * Create an instance of ONNX model builder that creates ONNX models in memory
 */
NNXUTILS_API IMLModelBuilder* CreateONNXModelBuilder(int64 IrVersion = OnnxIrVersion, int64 OpsetVersion = OnnxOpsetVersion);

/**
 * Utility functions to create single layer NN for operator testing with optional attributes
 */
NNXUTILS_API bool CreateONNXModelForOperator(const FString& OperatorName, TConstArrayView<const FMLTensorDesc> InInputTensors, TConstArrayView<const FMLTensorDesc> InOutputTensors, TArray<uint8>& ModelData);
NNXUTILS_API bool CreateONNXModelForOperator(const FString& OperatorName, TConstArrayView<const FMLTensorDesc> InInputTensors, TConstArrayView<const FMLTensorDesc> InOutputTensors, const FMLAttributeMap& Attributes, TArray<uint8>& ModelData);

/**
 * Create an instance of NNX model builder that creates NNX model/format in memory
 */
NNXUTILS_API IMLModelBuilder* CreateNNXModelBuilder();

} // NNX

