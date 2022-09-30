// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXCore.h"
#include "NNXModelBuilder.h"

#include "NNXThirdPartyWarningDisabler.h"
NNX_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT

#include "onnx/common/ir.h"
#include "onnx/common/constants.h"
#include "onnx/defs/operator_sets.h"

#include "core/session/ort_model_optimizer_api.h"	// For model validation
//#include "core/session/ort_env.h"
//#include "core/session/environment.h"
#include "core/session/onnxruntime_cxx_api.h"

NNX_THIRD_PARTY_INCLUDES_END

namespace NNX
{

inline onnx::ValueInfoProto* OnnxTensorCast(IMLModelBuilder::HTensor& Handle)
{
	if (Handle.Type == IMLModelBuilder::HandleType::Tensor)
	{
		return reinterpret_cast<onnx::ValueInfoProto*>(Handle.Ptr);
	}

	return nullptr;
}

inline onnx::NodeProto* OnnxOperatorCast(IMLModelBuilder::HOperator& Handle)
{
	if (Handle.Type == IMLModelBuilder::HandleType::Operator)
	{
		return reinterpret_cast<onnx::NodeProto*>(Handle.Ptr);
	}

	return nullptr;
}

/**
 * Builds an ONNX model in memory.
 * NOTE:
 * - We plan to use this only for generating simple networks for testing operators and simple models
 */
class FMLModelBuilderONNX : public IMLModelBuilder
{
	static constexpr const char* kOnnxDomain = onnx::ONNX_DOMAIN;

	onnx::ModelProto	Model;
	onnx::GraphProto*	Graph{ nullptr };
	int64				IrVersion { OnnxIrVersion };
	int64				OpsetVersion{ OnnxOpsetVersion };
	
public:

	FMLModelBuilderONNX(int64 InIrVersion = OnnxIrVersion, int64 InOpsetVersion = OnnxOpsetVersion)
		: IrVersion(InIrVersion)
		, OpsetVersion(InOpsetVersion)
	{
	}
	
	/** Initialize the model builder */
	virtual bool Begin(const FString& Name) override
	{
		// Setup model
		Model.set_ir_version(IrVersion);
		Model.set_domain(kOnnxDomain);

		onnx::OperatorSetIdProto* OpsetProto = Model.add_opset_import();

		OpsetProto->set_domain(kOnnxDomain);
		OpsetProto->set_version(OpsetVersion);

		Graph = Model.mutable_graph();
		Graph->set_name(TCHAR_TO_ANSI(*Name));

		return true;
	}

	/** Serialize the model to given array */
	virtual bool End(TArray<uint8>& Data) override
	{
		const int Size = (int)Model.ByteSizeLong();

		//Data.r
		Data.SetNum(Size);

		bool res = Model.SerializeToArray(Data.GetData(), Data.Num());

		// Validate model
		if (res)
		{
			OrtStatusPtr status = OrtValidateModelFromMemory(Data.GetData(), Data.Num());

			if (status)
			{
				UE_LOG(LogNNX, Warning, TEXT("ModelBuilder error:%s"), ANSI_TO_TCHAR(Ort::GetApi().GetErrorMessage(status)));
				return false;
			}
		}

		return res;
	}

	/** Add tensor */
	virtual HTensor AddTensor(const FString& Name, EMLTensorDataType DataType, TArrayView<const int32> Shape) override
	{
		auto Value = onnx::ValueInfoProto().New(Graph->GetArena());
		
		if (!SetValue(Value, Name, DataType, Shape))
		{
			return HTensor();
		}

		return MakeTensorHandle(Value);
	}

	/** Add model input */
	virtual bool AddInput(HTensor Handle) override
	{
		auto Value = OnnxTensorCast(Handle);

		if (!Value)
		{
			return false;
		}

		*Graph->mutable_input()->Add() = *Value;

		return true;
	}

	/** Add model output */
	virtual bool AddOutput(HTensor Handle) override
	{
		auto Value = OnnxTensorCast(Handle);

		if (!Value)
		{
			return false;
		}

		*Graph->mutable_output()->Add() = *Value;

		return true;
	}

	/** Add operator */
	virtual HOperator AddOperator(const FString& TypeName, const FString& Name) override
	{
		FTCHARToUTF8 Convert(*TypeName);

		std::string NodeType = onnx::Symbol(Convert.Get()).toString();

		onnx::NodeProto* Node = Graph->add_node();

		Node->set_op_type(NodeType);

		if (!Name.IsEmpty())
		{
			Node->set_name(TCHAR_TO_ANSI(*Name));
		}
		else
		{
			Node->set_name(TCHAR_TO_ANSI(*TypeName));
		}

		Node->set_domain(Model.domain());

		return MakeOperatorHandle(Node);
	}

	/** Add operator input */
	virtual bool AddOperatorInput(HOperator Op, HTensor Tensor) override
	{
		auto Value = OnnxTensorCast(Tensor);
		auto NodeOp = OnnxOperatorCast(Op);
		auto InName = NodeOp->mutable_input()->Add();
		
		*InName = Value->name();
		
		return true;
	}

	/** Add operator output */
	virtual bool AddOperatorOutput(HOperator Op, HTensor Tensor) override
	{
		auto Value = OnnxTensorCast(Tensor);
		auto NodeOp = OnnxOperatorCast(Op);
		auto OutName = NodeOp->mutable_output()->Add();

		*OutName = Value->name();

		return true;
	}

private:

	bool SetValue(onnx::ValueInfoProto* Value, const FString& Name, EMLTensorDataType DataType, const TArrayView<const int32>& InShape)
	{
		onnx::TypeProto*		Type = Value->mutable_type();
		onnx::TypeProto_Tensor* TensorType = Type->mutable_tensor_type();
		onnx::TensorShapeProto* Shape = TensorType->mutable_shape();

		Value->set_name(TCHAR_TO_ANSI(*Name));
		TensorType->set_elem_type(ToTensorProtoDataType(DataType));

		for (int32 Idx = 0; Idx < InShape.Num(); ++Idx)
		{
			auto Dim = Shape->add_dim();

			Dim->set_dim_value(InShape[Idx]);
		}

		return true;
	}

	onnx::TensorProto_DataType ToTensorProtoDataType(EMLTensorDataType DataType)
	{
		switch (DataType)
		{
			case EMLTensorDataType::None: return onnx::TensorProto_DataType_UNDEFINED;
			case EMLTensorDataType::Float: return onnx::TensorProto_DataType_FLOAT;
			case EMLTensorDataType::UInt8: return onnx::TensorProto_DataType_UINT8;
			case EMLTensorDataType::Int8: return onnx::TensorProto_DataType_INT8;
			case EMLTensorDataType::UInt16: return onnx::TensorProto_DataType_UINT16;
			case EMLTensorDataType::Int16: return onnx::TensorProto_DataType_INT16;
			case EMLTensorDataType::Int32: return onnx::TensorProto_DataType_INT32;
			case EMLTensorDataType::Int64: return onnx::TensorProto_DataType_INT64;
			//case EMLTensorDataType::String: return onnx::TensorProto_DataType_STRING;
			case EMLTensorDataType::Boolean: return onnx::TensorProto_DataType_BOOL;
			case EMLTensorDataType::Half: return onnx::TensorProto_DataType_FLOAT16;
			case EMLTensorDataType::Double: return onnx::TensorProto_DataType_DOUBLE;
			case EMLTensorDataType::UInt32: return onnx::TensorProto_DataType_UINT32;
			case EMLTensorDataType::UInt64: return onnx::TensorProto_DataType_UINT64;
			case EMLTensorDataType::Complex64: return onnx::TensorProto_DataType_COMPLEX64;
			case EMLTensorDataType::Complex128: return onnx::TensorProto_DataType_COMPLEX128;
			case EMLTensorDataType::BFloat16: return onnx::TensorProto_DataType_BFLOAT16;
			default: return onnx::TensorProto_DataType_UNDEFINED;
		}
	}
};

//
//
//
NNXUTILS_API bool CreateONNXModelForOperator(const FString& OperatorName, TArrayView<FMLTensorDesc> InInputTensors, TArrayView<FMLTensorDesc> InOutputTensors, TArray<uint8>& ModelData)
{
	TUniquePtr<IMLModelBuilder> Builder(CreateONNXModelBuilder());

	Builder->Begin();

	TArray<IMLModelBuilder::HTensor>	InputTensors;
	
	for (int32 Idx = 0; Idx < InInputTensors.Num(); ++Idx)
	{
		const FMLTensorDesc& Desc = InInputTensors[Idx];
		IMLModelBuilder::HTensor Tensor = Builder->AddTensor(Desc.Name, Desc.DataType, MakeArrayView((const int32*) Desc.Sizes, Desc.Dimension));

		InputTensors.Emplace(Tensor);
		Builder->AddInput(Tensor);
	}

	TArray<IMLModelBuilder::HTensor>	OutputTensors;

	for (int32 Idx = 0; Idx < InOutputTensors.Num(); ++Idx)
	{
		const FMLTensorDesc& Desc = InOutputTensors[Idx];
		IMLModelBuilder::HTensor Tensor = Builder->AddTensor(Desc.Name, Desc.DataType, MakeArrayView((const int32*) Desc.Sizes, Desc.Dimension));

		OutputTensors.Emplace(Tensor);
		Builder->AddOutput(Tensor);
	}

	auto Op = Builder->AddOperator(OperatorName);

	for (int32 Idx = 0; Idx < InputTensors.Num(); ++Idx)
	{
		Builder->AddOperatorInput(Op, InputTensors[Idx]);
	}

	for (int32 Idx = 0; Idx < OutputTensors.Num(); ++Idx)
	{
		Builder->AddOperatorOutput(Op, OutputTensors[Idx]);
	}

	Builder->End(ModelData);

	return true;
}

/** Return instance of ONNX model builder */
NNXUTILS_API IMLModelBuilder* CreateONNXModelBuilder(int64 IrVersion, int64 OpsetVersion)
{
	return new FMLModelBuilderONNX(IrVersion, OpsetVersion);
}

} // namespace NNX

